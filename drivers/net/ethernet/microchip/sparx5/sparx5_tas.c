// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2024 Microchip Technology Inc. and its subsidiaries.
 */

#include <linux/iopoll.h>
#include <net/pkt_sched.h>

#include "sparx5_main.h"
#include "sparx5_qos.h"

#define SPX5_TAS_TIMEOUT_US		(1000 * USEC_PER_MSEC)
#define SPX5_TAS_LISTS_PER_PORT		2

/* Cycle times and intervals from 1 us to under a second */
#define SPX5_TAS_MIN_TIME_NS		NSEC_PER_USEC
#define SPX5_TAS_MAX_TIME_NS		(NSEC_PER_SEC - 1)

/* TAS link speeds for calculation of guard band */
enum sparx5_tas_link_speed {
	SPX5_TAS_SPEED_NO_GB,
	SPX5_TAS_SPEED_10,
	SPX5_TAS_SPEED_100,
	SPX5_TAS_SPEED_1000,
	SPX5_TAS_SPEED_2500,
	SPX5_TAS_SPEED_5000,
	SPX5_TAS_SPEED_10000,
	SPX5_TAS_SPEED_25000,
};

enum sparx5_tas_state {
	SPX5_TAS_STATE_ADMIN,
	SPX5_TAS_STATE_ADVANCING,
	SPX5_TAS_STATE_PENDING,
	SPX5_TAS_STATE_OPERATING,
	SPX5_TAS_STATE_TERMINATING,
};

/* The scheduler element of a port, where the gates sit: after the layer
 * 0 elements on LAN969x, after the layer 0 and 1 elements on SparX-5.
 */
static u32 sparx5_tas_sched_pos(struct sparx5 *sparx5, u32 portno)
{
	u32 pos = regs->gcnt[GC_HSCH_HSCH_CFG] + portno;

	return is_sparx5(sparx5) ? pos + SPX5_HSCH_L1_SE_CNT : pos;
}

static u32 sparx5_tas_num_lists(struct sparx5 *sparx5)
{
	return sparx5->port_count * SPX5_TAS_LISTS_PER_PORT;
}

/* The chip has fewer lists than port numbers, so a port gets its two
 * from the ports that exist before it.
 */
static u32 sparx5_tas_list_index(struct sparx5_port *port, u32 entry)
{
	struct sparx5 *sparx5 = port->sparx5;
	u32 portno, pidx = 0;

	for (portno = 0; portno < port->portno; portno++)
		if (sparx5->ports[portno])
			pidx++;

	return pidx * SPX5_TAS_LISTS_PER_PORT + entry;
}

static void sparx5_tas_list_select(struct sparx5 *sparx5, u32 list)
{
	spx5_rmw(HSCH_TAS_CFG_CTRL_LIST_NUM_SET(list),
		 HSCH_TAS_CFG_CTRL_LIST_NUM, sparx5, HSCH_TAS_CFG_CTRL);
}

static u32 sparx5_tas_list_state_get(struct sparx5 *sparx5)
{
	u32 val = spx5_rd(sparx5, HSCH_TAS_LIST_STATE);

	return HSCH_TAS_LIST_STATE_LIST_STATE_GET(val);
}

static void sparx5_tas_list_state_set(struct sparx5 *sparx5, u32 state)
{
	spx5_rmw(HSCH_TAS_LIST_STATE_LIST_STATE_SET(state),
		 HSCH_TAS_LIST_STATE_LIST_STATE, sparx5, HSCH_TAS_LIST_STATE);
}

static void sparx5_tas_gcl_select(struct sparx5 *sparx5, u32 entry)
{
	spx5_rmw(HSCH_TAS_CFG_CTRL_GCL_ENTRY_NUM_SET(entry),
		 HSCH_TAS_CFG_CTRL_GCL_ENTRY_NUM, sparx5, HSCH_TAS_CFG_CTRL);
}

/* One step towards stopping the selected list: a list that has not
 * started goes straight back to admin, an operating one is terminated
 * and gets there on its own.  The chip may overwrite the state, so it is
 * set until it sticks.
 */
static u32 sparx5_tas_list_stop(struct sparx5 *sparx5, bool *operating)
{
	u32 state = sparx5_tas_list_state_get(sparx5);

	switch (state) {
	case SPX5_TAS_STATE_ADVANCING:
	case SPX5_TAS_STATE_PENDING:
		sparx5_tas_list_state_set(sparx5, SPX5_TAS_STATE_ADMIN);
		break;
	case SPX5_TAS_STATE_OPERATING:
		sparx5_tas_list_state_set(sparx5, SPX5_TAS_STATE_TERMINATING);
		fallthrough;
	case SPX5_TAS_STATE_TERMINATING:
		*operating = true;
		break;
	default:
		break;
	}

	return state;
}

static int sparx5_tas_list_shutdown(struct sparx5_port *port, u32 list)
{
	struct sparx5 *sparx5 = port->sparx5;
	bool operating = false;
	u32 state;
	int err;

	sparx5_tas_list_select(sparx5, list);

	err = read_poll_timeout(sparx5_tas_list_stop, state,
				state == SPX5_TAS_STATE_ADMIN, 100,
				SPX5_TAS_TIMEOUT_US, false, sparx5, &operating);
	if (err) {
		netdev_err(port->ndev, "TAS list %u stuck in state %u\n", list,
			   state);
		return err;
	}

	if (operating) {
		/* The gates stay as the schedule left them */
		u32 pos = sparx5_tas_sched_pos(sparx5, port->portno);

		spx5_wr(HSCH_TAS_GATE_STATE_CTRL_HSCH_POS_SET(pos),
			sparx5, HSCH_TAS_GATE_STATE_CTRL);
		spx5_wr(HSCH_TAS_GATE_STATE_TAS_GATE_STATE_SET(0xff),
			sparx5, HSCH_TAS_GATE_STATE);
	}

	return 0;
}

/* The list to put a new schedule in.  A list that is operating is left
 * to run until the new one starts; one that has not started is stopped.
 */
static int sparx5_tas_list_find(struct sparx5_port *port, u32 *new,
				u32 *obsolete)
{
	struct sparx5 *sparx5 = port->sparx5;
	int i, oper = -1, free = -1;

	for (i = 0; i < SPX5_TAS_LISTS_PER_PORT; i++) {
		u32 list = sparx5_tas_list_index(port, i);
		u32 state;

		sparx5_tas_list_select(sparx5, list);
		state = sparx5_tas_list_state_get(sparx5);
		if (state == SPX5_TAS_STATE_OPERATING) {
			oper = list;
			continue;
		}

		if (state != SPX5_TAS_STATE_ADMIN) {
			int err = sparx5_tas_list_shutdown(port, list);

			if (err)
				return err;
		}

		if (free < 0)
			free = list;
	}

	if (free < 0)
		return -ENOSPC;

	*new = free;
	*obsolete = oper < 0 ? free : oper;

	return 0;
}

static int sparx5_tas_check(struct sparx5 *sparx5,
			    struct tc_taprio_qopt_offload *qopt)
{
	u64 total = 0;
	u32 i;

	if (qopt->cycle_time_extension) {
		NL_SET_ERR_MSG_MOD(qopt->extack,
				   "Cycle time extension is not supported");
		return -EOPNOTSUPP;
	}

	if (qopt->num_entries > sparx5->data->consts->n_tas_gcl) {
		NL_SET_ERR_MSG_MOD(qopt->extack,
				   "Too many gate control list entries");
		return -EINVAL;
	}

	if (qopt->cycle_time < SPX5_TAS_MIN_TIME_NS ||
	    qopt->cycle_time > SPX5_TAS_MAX_TIME_NS) {
		NL_SET_ERR_MSG_MOD(qopt->extack,
				   "Cycle time must be from 1 us to 1 s");
		return -EINVAL;
	}

	for (i = 0; i < qopt->num_entries; i++) {
		struct tc_taprio_sched_entry *entry = &qopt->entries[i];

		if (entry->command != TC_TAPRIO_CMD_SET_GATES) {
			NL_SET_ERR_MSG_MOD(qopt->extack,
					   "Only set-gate-states entries are supported");
			return -EOPNOTSUPP;
		}

		if (entry->interval < SPX5_TAS_MIN_TIME_NS ||
		    entry->interval > SPX5_TAS_MAX_TIME_NS) {
			NL_SET_ERR_MSG_MOD(qopt->extack,
					   "Intervals must be from 1 us to 1 s");
			return -EINVAL;
		}

		total += entry->interval;
	}

	if (total > qopt->cycle_time) {
		NL_SET_ERR_MSG_MOD(qopt->extack,
				   "The intervals exceed the cycle time");
		return -EINVAL;
	}

	return 0;
}

/* Mark the entries held by the started lists of all ports.  A SparX-5
 * list is a contiguous block, a LAN969x list links each entry to the
 * next.
 */
static void sparx5_tas_gcl_used(struct sparx5 *sparx5, unsigned long *used)
{
	u32 num_lists = sparx5_tas_num_lists(sparx5);
	u32 list, cfg, base, next;

	for (list = 0; list < num_lists; list++) {
		sparx5_tas_list_select(sparx5, list);
		if (sparx5_tas_list_state_get(sparx5) == SPX5_TAS_STATE_ADMIN)
			continue;

		cfg = spx5_rd(sparx5, HSCH_TAS_LIST_CFG);
		base = HSCH_TAS_LIST_CFG_LIST_BASE_ADDR_GET(cfg);

		if (is_sparx5(sparx5)) {
			bitmap_set(used, base,
				   HSCH_TAS_LIST_CFG_LIST_LENGTH_GET(cfg));
			continue;
		}

		next = base;
		do {
			__set_bit(next, used);
			sparx5_tas_gcl_select(sparx5, next);
			cfg = spx5_rd(sparx5, HSCH_TAS_GCL_CTRL_CFG2);
			next = HSCH_TAS_GCL_CTRL_CFG2_NEXT_GCL_GET(cfg);
		} while (next != base && !test_bit(next, used));
	}
}

/* Write the schedule to free entries and point the list at them.  SparX-5
 * addresses an entry from the base of the selected list, LAN969x from
 * the start of the table.
 */
static int sparx5_tas_gcl_setup(struct sparx5_port *port, u32 list,
				struct tc_taprio_qopt_offload *qopt)
{
	struct sparx5 *sparx5 = port->sparx5;
	u32 num = sparx5->data->consts->n_tas_gcl;
	u32 sched = sparx5_tas_sched_pos(sparx5, port->portno);
	unsigned long *used;
	u32 base, i;

	used = bitmap_zalloc(num, GFP_KERNEL);
	if (!used)
		return -ENOMEM;

	sparx5_tas_gcl_used(sparx5, used);
	base = bitmap_find_next_zero_area(used, num, 0, qopt->num_entries, 0);
	bitmap_free(used);

	if (base >= num) {
		NL_SET_ERR_MSG_MOD(qopt->extack,
				   "No room for the gate control list");
		return -ENOSPC;
	}

	sparx5_tas_list_select(sparx5, list);

	if (is_sparx5(sparx5))
		spx5_wr(HSCH_TAS_LIST_CFG_LIST_BASE_ADDR_SET(base) |
			HSCH_TAS_LIST_CFG_LIST_LENGTH_SET(qopt->num_entries),
			sparx5, HSCH_TAS_LIST_CFG);
	else
		spx5_wr(HSCH_TAS_LIST_CFG_LIST_BASE_ADDR_SET(base) |
			HSCH_TAS_LIST_CFG_LIST_PORT_NUM_SET(port->portno) |
			HSCH_TAS_LIST_CFG_LIST_HSCH_POS_SET(sched),
			sparx5, HSCH_TAS_LIST_CFG);

	for (i = 0; i < qopt->num_entries; i++) {
		struct tc_taprio_sched_entry *entry = &qopt->entries[i];

		if (is_sparx5(sparx5)) {
			sparx5_tas_gcl_select(sparx5, i);
			spx5_wr(HSCH_TAS_GCL_CTRL_CFG_GATE_STATE_SET(entry->gate_mask) |
				HSCH_TAS_GCL_CTRL_CFG_HSCH_POS_SET(sched) |
				HSCH_TAS_GCL_CTRL_CFG_PORT_PROFILE_SET(port->portno),
				sparx5, HSCH_TAS_GCL_CTRL_CFG);
		} else {
			u32 next = base + (i + 1) % qopt->num_entries;

			sparx5_tas_gcl_select(sparx5, base + i);
			spx5_wr(HSCH_TAS_GCL_CTRL_CFG_GATE_STATE_SET(entry->gate_mask),
				sparx5, HSCH_TAS_GCL_CTRL_CFG);
			spx5_wr(HSCH_TAS_GCL_CTRL_CFG2_NEXT_GCL_SET(next),
				sparx5, HSCH_TAS_GCL_CTRL_CFG2);
		}

		spx5_wr(entry->interval, sparx5, HSCH_TAS_GCL_TIME_CFG);
	}

	return 0;
}

int sparx5_tc_taprio_add(struct sparx5_port *port,
			 struct tc_taprio_qopt_offload *qopt)
{
	struct sparx5 *sparx5 = port->sparx5;
	u32 new, obsolete;
	struct timespec64 ts;
	ktime_t base_time;
	int err;

	err = sparx5_tas_check(sparx5, qopt);
	if (err)
		return err;

	err = sparx5_tas_list_find(port, &new, &obsolete);
	if (err)
		return err;

	err = sparx5_tas_gcl_setup(port, new, qopt);
	if (err)
		return err;

	sparx5_new_base_time(sparx5, qopt->cycle_time, qopt->base_time,
			     &base_time);
	ts = ktime_to_timespec64(base_time);

	spx5_wr(HSCH_TAS_BASE_TIME_NSEC_BASE_TIME_NSEC_SET(ts.tv_nsec),
		sparx5, HSCH_TAS_BASE_TIME_NSEC);
	spx5_wr(lower_32_bits(ts.tv_sec), sparx5, HSCH_TAS_BASE_TIME_SEC_LSB);
	spx5_wr(HSCH_TAS_BASE_TIME_SEC_MSB_BASE_TIME_SEC_MSB_SET(upper_32_bits(ts.tv_sec)),
		sparx5, HSCH_TAS_BASE_TIME_SEC_MSB);
	spx5_wr(qopt->cycle_time, sparx5, HSCH_TAS_CYCLE_TIME_CFG);

	/* The list running when this one starts is stopped by it */
	spx5_rmw(HSCH_TAS_STARTUP_CFG_OBSOLETE_IDX_SET(obsolete),
		 HSCH_TAS_STARTUP_CFG_OBSOLETE_IDX,
		 sparx5, HSCH_TAS_STARTUP_CFG);

	sparx5_tas_list_state_set(sparx5, SPX5_TAS_STATE_ADVANCING);

	return 0;
}

int sparx5_tc_taprio_del(struct sparx5_port *port)
{
	int i, err = 0;

	for (i = 0; i < SPX5_TAS_LISTS_PER_PORT && !err; i++)
		err = sparx5_tas_list_shutdown(port,
					       sparx5_tas_list_index(port, i));

	return err;
}

void sparx5_tas_speed(struct sparx5_port *port, int speed)
{
	struct sparx5 *sparx5 = port->sparx5;
	u8 spd;

	switch (speed) {
	case SPEED_10:
		spd = SPX5_TAS_SPEED_10;
		break;
	case SPEED_100:
		spd = SPX5_TAS_SPEED_100;
		break;
	case SPEED_1000:
		spd = SPX5_TAS_SPEED_1000;
		break;
	case SPEED_2500:
		spd = SPX5_TAS_SPEED_2500;
		break;
	case SPEED_5000:
		spd = SPX5_TAS_SPEED_5000;
		break;
	case SPEED_10000:
		spd = SPX5_TAS_SPEED_10000;
		break;
	case SPEED_25000:
		spd = SPX5_TAS_SPEED_25000;
		break;
	default:
		netdev_err(port->ndev, "TAS: Unsupported speed: %d\n", speed);
		return;
	}

	spx5_rmw(HSCH_TAS_PROFILE_CONFIG_LINK_SPEED_SET(spd),
		 HSCH_TAS_PROFILE_CONFIG_LINK_SPEED,
		 sparx5,
		 HSCH_TAS_PROFILE_CONFIG(port->portno));
}

int sparx5_tas_init(struct sparx5 *sparx5)
{
	const struct sparx5_consts *consts = sparx5->data->consts;
	u32 portno;

	/* A frame that cannot finish before its gate closes is held back */
	spx5_rmw(HSCH_TAS_CFG_CTRL_LIST_NUM_MAX_SET(sparx5_tas_num_lists(sparx5)) |
		 HSCH_TAS_CFG_CTRL_ALWAYS_GUARD_BAND_SCH_Q_SET(1),
		 HSCH_TAS_CFG_CTRL_LIST_NUM_MAX |
		 HSCH_TAS_CFG_CTRL_ALWAYS_GUARD_BAND_SCH_Q,
		 sparx5, HSCH_TAS_CFG_CTRL);

	/* SparX-5 ties a profile to its port here, LAN969x in each list */
	if (!is_sparx5(sparx5))
		return 0;

	for (portno = 0; portno < consts->n_ports; portno++) {
		if (!sparx5->ports[portno])
			continue;

		spx5_rmw(HSCH_TAS_PROFILE_CONFIG_PORT_NUM_SET(portno),
			 HSCH_TAS_PROFILE_CONFIG_PORT_NUM,
			 sparx5, HSCH_TAS_PROFILE_CONFIG(portno));
	}

	return 0;
}

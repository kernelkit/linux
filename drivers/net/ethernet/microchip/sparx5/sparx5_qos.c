// SPDX-License-Identifier: GPL-2.0+
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#include <net/pkt_cls.h>

#include "sparx5_main.h"
#include "sparx5_qos.h"

/* Calculate new base_time based on cycle_time.
 *
 * The hardware requires a base_time that is always in the future.
 * We define threshold_time as current_time + (2 * cycle_time).
 * If base_time is below threshold_time this function recalculates it to be in
 * the interval:
 * threshold_time <= base_time < (threshold_time + cycle_time)
 *
 * A very simple algorithm could be like this:
 * new_base_time = org_base_time + N * cycle_time
 * using the lowest N so (new_base_time >= threshold_time
 */
void sparx5_new_base_time(struct sparx5 *sparx5, const u32 cycle_time,
			  const ktime_t org_base_time, ktime_t *new_base_time)
{
	ktime_t current_time, threshold_time, new_time;
	struct timespec64 ts;
	u64 nr_of_cycles_p2;
	u64 nr_of_cycles;
	u64 diff_time;

	new_time = org_base_time;

	sparx5_ptp_gettime64(&sparx5->phc[SPARX5_PHC_PORT].info, &ts);
	current_time = timespec64_to_ktime(ts);
	threshold_time = current_time + (2 * cycle_time);
	diff_time = threshold_time - new_time;
	nr_of_cycles = div_u64(diff_time, cycle_time);
	nr_of_cycles_p2 = 1; /* Use 2^0 as start value */

	if (new_time >= threshold_time) {
		*new_base_time = new_time;
		return;
	}

	/* Calculate the smallest power of 2 (nr_of_cycles_p2)
	 * that is larger than nr_of_cycles.
	 */
	while (nr_of_cycles_p2 < nr_of_cycles)
		nr_of_cycles_p2 <<= 1; /* Next (higher) power of 2 */

	/* Add as big chunks (power of 2 * cycle_time)
	 * as possible for each power of 2
	 */
	while (nr_of_cycles_p2) {
		if (new_time < threshold_time) {
			new_time += cycle_time * nr_of_cycles_p2;
			while (new_time < threshold_time)
				new_time += cycle_time * nr_of_cycles_p2;
			new_time -= cycle_time * nr_of_cycles_p2;
		}
		nr_of_cycles_p2 >>= 1; /* Next (lower) power of 2 */
	}
	new_time += cycle_time;
	*new_base_time = new_time;
}

/* Max rates for leak groups */
static const u32 spx5_hsch_max_group_rate[SPX5_HSCH_LEAK_GRP_CNT] = {
	1048568, /*  1.049 Gbps */
	2621420, /*  2.621 Gbps */
	10485680, /* 10.486 Gbps */
	26214200 /* 26.214 Gbps */
};

int sparx5_hsch_l0_get_idx(struct sparx5 *sparx5, int port, int queue)
{
	const struct sparx5_consts *consts = sparx5->data->consts;

	return (consts->n_hsch_l1_elems * port) +
	       (consts->n_hsch_queues * queue);
}

u32 sparx5_get_hsch_max_group_rate(int grp)
{
	return spx5_hsch_max_group_rate[grp];
}

static struct sparx5_layer layers[SPX5_HSCH_LAYER_CNT];

static u32 sparx5_lg_get_leak_time(struct sparx5 *sparx5, u32 layer, u32 group)
{
	u32 value;

	value = spx5_rd(sparx5, HSCH_HSCH_TIMER_CFG(layer, group));
	return HSCH_HSCH_TIMER_CFG_LEAK_TIME_GET(value);
}

static void sparx5_lg_set_leak_time(struct sparx5 *sparx5, u32 layer, u32 group,
				    u32 leak_time)
{
	spx5_wr(HSCH_HSCH_TIMER_CFG_LEAK_TIME_SET(leak_time), sparx5,
		HSCH_HSCH_TIMER_CFG(layer, group));
}

static u32 sparx5_lg_get_first(struct sparx5 *sparx5, u32 layer, u32 group)
{
	u32 value;

	value = spx5_rd(sparx5, HSCH_HSCH_LEAK_CFG(layer, group));
	return HSCH_HSCH_LEAK_CFG_LEAK_FIRST_GET(value);
}

static u32 sparx5_lg_get_next(struct sparx5 *sparx5, u32 layer, u32 group,
			      u32 idx)

{
	u32 value;

	value = spx5_rd(sparx5, HSCH_SE_CONNECT(idx));
	return HSCH_SE_CONNECT_SE_LEAK_LINK_GET(value);
}

static u32 sparx5_lg_get_last(struct sparx5 *sparx5, u32 layer, u32 group)
{
	u32 itr, next;

	itr = sparx5_lg_get_first(sparx5, layer, group);

	for (;;) {
		next = sparx5_lg_get_next(sparx5, layer, group, itr);
		if (itr == next)
			return itr;

		itr = next;
	}
}

static bool sparx5_lg_is_last(struct sparx5 *sparx5, u32 layer, u32 group,
			      u32 idx)
{
	return idx == sparx5_lg_get_next(sparx5, layer, group, idx);
}

static bool sparx5_lg_is_first(struct sparx5 *sparx5, u32 layer, u32 group,
			       u32 idx)
{
	return idx == sparx5_lg_get_first(sparx5, layer, group);
}

static bool sparx5_lg_is_empty(struct sparx5 *sparx5, u32 layer, u32 group)
{
	return sparx5_lg_get_leak_time(sparx5, layer, group) == 0;
}

static bool sparx5_lg_is_singular(struct sparx5 *sparx5, u32 layer, u32 group)
{
	if (sparx5_lg_is_empty(sparx5, layer, group))
		return false;

	return sparx5_lg_get_first(sparx5, layer, group) ==
	       sparx5_lg_get_last(sparx5, layer, group);
}

static void sparx5_lg_enable(struct sparx5 *sparx5, u32 layer, u32 group,
			     u32 leak_time)
{
	sparx5_lg_set_leak_time(sparx5, layer, group, leak_time);
}

static void sparx5_lg_disable(struct sparx5 *sparx5, u32 layer, u32 group)
{
	sparx5_lg_set_leak_time(sparx5, layer, group, 0);
}

static int sparx5_lg_get_group_by_index(struct sparx5 *sparx5, u32 layer,
					u32 idx, u32 *group)
{
	u32 itr, next;
	int i;

	for (i = 0; i < SPX5_HSCH_LEAK_GRP_CNT; i++) {
		if (sparx5_lg_is_empty(sparx5, layer, i))
			continue;

		itr = sparx5_lg_get_first(sparx5, layer, i);

		for (;;) {
			next = sparx5_lg_get_next(sparx5, layer, i, itr);

			if (itr == idx) {
				*group = i;
				return 0; /* Found it */
			}
			if (itr == next)
				break; /* Was not found */

			itr = next;
		}
	}

	return -1;
}

static int sparx5_lg_get_group_by_rate(u32 layer, u32 rate, u32 *group)
{
	struct sparx5_layer *l = &layers[layer];
	struct sparx5_lg *lg;
	u32 i;

	for (i = 0; i < SPX5_HSCH_LEAK_GRP_CNT; i++) {
		lg = &l->leak_groups[i];
		if (rate <= lg->max_rate) {
			*group = i;
			return 0;
		}
	}

	return -1;
}

static int sparx5_lg_get_adjacent(struct sparx5 *sparx5, u32 layer, u32 group,
				  u32 idx, u32 *prev, u32 *next, u32 *first)
{
	u32 itr;

	*first = sparx5_lg_get_first(sparx5, layer, group);
	*prev = *first;
	*next = *first;
	itr = *first;

	for (;;) {
		*next = sparx5_lg_get_next(sparx5, layer, group, itr);

		if (itr == idx)
			return 0; /* Found it */

		if (itr == *next)
			return -1; /* Was not found */

		*prev = itr;
		itr = *next;
	}

	return -1;
}

static int sparx5_lg_conf_set(struct sparx5 *sparx5, u32 layer, u32 group,
			      u32 se_first, u32 idx, u32 idx_next, bool empty)
{
	u32 leak_time = layers[layer].leak_groups[group].leak_time;

	/* Stop leaking */
	sparx5_lg_disable(sparx5, layer, group);

	if (empty)
		return 0;

	/* Select layer */
	spx5_rmw(HSCH_HSCH_CFG_CFG_HSCH_LAYER_SET(layer),
		 HSCH_HSCH_CFG_CFG_HSCH_LAYER, sparx5, HSCH_HSCH_CFG_CFG);

	/* Link elements */
	spx5_wr(HSCH_SE_CONNECT_SE_LEAK_LINK_SET(idx_next), sparx5,
		HSCH_SE_CONNECT(idx));

	/* Set the first element. */
	spx5_rmw(HSCH_HSCH_LEAK_CFG_LEAK_FIRST_SET(se_first),
		 HSCH_HSCH_LEAK_CFG_LEAK_FIRST, sparx5,
		 HSCH_HSCH_LEAK_CFG(layer, group));

	/* Start leaking */
	sparx5_lg_enable(sparx5, layer, group, leak_time);

	return 0;
}

static int sparx5_lg_del(struct sparx5 *sparx5, u32 layer, u32 group, u32 idx)
{
	u32 first, next, prev;
	bool empty = false;

	/* idx *must* be present in the leak group */
	WARN_ON(sparx5_lg_get_adjacent(sparx5, layer, group, idx, &prev, &next,
				       &first) < 0);

	if (sparx5_lg_is_singular(sparx5, layer, group)) {
		empty = true;
	} else if (sparx5_lg_is_last(sparx5, layer, group, idx)) {
		/* idx is removed, prev is now last */
		idx = prev;
		next = prev;
	} else if (sparx5_lg_is_first(sparx5, layer, group, idx)) {
		/* idx is removed and points to itself, first is next */
		first = next;
		next = idx;
	} else {
		/* Next is not touched */
		idx = prev;
	}

	return sparx5_lg_conf_set(sparx5, layer, group, first, idx, next,
				  empty);
}

static int sparx5_lg_add(struct sparx5 *sparx5, u32 layer, u32 new_group,
			 u32 idx)
{
	u32 first, next, old_group;

	pr_debug("ADD: layer: %d, new_group: %d, idx: %d", layer, new_group,
		 idx);

	/* Is this SE already shaping ? */
	if (sparx5_lg_get_group_by_index(sparx5, layer, idx, &old_group) >= 0) {
		if (old_group != new_group) {
			/* Delete from old group */
			sparx5_lg_del(sparx5, layer, old_group, idx);
		} else {
			/* Nothing to do here */
			return 0;
		}
	}

	/* We always add to head of the list */
	first = idx;

	if (sparx5_lg_is_empty(sparx5, layer, new_group))
		next = idx;
	else
		next = sparx5_lg_get_first(sparx5, layer, new_group);

	return sparx5_lg_conf_set(sparx5, layer, new_group, first, idx, next,
				  false);
}

static int sparx5_shaper_conf_set(struct sparx5_port *port,
				  const struct sparx5_shaper *sh, u32 layer,
				  u32 idx, u32 group)
{
	int (*sparx5_lg_action)(struct sparx5 *, u32, u32, u32);
	struct sparx5 *sparx5 = port->sparx5;

	if (!sh->rate && !sh->burst)
		sparx5_lg_action = &sparx5_lg_del;
	else
		sparx5_lg_action = &sparx5_lg_add;

	/* Select layer */
	spx5_rmw(HSCH_HSCH_CFG_CFG_HSCH_LAYER_SET(layer),
		 HSCH_HSCH_CFG_CFG_HSCH_LAYER, sparx5, HSCH_HSCH_CFG_CFG);

	/* Set frame mode */
	spx5_rmw(HSCH_SE_CFG_SE_FRM_MODE_SET(sh->mode), HSCH_SE_CFG_SE_FRM_MODE,
		 sparx5, HSCH_SE_CFG(idx));

	/* Set committed rate and burst */
	spx5_wr(HSCH_CIR_CFG_CIR_RATE_SET(sh->rate) |
			HSCH_CIR_CFG_CIR_BURST_SET(sh->burst),
		sparx5, HSCH_CIR_CFG(idx));

	/* This has to be done after the shaper configuration has been set */
	sparx5_lg_action(sparx5, layer, group, idx);

	return 0;
}

/* The element serves a weighted input in inverse proportion to its cost,
 * 1 to 32, held as cost - 1.  The scale is the cost of the smallest
 * weight: the biggest scale rounds the other costs coarsest, 60:30:5:5
 * at scale 32 becomes 3:5:32:32, a 5:3 split, where scale 24 gives
 * 2:4:24:24.  Try every scale and keep the one whose costs stray least
 * from the weights.
 */
static void sparx5_dwrr_cost_set(struct sparx5_dwrr *dwrr, const u32 *weight)
{
	u32 w_min = U32_MAX, w_max = 0, best = U32_MAX;
	u32 scale, ref, err, i;
	u8 cost[SPX5_PRIOS];

	if (!dwrr->count)
		return;

	for (i = 0; i < dwrr->count; i++) {
		w_min = min(w_min, weight[i]);
		w_max = max(w_max, weight[i]);
	}

	for (scale = SPX5_DWRR_COST_MAX + 1; scale > 0; scale--) {
		ref = scale * w_min;
		err = 0;
		for (i = 0; i < dwrr->count; i++) {
			cost[i] = max(DIV_ROUND_CLOSEST(ref, weight[i]), 1U);
			err = max(err, abs_diff(cost[i] * weight[i], ref) * 1000 / ref);
		}
		if (err < best) {
			best = err;
			for (i = 0; i < dwrr->count; i++)
				dwrr->cost[i] = cost[i] - 1;
		}
		/* A smaller scale cannot resolve the biggest weight */
		if (ref <= w_max)
			break;
	}
}

static int sparx5_dwrr_conf_set(struct sparx5_port *port,
				struct sparx5_dwrr *dwrr)
{
	struct sparx5 *sparx5 = port->sparx5;
	u32 val, first = 0, top = 0, l0, idx;
	u8 inp[SPX5_PRIOS];
	int q;

	/* The chip builds the hierarchy, so read where the queues go: one
	 * element, which serves its inputs 0 to SE_DWRR_CNT weighted and
	 * the rest strict, so the strict queues must sit above there too.
	 */
	for (q = 0; q < SPX5_PRIOS; q++) {
		l0 = sparx5_hsch_l0_get_idx(sparx5, port->portno, q);
		val = spx5_rd(sparx5, HSCH_HSCH_L0_CFG(l0));
		if (q == 0) {
			first = val;
		} else if ((val ^ first) & (HSCH_HSCH_L0_CFG_NEXT_LAYER |
					    HSCH_HSCH_L0_CFG_NEXT_IDX)) {
			netdev_warn(port->ndev,
				    "the queues are not scheduled by one element\n");
			return -EOPNOTSUPP;
		}
		inp[q] = HSCH_HSCH_L0_CFG_NEXT_INP_GET(val);
		if (q < dwrr->count) {
			top = max_t(u32, top, inp[q]);
		} else if (dwrr->count && inp[q] <= top) {
			netdev_warn(port->ndev,
				    "queue %d is scheduled below a weighted one\n",
				    q);
			return -EOPNOTSUPP;
		}
	}
	idx = HSCH_HSCH_L0_CFG_NEXT_IDX_GET(first);

	spx5_rmw(HSCH_HSCH_CFG_CFG_HSCH_LAYER_SET(
			 HSCH_HSCH_L0_CFG_NEXT_LAYER_GET(first) ? 2 : 1) |
		 HSCH_HSCH_CFG_CFG_CFG_SE_IDX_SET(idx),
		 HSCH_HSCH_CFG_CFG_HSCH_LAYER | HSCH_HSCH_CFG_CFG_CFG_SE_IDX,
		 sparx5, HSCH_HSCH_CFG_CFG);

	spx5_rmw(HSCH_SE_CFG_SE_DWRR_CNT_SET(top), HSCH_SE_CFG_SE_DWRR_CNT,
		 sparx5, HSCH_SE_CFG(idx));

	for (q = 0; q < dwrr->count; q++)
		spx5_rmw(HSCH_DWRR_ENTRY_DWRR_COST_SET(dwrr->cost[q]),
			 HSCH_DWRR_ENTRY_DWRR_COST, sparx5,
			 HSCH_DWRR_ENTRY(inp[q]));

	return 0;
}

static int sparx5_leak_groups_init(struct sparx5 *sparx5)
{
	const struct sparx5_ops *ops = sparx5->data->ops;
	struct sparx5_layer *layer;
	u32 sys_clk_per_100ps;
	struct sparx5_lg *lg;
	u32 leak_time_us;
	int i, ii;

	sys_clk_per_100ps = spx5_rd(sparx5, HSCH_SYS_CLK_PER);

	for (i = 0; i < SPX5_HSCH_LAYER_CNT; i++) {
		layer = &layers[i];
		for (ii = 0; ii < SPX5_HSCH_LEAK_GRP_CNT; ii++) {
			lg = &layer->leak_groups[ii];
			lg->max_rate = ops->get_hsch_max_group_rate(ii);

			/* Calculate the leak time in us, to serve a maximum
			 * rate of 'max_rate' for this group
			 */
			leak_time_us = (SPX5_SE_RATE_MAX * 1000) / lg->max_rate;

			/* Hardware wants leak time in ns */
			lg->leak_time = 1000 * leak_time_us;

			/* Calculate resolution */
			lg->resolution = 1000 / leak_time_us;

			/* Maximum number of shapers that can be served by
			 * this leak group
			 */
			lg->max_ses = (1000 * leak_time_us) / sys_clk_per_100ps;

			/* Example:
			 * Wanted bandwidth is 100Mbit:
			 *
			 * 100 mbps can be served by leak group zero.
			 *
			 * leak_time is 125000 ns.
			 * resolution is: 8
			 *
			 * cir          = 100000 / 8 = 12500
			 * leaks_pr_sec = 125000 / 10^9 = 8000
			 * bw           = 12500 * 8000 = 10^8 (100 Mbit)
			 */

			/* Disable by default - this also indicates an empty
			 * leak group
			 */
			sparx5_lg_disable(sparx5, i, ii);
		}
	}

	return 0;
}

int sparx5_qos_init(struct sparx5 *sparx5)
{
	int ret;

	ret = sparx5_leak_groups_init(sparx5);
	if (ret < 0)
		return ret;

	ret = sparx5_dcb_init(sparx5);
	if (ret < 0)
		return ret;

	ret = sparx5_tas_init(sparx5);
	if (ret < 0)
		return ret;

	sparx5_psfp_init(sparx5);

	return 0;
}

int sparx5_tc_mqprio_add(struct net_device *ndev, u8 num_tc)
{
	int i;

	if (num_tc != SPX5_PRIOS) {
		netdev_err(ndev, "Only %d traffic classes supported\n",
			   SPX5_PRIOS);
		return -EINVAL;
	}

	netdev_set_num_tc(ndev, num_tc);

	for (i = 0; i < num_tc; i++)
		netdev_set_tc_queue(ndev, i, 1, i);

	netdev_dbg(ndev, "dev->num_tc %u dev->real_num_tx_queues %u\n",
		   ndev->num_tc, ndev->real_num_tx_queues);

	return 0;
}

int sparx5_tc_mqprio_del(struct net_device *ndev)
{
	netdev_reset_tc(ndev);

	netdev_dbg(ndev, "dev->num_tc %u dev->real_num_tx_queues %u\n",
		   ndev->num_tc, ndev->real_num_tx_queues);

	return 0;
}

/* Shape a scheduler element: rate in kbit/s, burst in bytes */
static int sparx5_shaper_add(struct sparx5_port *port, u32 layer, u32 idx,
			     u32 rate, u64 burst)
{
	struct sparx5_shaper sh = {
		.mode = SPX5_SE_MODE_DATARATE,
	};
	struct sparx5_lg *lg;
	u32 group;

	/* Find suitable group for this se */
	if (sparx5_lg_get_group_by_rate(layer, rate, &group) < 0) {
		pr_debug("Could not find leak group for se with rate: %d",
			 rate);
		return -EINVAL;
	}

	lg = &layers[layer].leak_groups[group];

	pr_debug("Found matching group (speed: %d)\n", lg->max_rate);

	if (rate < SPX5_SE_RATE_MIN || burst < SPX5_SE_BURST_MIN)
		return -EINVAL;

	/* Calculate committed rate and burst */
	sh.rate = DIV_ROUND_UP(rate, lg->resolution);
	burst = DIV_ROUND_UP_ULL(burst, SPX5_SE_BURST_UNIT);

	if (sh.rate > SPX5_SE_RATE_MAX || burst > SPX5_SE_BURST_MAX)
		return -EINVAL;

	sh.burst = burst;

	return sparx5_shaper_conf_set(port, &sh, layer, idx, group);
}

static int sparx5_shaper_del(struct sparx5_port *port, u32 layer, u32 idx)
{
	struct sparx5_shaper sh = {0};
	u32 group;

	if (sparx5_lg_get_group_by_index(port->sparx5, layer, idx, &group) < 0)
		return 0;

	return sparx5_shaper_conf_set(port, &sh, layer, idx, group);
}

int sparx5_tc_tbf_add(struct sparx5_port *port,
		      struct tc_tbf_qopt_offload_replace_params *params,
		      u32 layer, u32 idx)
{
	return sparx5_shaper_add(port, layer, idx,
				 div_u64(params->rate.rate_bytes_ps, 1000) * 8,
				 params->max_size);
}

int sparx5_tc_tbf_del(struct sparx5_port *port, u32 layer, u32 idx)
{
	return sparx5_shaper_del(port, layer, idx);
}

int sparx5_tc_ets_add(struct sparx5_port *port,
		      struct tc_ets_qopt_offload_replace_params *params)
{
	struct sparx5_dwrr dwrr = {0};
	u32 weight[SPX5_PRIOS];
	int i;

	for (i = 0; i < SPX5_PRIOS; i++) {
		/* Strict band; skip */
		if (params->quanta[i] == 0)
			continue;

		dwrr.count++;

		/* On the sparx5, bands with higher indexes are preferred and
		 * arbitrated strict. Strict bands are put in the lower indexes,
		 * by tc, so we reverse the bands here.
		 */
		weight[SPX5_PRIOS - i - 1] = params->weights[i];
	}
	sparx5_dwrr_cost_set(&dwrr, weight);

	return sparx5_dwrr_conf_set(port, &dwrr);
}

int sparx5_tc_ets_del(struct sparx5_port *port)
{
	struct sparx5_dwrr dwrr = {0};

	return sparx5_dwrr_conf_set(port, &dwrr);
}

int sparx5_tc_cbs_add(struct sparx5_port *port,
		      struct tc_cbs_qopt_offload *qopt, u32 idx)
{
	u64 burst;
	int err;

	if (qopt->idleslope <= 0 || qopt->sendslope >= 0 ||
	    qopt->locredit >= qopt->hicredit)
		return -EINVAL;

	/* The burst is the credit span scaled from the send slope to the
	 * link rate: the bytes the class sends at once on a full credit.
	 */
	burst = (u64)(qopt->idleslope - qopt->sendslope) *
		(qopt->hicredit - qopt->locredit);
	burst = div_u64(burst, -qopt->sendslope);

	err = sparx5_shaper_add(port, 0, idx, qopt->idleslope, burst);
	if (err)
		return err;

	/* Run the element as a credit-based shaper */
	spx5_rmw(HSCH_SE_CFG_SE_AVB_ENA_SET(1), HSCH_SE_CFG_SE_AVB_ENA,
		 port->sparx5, HSCH_SE_CFG(idx));

	return 0;
}

int sparx5_tc_cbs_del(struct sparx5_port *port, u32 idx)
{
	spx5_rmw(HSCH_SE_CFG_SE_AVB_ENA_SET(0), HSCH_SE_CFG_SE_AVB_ENA,
		 port->sparx5, HSCH_SE_CFG(idx));

	return sparx5_shaper_del(port, 0, idx);
}

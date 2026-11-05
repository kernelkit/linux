/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip Sparx5 Switch driver
 *
 * Copyright (c) 2022 Microchip Technology Inc. and its subsidiaries.
 */

#ifndef __SPARX5_QOS_H__
#define __SPARX5_QOS_H__

#include <linux/netdevice.h>

/* Number of Layers */
#define SPX5_HSCH_LAYER_CNT 3

/* Scheduling elements per layer */
#define SPX5_HSCH_L0_SE_CNT 5040
#define SPX5_HSCH_L1_SE_CNT 64
#define SPX5_HSCH_L2_SE_CNT 64

/* Layer 0 Scheduler Element of a port queue in the normal hierarchy */
int sparx5_hsch_l0_get_idx(struct sparx5 *sparx5, int port, int queue);

/* Number of leak groups */
#define SPX5_HSCH_LEAK_GRP_CNT 4

/* Scheduler modes */
#define SPX5_SE_MODE_LINERATE 0
#define SPX5_SE_MODE_DATARATE 1

/* Rate and burst */
#define SPX5_SE_RATE_MAX 131071
#define SPX5_SE_BURST_MAX 127
#define SPX5_SE_RATE_MIN 1
#define SPX5_SE_BURST_MIN 1
#define SPX5_SE_BURST_UNIT 4096

/* Dwrr */
#define SPX5_DWRR_COST_MAX 31

struct sparx5_shaper {
	u32 mode;
	u32 rate;
	u32 burst;
};

struct sparx5_lg {
	u32 max_rate;
	u32 resolution;
	u32 leak_time;
	u32 max_ses;
};

struct sparx5_layer {
	struct sparx5_lg leak_groups[SPX5_HSCH_LEAK_GRP_CNT];
};

struct sparx5_dwrr {
	u32 count; /* Queues 0 to count - 1 are weighted, the rest strict */
	u8 cost[SPX5_PRIOS]; /* Per queue */
};

int sparx5_qos_init(struct sparx5 *sparx5);
int sparx5_tas_init(struct sparx5 *sparx5);
void sparx5_tas_speed(struct sparx5_port *port, int speed);

int sparx5_tc_prio_tc_set(struct sparx5_port *port, const u8 *prio_tc);

/* Multi-Queue Priority */
struct tc_mqprio_qopt;
int sparx5_tc_mqprio_add(struct net_device *ndev,
			 const struct tc_mqprio_qopt *qopt);
int sparx5_tc_mqprio_del(struct net_device *ndev);

/* Token Bucket Filter */
struct tc_tbf_qopt_offload_replace_params;
int sparx5_tc_tbf_add(struct sparx5_port *port,
		      struct tc_tbf_qopt_offload_replace_params *params,
		      u32 layer, u32 idx);
int sparx5_tc_tbf_del(struct sparx5_port *port, u32 layer, u32 idx);

/* Enhanced Transmission Selection */
struct tc_ets_qopt_offload_replace_params;
int sparx5_tc_ets_add(struct sparx5_port *port,
		      struct tc_ets_qopt_offload_replace_params *params);

int sparx5_tc_ets_del(struct sparx5_port *port);

/* Credit-Based Shaper */
struct tc_cbs_qopt_offload;
int sparx5_tc_cbs_add(struct sparx5_port *port,
		      struct tc_cbs_qopt_offload *qopt, u32 idx);
int sparx5_tc_cbs_del(struct sparx5_port *port, u32 idx);

/* Time-Aware Shaper */
struct tc_taprio_qopt_offload;
int sparx5_tc_taprio_add(struct sparx5_port *port,
			 struct tc_taprio_qopt_offload *qopt);
int sparx5_tc_taprio_del(struct sparx5_port *port);

u32 sparx5_get_hsch_max_group_rate(int grp);

#endif	/* __SPARX5_QOS_H__ */

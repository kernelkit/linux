/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (C) 2015-2018 Texas Instruments Incorporated - http://www.ti.com
 */

#ifndef __ICSS_SWITCH_H
#define __ICSS_SWITCH_H

/* Basic Switch Parameters
 * Used to auto compute offset addresses on L3 OCMC RAM. Do not modify these
 * without changing firmware accordingly
 */
#define SWITCH_BUFFER_SIZE	(64 * 1024)	/* L3 buffer */
#define ICSS_BLOCK_SIZE		32		/* data bytes per BD */
#define BD_SIZE			4		/* byte buffer descriptor */
 /* Task(22173) - EMAC/RSTP Tx Queues re-design 
  * As we Added 3 new queues i.e PTP, SV, GOOSE, Number of Queue size is 7 
  * Roopak@cit - 24-August-2023
  */
#define NUM_QUEUES		7		/* Queues on Port 0/1/2 */
#define RX_NUM_QUEUES		4		/* Queues on Port 0/1/2 */

#define PORT_LINK_MASK		0x1
#define PORT_IS_HD_MASK		0x2

/* Physical Port queue size (number of BDs). Same for both ports */
#define QUEUE_1_SIZE		97	/* Network Management high */
#define QUEUE_2_SIZE		97	/* Network Management low */
#define QUEUE_3_SIZE		97	/* Protocol specific */
#define QUEUE_4_SIZE		97	/* NRT (IP,ARP, ICMP) */
 /* Task(22173) - EMAC/RSTP Tx Queues re-design 
  * Adding 3 new queues i.e PTP, SV, GOOSE
  * Roopak@cit - 24-August-2023
  */
#define QUEUE_5_SIZE		97 // PTP
#define QUEUE_6_SIZE		97 // SV
#define QUEUE_7_SIZE		97 // GOOSE
/* Below code was added for HSR/PRP TX optimization
*  Parvathi@CIT - 19-Aug-2022
*/
#define QUEUE_3_HSRPRP_TXOPT_SIZE		194	/* Protocol specific */
#define QUEUE_4_HSRPRP_TXOPT_SIZE		194	/* NRT (IP,ARP, ICMP) */
/* Below macros are for HSR/PRP Updated queue structure ( Dedicated queues for PTP, SV and GOOSE )
 * Roopak@CIT - 02-June-2023
 */
#define QUEUE_5_HSRPRP_TXOPT_SIZE		194
#define QUEUE_6_HSRPRP_TXOPT_SIZE		194
#define QUEUE_7_HSRPRP_TXOPT_SIZE		194


/* Host queue size (number of BDs). Each BD points to data buffer of 32 bytes.
 * HOST PORT QUEUES can buffer up to 4 full sized frames per queue
 */
#define	HOST_QUEUE_1_SIZE	194	/* Protocol and VLAN priority 7 & 6 */
#define HOST_QUEUE_2_SIZE	194	/* Protocol mid */
#define HOST_QUEUE_3_SIZE	194	/* Protocol low */
#define HOST_QUEUE_4_SIZE	194	/* NRT (IP, ARP, ICMP) */

#define COL_QUEUE_SIZE		0

/* NRT Buffer descriptor definition
 * Each buffer descriptor points to a max 32 byte block and has 32 bit in size
 * to have atomic operation.
 * PRU can address bytewise into memory.
 * Definition of 32 bit descriptor is as follows
 *
 * Bits		Name			Meaning
 * =============================================================================
 * 0..7		Index		points to index in buffer queue, max 256 x 32
 *				byte blocks can be addressed
 * 6            LookupSuccess   For switch, FDB lookup was successful (source
 *                              MAC address found in FDB).
 *                              For RED, NodeTable lookup was successful.
 * 7            Flood           Packet should be flooded (destination MAC
 *                              address found in FDB). For switch only.
 * 8..12	Block_length	number of valid bytes in this specific block.
 *				Will be <=32 bytes on last block of packet
 * 13		More		"More" bit indicating that there are more blocks
 * 14		Shadow		indicates that "index" is pointing into shadow
 *				buffer
 * 15		TimeStamp	indicates that this packet has time stamp in
 *				separate buffer - only needed of PTCP runs on
 *				host
 * 16..17	Port		different meaning for ingress and egress,
 *				Ingress: Port = 0 indicates phy port 1 and
 *				Port = 1 indicates phy port 2.
 *				Egress: 0 sends on phy port 1 and 1 sends on
 *				phy port 2. Port = 2 goes over MAC table
 *				look-up
 * 18..28	Length		11 bit of total packet length which is put into
 *				first BD only so that host access only one BD
 * 29		VlanTag		indicates that packet has Length/Type field of
 *				0x08100 with VLAN tag in following byte
 * 30		Broadcast	indicates that packet goes out on both physical
 *				ports,  there will be two bd but only one buffer
 * 31		Error		indicates there was an error in the packet
 */
#define PRUETH_BD_START_FLAG_MASK	BIT(0)
#define PRUETH_BD_START_FLAG_SHIFT	0

#define PRUETH_BD_HSR_FRAME_MASK	BIT(4)
#define PRUETH_BD_HSR_FRAME_SHIFT	4

#define PRUETH_BD_SUP_HSR_FRAME_MASK	BIT(5)
#define PRUETH_BD_SUP_HSR_FRAME_SHIFT	5

#define PRUETH_BD_LOOKUP_SUCCESS_MASK	BIT(6)
#define PRUETH_BD_LOOKUP_SUCCESS_SHIFT	6

#define PRUETH_BD_SW_FLOOD_MASK		BIT(7)
#define PRUETH_BD_SW_FLOOD_SHIFT	7

/* Below macro was added for HSR RX optimization
 *  bit indicates whether packet is consumed by host or not.
 *  Re-using Bit 10 as it is currently not used.
 *  basharath@CIT - 08-Sep-2023
 */
#define PRUETH_BD_HOST_RECV_MASK    BIT(10)
#define PRUETH_BD_HOST_RECV_SHIFT	10

#define	PRUETH_LL_HAS_NO_HSRTAG_MASK	BIT(13)
#define	PRUETH_LL_HAS_NO_HSRTAG_SHIFT	13

#define	PRUETH_BD_SHADOW_MASK		BIT(14)
#define	PRUETH_BD_SHADOW_SHIFT		14

#define PRUETH_BD_TIMESTAMP_MASK	BIT(15)
#define PRUETH_BD_TIMESTAMP_SHIT	15

#define PRUETH_BD_PORT_MASK		GENMASK(17, 16)
#define PRUETH_BD_PORT_SHIFT		16

#define PRUETH_BD_LENGTH_MASK		GENMASK(28, 18)
#define PRUETH_BD_LENGTH_SHIFT		18

#define PRUETH_BD_BROADCAST_MASK	BIT(30)
#define PRUETH_BD_BROADCAST_SHIFT	30

#define PRUETH_BD_ERROR_MASK		BIT(31)
#define PRUETH_BD_ERROR_SHIFT		31

/* The following offsets indicate which sections of the memory are used
 * for EMAC internal tasks
 */
  /* Task(22173) - EMAC/RSTP Tx Queues re-design 
  * Below code change is for shifting port queue context and descriptors  
  * Roopak@cit - 23-August-2023
  */

#define DRAM_START_OFFSET		0x1e68
#define SRAM_START_OFFSET		0x400

/* General Purpose Statistics
 * These are present on both PRU0 and PRU1 DRAM
 */
/* base statistics offset */
#define STATISTICS_OFFSET	0x1f00
#define STAT_SIZE		0x98

/* The following offsets indicate which sections of the memory are used
 * for switch internal tasks
 */
#define SWITCH_SPECIFIC_DRAM0_START_SIZE		0x100
#define SWITCH_SPECIFIC_DRAM0_START_OFFSET		0x1F00

#define SWITCH_SPECIFIC_DRAM1_START_SIZE		0x300
/* Task(22173) - EMAC/RSTP Tx Queues re-design 
* Shifted the Queue context offset to accommodate space for 3 new queues
* Roopak@cit - 24-August-2023
*/
#define SWITCH_SPECIFIC_DRAM1_START_OFFSET		0x1C60

/* Offset for storing
 * 1. Storm Prevention Params
 * 2. PHY Speed Offset
 * 3. Port Status Offset
 * These are present on both PRU0 and PRU1
 */
/* 4 bytes */
#define STORM_PREVENTION_OFFSET_BC	(STATISTICS_OFFSET + STAT_SIZE)
/* 4 bytes */
#define PHY_SPEED_OFFSET		(STATISTICS_OFFSET + STAT_SIZE + 4)
/* 1 byte */
#define PORT_STATUS_OFFSET		(STATISTICS_OFFSET + STAT_SIZE + 8)
/* 1 byte */
#define COLLISION_COUNTER		(STATISTICS_OFFSET + STAT_SIZE + 9)
/* 4 bytes */
#define RX_PKT_SIZE_OFFSET		(STATISTICS_OFFSET + STAT_SIZE + 10)
/* 4 bytes */
#define PORT_CONTROL_ADDR		(STATISTICS_OFFSET + STAT_SIZE + 14)
/* 6 bytes */
#define PORT_MAC_ADDR			(STATISTICS_OFFSET + STAT_SIZE + 18)
/* 1 byte */
#define RX_INT_STATUS_OFFSET		(STATISTICS_OFFSET + STAT_SIZE + 24)
/* 4 bytes */
#define STORM_PREVENTION_OFFSET_MC	(STATISTICS_OFFSET + STAT_SIZE + 25)
/* 4 bytes */
#define STORM_PREVENTION_OFFSET_UC	(STATISTICS_OFFSET + STAT_SIZE + 29)
/* 4 bytes ? */
#define STP_INVALID_STATE_OFFSET        (STATISTICS_OFFSET + STAT_SIZE + 33)

/* 
* Task(24211) : Host queue overflow ethtool stat
* macros for Host and Fwd Queue overflow counters
* Roopak@cit - 24-January-2025
*/
#define RX_HOST_QUEUE_OVERFLOW_FRAMES_OFFSET    (STATISTICS_OFFSET + STAT_SIZE + 64)	//4 bytes
#define RX_FWD_QUEUE_OVERFLOW_FRAMES_OFFSET     (STATISTICS_OFFSET + STAT_SIZE + 68)	//4 bytes

/* DRAM1 Offsets for Switch */
/* 4 queue descriptors for port 0 (host receive) */
 /* Task(22173) - EMAC/RSTP Tx Queues re-design 
  * Shifting the Queue descriptor offset, to accomidate space for new queues
  * Roopak@cit - 24-August-2023
  */
#define SW_LRE_QUEUE_DESC_OFFSET 0x1E20
#define P0_QUEUE_DESC_OFFSET		SW_LRE_QUEUE_DESC_OFFSET
#define P1_QUEUE_DESC_OFFSET		P0_QUEUE_DESC_OFFSET + RX_NUM_QUEUES * 8 // as Host Queues are 4 and queue desc size is 8 so 4*8 will be size of p0 queue desc
#define P2_QUEUE_DESC_OFFSET		P1_QUEUE_DESC_OFFSET + NUM_QUEUES * 8
#define END_QUEUE_DESC_OFFSET		P2_QUEUE_DESC_OFFSET + NUM_QUEUES * 8

/* Task(22173) - EMAC/RSTP Tx Queues re-design 
* Shifting the Table offsets to accomidate space for new queues
* Roopak@cit - 24-August-2023
*/
#define QUEUE_SIZE_ADDR			0x1E00
#define QUEUE_OFFSET_ADDR		0x1DE8
#define QUEUE_DESCRIPTOR_OFFSET_ADDR	0x1DD0

/* Task(22173) - EMAC/RSTP Tx Queues re-design 
* Included Macros for Queue context of new queues
* Roopak@cit - 24-August-2023
*/
/* Port 2 Rx Context */
#define P2_Q7_RX_CONTEXT_OFFSET		(P2_Q6_RX_CONTEXT_OFFSET + 8)
#define P2_Q6_RX_CONTEXT_OFFSET		(P2_Q5_RX_CONTEXT_OFFSET + 8)
#define P2_Q5_RX_CONTEXT_OFFSET		(P2_Q4_RX_CONTEXT_OFFSET + 8)
#define P2_Q4_RX_CONTEXT_OFFSET		(P2_Q3_RX_CONTEXT_OFFSET + 8)
#define P2_Q3_RX_CONTEXT_OFFSET		(P2_Q2_RX_CONTEXT_OFFSET + 8)
#define P2_Q2_RX_CONTEXT_OFFSET		(P2_Q1_RX_CONTEXT_OFFSET + 8)
#define P2_Q1_RX_CONTEXT_OFFSET		RX_CONTEXT_P2_Q1_OFFSET_ADDR
#define RX_CONTEXT_P2_Q1_OFFSET_ADDR	(P1_Q7_RX_CONTEXT_OFFSET + 8)

/* Port 1 Rx Context */
#define P1_Q7_RX_CONTEXT_OFFSET		(P1_Q6_RX_CONTEXT_OFFSET + 8)
#define P1_Q6_RX_CONTEXT_OFFSET		(P1_Q5_RX_CONTEXT_OFFSET + 8)
#define P1_Q5_RX_CONTEXT_OFFSET		(P1_Q4_RX_CONTEXT_OFFSET + 8)
#define P1_Q4_RX_CONTEXT_OFFSET		(P1_Q3_RX_CONTEXT_OFFSET + 8)
#define P1_Q3_RX_CONTEXT_OFFSET		(P1_Q2_RX_CONTEXT_OFFSET + 8)
#define P1_Q2_RX_CONTEXT_OFFSET		(P1_Q1_RX_CONTEXT_OFFSET + 8)
#define P1_Q1_RX_CONTEXT_OFFSET		(RX_CONTEXT_P1_Q1_OFFSET_ADDR)
#define RX_CONTEXT_P1_Q1_OFFSET_ADDR	(P0_Q4_RX_CONTEXT_OFFSET + 8)

/* Host Port Rx Context */
#define P0_Q4_RX_CONTEXT_OFFSET		(P0_Q3_RX_CONTEXT_OFFSET + 8)
#define P0_Q3_RX_CONTEXT_OFFSET		(P0_Q2_RX_CONTEXT_OFFSET + 8)
#define P0_Q2_RX_CONTEXT_OFFSET		(P0_Q1_RX_CONTEXT_OFFSET + 8)
#define P0_Q1_RX_CONTEXT_OFFSET		RX_CONTEXT_P0_Q1_OFFSET_ADDR
#define RX_CONTEXT_P0_Q1_OFFSET_ADDR	(P2_Q7_TX_CONTEXT_OFFSET + 8)

/* Port 2 */
#define P2_Q7_TX_CONTEXT_OFFSET		(P2_Q6_TX_CONTEXT_OFFSET + 8)
#define P2_Q6_TX_CONTEXT_OFFSET		(P2_Q5_TX_CONTEXT_OFFSET + 8)
#define P2_Q5_TX_CONTEXT_OFFSET		(P2_Q4_TX_CONTEXT_OFFSET + 8)
#define P2_Q4_TX_CONTEXT_OFFSET		(P2_Q3_TX_CONTEXT_OFFSET + 8)
#define P2_Q3_TX_CONTEXT_OFFSET		(P2_Q2_TX_CONTEXT_OFFSET + 8)
#define P2_Q2_TX_CONTEXT_OFFSET		(P2_Q1_TX_CONTEXT_OFFSET + 8)
#define P2_Q1_TX_CONTEXT_OFFSET		TX_CONTEXT_P2_Q1_OFFSET_ADDR
#define TX_CONTEXT_P2_Q1_OFFSET_ADDR	(P1_Q7_TX_CONTEXT_OFFSET + 8)

/* Port 1 */
#define P1_Q7_TX_CONTEXT_OFFSET		(P1_Q6_TX_CONTEXT_OFFSET + 8)
#define P1_Q6_TX_CONTEXT_OFFSET		(P1_Q5_TX_CONTEXT_OFFSET + 8)
#define P1_Q5_TX_CONTEXT_OFFSET		(P1_Q4_TX_CONTEXT_OFFSET + 8)
#define P1_Q4_TX_CONTEXT_OFFSET		(P1_Q3_TX_CONTEXT_OFFSET + 8)
#define P1_Q3_TX_CONTEXT_OFFSET		(P1_Q2_TX_CONTEXT_OFFSET + 8)
#define P1_Q2_TX_CONTEXT_OFFSET		(P1_Q1_TX_CONTEXT_OFFSET + 8)
#define P1_Q1_TX_CONTEXT_OFFSET		TX_CONTEXT_P1_Q1_OFFSET_ADDR
#define TX_CONTEXT_P1_Q1_OFFSET_ADDR	SWITCH_SPECIFIC_DRAM1_START_OFFSET

/* Shared RAM Offsets for Switch */
/* NSP (Network Storm Prevention) timer re-uses NT timer */
#define PRUETH_NSP_CREDIT_SHIFT       8
#define PRUETH_NSP_ENABLE            BIT(0)

/* DRAM Offsets for EMAC
 * Present on Both DRAM0 and DRAM1
 */

 /* Task(22173) - EMAC/RSTP Tx Queues re-design 
  * Below code change is for shifting port queue context and descriptors  
  * Roopak@cit - 23-August-2023
  */

/* 7 queue descriptors for port tx = 56 bytes */
#define TX_CONTEXT_Q1_OFFSET_ADDR	(PORT_QUEUE_DESC_OFFSET + 56)
#define PORT_QUEUE_DESC_OFFSET	(ICSS_EMAC_TTS_CYC_TX_SOF + 8)

/* EMAC Time Triggered Send Offsets */
#define ICSS_EMAC_TTS_CYC_TX_SOF	(ICSS_EMAC_TTS_PREV_TX_SOF + 8)
#define ICSS_EMAC_TTS_PREV_TX_SOF	(ICSS_EMAC_TTS_MISSED_CYCLE_CNT_OFFSET + 4)
#define ICSS_EMAC_TTS_MISSED_CYCLE_CNT_OFFSET	(ICSS_EMAC_TTS_STATUS_OFFSET + 4)
#define ICSS_EMAC_TTS_STATUS_OFFSET	(ICSS_EMAC_TTS_CFG_TIME_OFFSET + 4)
#define ICSS_EMAC_TTS_CFG_TIME_OFFSET	(ICSS_EMAC_TTS_CYCLE_PERIOD_OFFSET + 4)
#define ICSS_EMAC_TTS_CYCLE_PERIOD_OFFSET	(ICSS_EMAC_TTS_CYCLE_START_OFFSET + 8)
#define ICSS_EMAC_TTS_CYCLE_START_OFFSET	ICSS_EMAC_TTS_BASE_OFFSET
#define ICSS_EMAC_TTS_BASE_OFFSET	DRAM_START_OFFSET

/* Shared RAM offsets for EMAC */

/* Queue Descriptors */

/* 4 queue descriptors for port 0 (host receive). 32 bytes */
#define HOST_QUEUE_DESC_OFFSET		(HOST_QUEUE_SIZE_ADDR + 16)

/* table offset for queue size:
 * 3 ports * 4 Queues * 1 byte offset = 12 bytes
 */
#define HOST_QUEUE_SIZE_ADDR		(HOST_QUEUE_OFFSET_ADDR + 8)
/* table offset for queue:
 * 4 Queues * 2 byte offset = 8 bytes
 */
#define HOST_QUEUE_OFFSET_ADDR		(HOST_QUEUE_DESCRIPTOR_OFFSET_ADDR + 8)
/* table offset for Host queue descriptors:
 * 1 ports * 4 Queues * 2 byte offset = 8 bytes
 */
#define HOST_QUEUE_DESCRIPTOR_OFFSET_ADDR	(HOST_Q4_RX_CONTEXT_OFFSET + 8)

/* Host Port Rx Context */
#define HOST_Q4_RX_CONTEXT_OFFSET	(HOST_Q3_RX_CONTEXT_OFFSET + 8)
#define HOST_Q3_RX_CONTEXT_OFFSET	(HOST_Q2_RX_CONTEXT_OFFSET + 8)
#define HOST_Q2_RX_CONTEXT_OFFSET	(HOST_Q1_RX_CONTEXT_OFFSET + 8)
#define HOST_Q1_RX_CONTEXT_OFFSET	(EMAC_PROMISCUOUS_MODE_OFFSET + 4)

/* Promiscuous mode control */
#define EMAC_P1_PROMISCUOUS_BIT		BIT(0)
#define EMAC_P2_PROMISCUOUS_BIT		BIT(1)
#define EMAC_PROMISCUOUS_MODE_OFFSET	(EMAC_RESERVED + 4)
#define EMAC_RESERVED			EOF_48K_BUFFER_BD

/* allow for max 48k buffer which spans the descriptors up to 0x1800 6kB */
#define EOF_48K_BUFFER_BD	(P0_BUFFER_DESC_OFFSET + HOST_BD_SIZE + PORT_BD_SIZE)

#define HOST_BD_SIZE		((HOST_QUEUE_1_SIZE + HOST_QUEUE_2_SIZE + HOST_QUEUE_3_SIZE + HOST_QUEUE_4_SIZE) * BD_SIZE)
#define PORT_BD_SIZE		((QUEUE_1_SIZE + QUEUE_2_SIZE + QUEUE_3_SIZE + QUEUE_4_SIZE) * 2 * BD_SIZE)
 /* Task(22173) - EMAC/RSTP Tx Queues re-design 
  * Adding 3 new queues i.e PTP, SV, GOOSE
  * Roopak@cit - 24-August-2023
  */
#define END_OF_BD_POOL		(P2_Q7_BD_OFFSET + QUEUE_7_SIZE * BD_SIZE)
#define P2_Q7_BD_OFFSET		(P2_Q6_BD_OFFSET + QUEUE_6_SIZE * BD_SIZE)
#define P2_Q6_BD_OFFSET		(P2_Q5_BD_OFFSET + QUEUE_5_SIZE * BD_SIZE)
#define P2_Q5_BD_OFFSET		(P2_Q4_BD_OFFSET + QUEUE_4_SIZE * BD_SIZE)
#define P2_Q4_BD_OFFSET		(P2_Q3_BD_OFFSET + QUEUE_3_SIZE * BD_SIZE)
#define P2_Q3_BD_OFFSET		(P2_Q2_BD_OFFSET + QUEUE_2_SIZE * BD_SIZE)
#define P2_Q2_BD_OFFSET		(P2_Q1_BD_OFFSET + QUEUE_1_SIZE * BD_SIZE)
#define P2_Q1_BD_OFFSET		(P1_Q7_BD_OFFSET + QUEUE_7_SIZE * BD_SIZE)
#define P1_Q7_BD_OFFSET		(P1_Q6_BD_OFFSET + QUEUE_6_SIZE * BD_SIZE)
#define P1_Q6_BD_OFFSET		(P1_Q5_BD_OFFSET + QUEUE_5_SIZE * BD_SIZE)
#define P1_Q5_BD_OFFSET		(P1_Q4_BD_OFFSET + QUEUE_4_SIZE * BD_SIZE)
#define P1_Q4_BD_OFFSET		(P1_Q3_BD_OFFSET + QUEUE_3_SIZE * BD_SIZE)
#define P1_Q3_BD_OFFSET		(P1_Q2_BD_OFFSET + QUEUE_2_SIZE * BD_SIZE)
#define P1_Q2_BD_OFFSET		(P1_Q1_BD_OFFSET + QUEUE_1_SIZE * BD_SIZE)
#define P1_Q1_BD_OFFSET		(P0_Q4_BD_OFFSET + HOST_QUEUE_4_SIZE * BD_SIZE)
#define P0_Q4_BD_OFFSET		(P0_Q3_BD_OFFSET + HOST_QUEUE_3_SIZE * BD_SIZE)
#define P0_Q3_BD_OFFSET		(P0_Q2_BD_OFFSET + HOST_QUEUE_2_SIZE * BD_SIZE)
#define P0_Q2_BD_OFFSET		(P0_Q1_BD_OFFSET + HOST_QUEUE_1_SIZE * BD_SIZE)

/* Below Macros are for HSR/PRP Updated queue structure ( Dedicated queues for PTP, SV and GOOSE )
 * Roopak@CIT - 19-September-2023
 */
#define EMAC_END_OF_BD_POOL		(EMAC_P2_Q7_BD_OFFSET + QUEUE_7_SIZE * BD_SIZE)
#define EMAC_P2_Q7_BD_OFFSET		(EMAC_P2_Q6_BD_OFFSET + QUEUE_6_SIZE * BD_SIZE)
#define EMAC_P2_Q6_BD_OFFSET		(EMAC_P2_Q5_BD_OFFSET + QUEUE_5_SIZE * BD_SIZE)
#define EMAC_P2_Q5_BD_OFFSET		(EMAC_P2_Q4_BD_OFFSET + QUEUE_4_SIZE * BD_SIZE)
#define EMAC_P2_Q4_BD_OFFSET		(EMAC_P2_Q3_BD_OFFSET + QUEUE_3_SIZE * BD_SIZE)
#define EMAC_P2_Q3_BD_OFFSET		(EMAC_P2_Q2_BD_OFFSET + QUEUE_2_SIZE * BD_SIZE)
#define EMAC_P2_Q2_BD_OFFSET		(EMAC_P2_Q1_BD_OFFSET + QUEUE_1_SIZE * BD_SIZE)
#define EMAC_P2_Q1_BD_OFFSET		EMAC_BD_START_OFFSET
#define EMAC_P1_Q7_BD_OFFSET		(EMAC_P1_Q6_BD_OFFSET + QUEUE_6_SIZE * BD_SIZE)
#define EMAC_P1_Q6_BD_OFFSET		(EMAC_P1_Q5_BD_OFFSET + QUEUE_5_SIZE * BD_SIZE)
#define EMAC_P1_Q5_BD_OFFSET		(EMAC_P1_Q4_BD_OFFSET + QUEUE_4_SIZE * BD_SIZE)
#define EMAC_P1_Q4_BD_OFFSET		(EMAC_P1_Q3_BD_OFFSET + QUEUE_3_SIZE * BD_SIZE)
#define EMAC_P1_Q3_BD_OFFSET		(EMAC_P1_Q2_BD_OFFSET + QUEUE_2_SIZE * BD_SIZE)
#define EMAC_P1_Q2_BD_OFFSET		(EMAC_P1_Q1_BD_OFFSET + QUEUE_1_SIZE * BD_SIZE)
#define EMAC_P1_Q1_BD_OFFSET		EMAC_BD_START_OFFSET
#define EMAC_BD_START_OFFSET    0x0840

/* Below code was added for HSR/PRP TX optimization
*  We have merged the Q3 and Q4 of both the ports to create larger queues commonly for both port1 and port2
*  | Port1 Q1 | Port1 Q2 | Port1/Port2 Q3 | Port2 Q1 | Port2 Q2 | Port1/Port2 Q4 |
*  Parvathi@CIT - 19-Aug-2022
*/

/* Below Macros are for HSR/PRP Updated queue structure ( Dedicated queues for PTP, SV and GOOSE )
 * Roopak@CIT - 02-June-2023
 */
#define HSRP1_TXOPT_Q3_BD_OFFSET      (P1_Q2_BD_OFFSET + QUEUE_2_SIZE * BD_SIZE)                        
#define HSRP2_TXOPT_Q1_BD_OFFSET      (HSRP1_TXOPT_Q3_BD_OFFSET + QUEUE_3_HSRPRP_TXOPT_SIZE * BD_SIZE)  
#define HSRP2_TXOPT_Q2_BD_OFFSET      (HSRP2_TXOPT_Q1_BD_OFFSET + QUEUE_1_SIZE * BD_SIZE)               
#define HSRP1_TXOPT_Q4_BD_OFFSET      (HSRP2_TXOPT_Q2_BD_OFFSET + QUEUE_2_SIZE * BD_SIZE)               
#define HSRP1_TXOPT_Q5_BD_OFFSET      (HSRP1_TXOPT_Q4_BD_OFFSET + QUEUE_4_HSRPRP_TXOPT_SIZE * BD_SIZE)  
#define HSRP1_TXOPT_Q6_BD_OFFSET      (HSRP1_TXOPT_Q5_BD_OFFSET + QUEUE_5_HSRPRP_TXOPT_SIZE * BD_SIZE)
#define HSRP1_TXOPT_Q7_BD_OFFSET      (HSRP1_TXOPT_Q6_BD_OFFSET + QUEUE_6_HSRPRP_TXOPT_SIZE * BD_SIZE)


#define P0_Q1_BD_OFFSET		P0_BUFFER_DESC_OFFSET                                                            // P0_Q1_BD_OFFSET = 0x0400
#define P0_BUFFER_DESC_OFFSET	SRAM_START_OFFSET

/* Memory Usage of L3 OCMC RAM */
 /* Task(22173) - EMAC/RSTP Tx Queues re-design 
  * Adding 3 new queues i.e PTP, SV, GOOSE
  * Roopak@cit - 24-August-2023
  */
/* L3 64KB Memory - mainly buffer Pool */
#define END_OF_BUFFER_POOL	(P2_Q7_BUFFER_OFFSET + QUEUE_7_SIZE * ICSS_BLOCK_SIZE)
#define P2_Q7_BUFFER_OFFSET	(P2_Q6_BUFFER_OFFSET + QUEUE_6_SIZE * ICSS_BLOCK_SIZE) // GOOSE
#define P2_Q6_BUFFER_OFFSET	(P2_Q5_BUFFER_OFFSET + QUEUE_5_SIZE * ICSS_BLOCK_SIZE) // SV
#define P2_Q5_BUFFER_OFFSET	(P2_Q4_BUFFER_OFFSET + QUEUE_4_SIZE * ICSS_BLOCK_SIZE) // PTP
#define P2_Q4_BUFFER_OFFSET	(P2_Q3_BUFFER_OFFSET + QUEUE_3_SIZE * ICSS_BLOCK_SIZE)
#define P2_Q3_BUFFER_OFFSET	(P2_Q2_BUFFER_OFFSET + QUEUE_2_SIZE * ICSS_BLOCK_SIZE)
#define P2_Q2_BUFFER_OFFSET	(P2_Q1_BUFFER_OFFSET + QUEUE_1_SIZE * ICSS_BLOCK_SIZE)
#define P2_Q1_BUFFER_OFFSET	(P1_Q7_BUFFER_OFFSET + QUEUE_7_SIZE * ICSS_BLOCK_SIZE)
#define P1_Q7_BUFFER_OFFSET	(P1_Q6_BUFFER_OFFSET + QUEUE_6_SIZE * ICSS_BLOCK_SIZE) // GOOSE
#define P1_Q6_BUFFER_OFFSET	(P1_Q5_BUFFER_OFFSET + QUEUE_5_SIZE * ICSS_BLOCK_SIZE) // SV
#define P1_Q5_BUFFER_OFFSET	(P1_Q4_BUFFER_OFFSET + QUEUE_4_SIZE * ICSS_BLOCK_SIZE) // PTP
#define P1_Q4_BUFFER_OFFSET	(P1_Q3_BUFFER_OFFSET + QUEUE_3_SIZE * ICSS_BLOCK_SIZE)
#define P1_Q3_BUFFER_OFFSET	(P1_Q2_BUFFER_OFFSET + QUEUE_2_SIZE * ICSS_BLOCK_SIZE)
#define P1_Q2_BUFFER_OFFSET	(P1_Q1_BUFFER_OFFSET + QUEUE_1_SIZE * ICSS_BLOCK_SIZE)
 /* Task(22173) - EMAC/RSTP Tx Queues re-design 
  * Shifted the tx queues to starting of second 64kb offset 
  * Roopak@cit - 23-August-2023
  */
#define P1_Q1_BUFFER_OFFSET	0x0000
#define P0_Q4_BUFFER_OFFSET	(P0_Q3_BUFFER_OFFSET + HOST_QUEUE_3_SIZE * ICSS_BLOCK_SIZE)
#define P0_Q3_BUFFER_OFFSET	(P0_Q2_BUFFER_OFFSET + HOST_QUEUE_2_SIZE * ICSS_BLOCK_SIZE)
#define P0_Q2_BUFFER_OFFSET	(P0_Q1_BUFFER_OFFSET + HOST_QUEUE_1_SIZE * ICSS_BLOCK_SIZE)
/* Below code was added for HSR/PRP TX optimization
*  We have merged the Q3 and Q4 of both the ports to create larger queues for both port1 and port2
*  | Port1 Q1 | Port1 Q2 | Port1/Port2 Q3 | Port2 Q1 | Port2 Q2 | Port1/Port2 Q4 |
*  Parvathi@CIT - 19-Aug-2022
*/
/* Below Macros are for HSR/PRP Updated queue structure ( Dedicated queues for PTP, SV and GOOSE )
 * Roopak@CIT - 02-June-2023
 */
/* 
 * Task(22795) - HSR/PRP: Handle the hole in the OCMC by Port Tx queues
 * Removed forwarding queues and shift host tx queues to starting of the 2nd 64kb offset
 * Roopak@CIT - 03-june-2024
 */
#define HSRP1_TXOPT_Q3_BUFFER_OFFSET 0x0000
#define HSRP1_TXOPT_Q4_BUFFER_OFFSET (HSRP1_TXOPT_Q3_BUFFER_OFFSET + QUEUE_3_HSRPRP_TXOPT_SIZE * ICSS_BLOCK_SIZE)

/* Below macros are for HSR/PRP Updated queue structure ( Dedicated queues for PTP, SV and GOOSE )
 * Roopak@CIT - 02-June-2023
 */
#define HSRP1_TXOPT_Q5_BUFFER_OFFSET (HSRP1_TXOPT_Q4_BUFFER_OFFSET + QUEUE_4_HSRPRP_TXOPT_SIZE * ICSS_BLOCK_SIZE)
#define HSRP1_TXOPT_Q6_BUFFER_OFFSET (HSRP1_TXOPT_Q5_BUFFER_OFFSET + QUEUE_5_HSRPRP_TXOPT_SIZE * ICSS_BLOCK_SIZE)
#define HSRP1_TXOPT_Q7_BUFFER_OFFSET (HSRP1_TXOPT_Q6_BUFFER_OFFSET + QUEUE_6_HSRPRP_TXOPT_SIZE * ICSS_BLOCK_SIZE)

#define P0_COL_BUFFER_OFFSET    0xEE00
/* 
 * Task - HSR/PRP: Wave 2: OCMC memory offset change
 * Shifting the OCMC Start offset by 0x3000 as the 0x0000 to 0x3000 memory is used by system firmware
 * Below change is for shifting the Rx Queues by 0x3000 offset 
 * Roopak@CIT - 04-March-2024
 */
#define P0_Q1_BUFFER_OFFSET	0x3000

/* The below bit will be set in BD for EMAC mode in the egress
 * direction and reset for PRP mode
 */
#define PRUETH_TX_PRP_EMAC_MODE	BIT(0)

/* Below Rx Interrupt pacing defines. */
/* shared RAM */
/* 1 byte for pace control */
 /* Task(22173) - EMAC/RSTP Tx Queues re-design 
  * Below code change is for shifting interrupt pacing 
  * Roopak@cit - 22-August-2023
  */
/* EMAC Interrupt Pacing Control. They are per port */
/* 1 byte | 0 : Interrupt Pacing disabled | 1 : Interrupt Pacing enabled */
#define INTR_PAC_STATUS_OFFSET_PRU1             0x2FEA
/* 1 byte | 0 : Interrupt Pacing disabled | 1 : Interrupt Pacing enabled */
#define INTR_PAC_STATUS_OFFSET_PRU0             0x2FEB
/* HSR/PRP firmware Interrupt Pacing Control. They are common to both
 * ports
 */
#define INTR_PAC_STATUS_OFFSET                       0x2FEB
/* Interrupt Pacing disabled, Adaptive logic disabled */
#define INTR_PAC_DIS_ADP_LGC_DIS                     0x0
/* Interrupt Pacing enabled, Adaptive logic disabled */
#define INTR_PAC_ENA_ADP_LGC_DIS                     0x1
/* Interrupt Pacing enabled, Adaptive logic enabled */
#define INTR_PAC_ENA_ADP_LGC_ENA                     0x2

/* Timeout values are per port for both Dual EMAC and HSR/PRP */
/* 4 bytes | previous TS from eCAP TSCNT for PRU 0 */
#define INTR_PAC_PREV_TS_OFFSET_PRU0                 0x2FEC
/* 4 bytes | timer expiration value for PRU 0 */
#define INTR_PAC_TMR_EXP_OFFSET_PRU0                 0x2FF0
/* 4 bytes | previous TS from eCAP TSCNT for PRU 1 */
#define INTR_PAC_PREV_TS_OFFSET_PRU1                 0x2FF4
/* 4 bytes | timer expiration value for PRU 1 */
#define INTR_PAC_TMR_EXP_OFFSET_PRU1                 0x2FF8
#define INTR_PAC_PREV_TS_RESET_VAL                   0x0

#define V2_1_FDB_TBL_LOC          PRUETH_MEM_SHARED_RAM
 /* Task(22173) - EMAC/RSTP Tx Queues re-design 
  * Below code change is for shifting FDB Table 
  * Roopak@cit - 22-August-2023
  */
#define V2_1_FDB_TBL_OFFSET       0x3000

#define FDB_INDEX_TBL_MAX_ENTRIES     256
#define FDB_MAC_TBL_MAX_ENTRIES       256

#endif /* __ICSS_SWITCH_H */

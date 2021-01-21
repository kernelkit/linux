// SPDX-License-Identifier: GPL-2.0+
/*
 * TI Common Platform Time Sync
 *
 * Copyright (C) 2012 Richard Cochran <richardcochran@gmail.com>
 *
 */
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/if.h>
#include <linux/hrtimer.h>
#include <linux/module.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_classify.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#ifdef CONFIG_TI_1PPS_DM_TIMER
#include <linux/irqreturn.h>
#include <linux/interrupt.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#endif

#include "cpts.h"
#ifdef CONFIG_TI_1PPS_DM_TIMER
#include "ptp_bc.h"
#endif

#define CPTS_SKB_TX_WORK_TIMEOUT 1 /* jiffies */
#define CPTS_SKB_RX_TX_TMO 100 /*ms */
#define CPTS_EVENT_RX_TX_TIMEOUT (100) /* ms */

struct cpts_skb_cb_data {
	u32 skb_mtype_seqid;
	unsigned long tmo;
};

#define cpts_read32(c, r)	readl_relaxed(&c->reg->r)
#define cpts_write32(c, v, r)	writel_relaxed(v, &c->reg->r)

#ifdef CONFIG_TI_1PPS_DM_TIMER
#define READ_TCRR(odt) __omap_dm_timer_read((odt), OMAP_TIMER_COUNTER_REG, 0)
#define READ_TCLR(odt) __omap_dm_timer_read((odt), OMAP_TIMER_CTRL_REG, 0)
#define READ_TCAP(odt) __omap_dm_timer_read((odt), OMAP_TIMER_CAPTURE_REG, 0)
#define WRITE_TCRR(odt, val) __omap_dm_timer_write((odt), \
				OMAP_TIMER_COUNTER_REG, (val), 0)
#define WRITE_TLDR(odt, val) __omap_dm_timer_write((odt), \
				OMAP_TIMER_LOAD_REG, (val), 0)
#define WRITE_TMAR(odt, val) __omap_dm_timer_write((odt), \
				OMAP_TIMER_MATCH_REG, (val), 0)
#define WRITE_TCLR(odt, val) __omap_dm_timer_write((odt), \
				OMAP_TIMER_CTRL_REG, (val), 0)
#define WRITE_TSICR(odt, val) __omap_dm_timer_write((odt), \
				OMAP_TIMER_IF_CTRL_REG, (val), 0)

#define CPTS_TS_THRESH		98000000ULL
#define CPTS_TMR_CLK_RATE	100000000
#define CPTS_TMR_CLK_PERIOD	(1000000000 / CPTS_TMR_CLK_RATE)
#define CPTS_DEFAULT_PPS_WIDTH_MS	20
#define CPTS_DEFAULT_PPS_WIDTH_NS	(CPTS_DEFAULT_PPS_WIDTH_MS * 1000000UL)
#define CPTS_TMR_RELOAD_CNT	(0xFFFFFFFFUL - \
				 100000000UL / CPTS_TMR_CLK_PERIOD + 1)
#define CPTS_TMR_CMP_CNT	(CPTS_TMR_RELOAD_CNT + \
				 CPTS_DEFAULT_PPS_WIDTH_NS / \
				 CPTS_TMR_CLK_PERIOD)
#define CPTS_MAX_MMR_ACCESS_TIME	1000
#define CPTS_NOM_MMR_ACCESS_TIME	250
#define CPTS_NOM_MMR_ACCESS_TICK	(CPTS_NOM_MMR_ACCESS_TIME / \
					 CPTS_TMR_CLK_PERIOD)

#define CPTS_LATCH_TMR_RELOAD_CNT	(0xFFFFFFFFUL - \
					 1000000000UL / CPTS_TMR_CLK_PERIOD + 1)
#define CPTS_LATCH_TMR_CMP_CNT		(CPTS_LATCH_TMR_RELOAD_CNT + \
					 10000000UL / CPTS_TMR_CLK_PERIOD)
/* The following three constants define the edges and center of the
 * desired latch offset measurement window. We are able to calculate
 * the timestamp of the incoming 1PPS pulse by adjusting it with
 * the latch offset which is the distance from the input pulse to
 * the rollover time 0xFFFFFFFF when timer15 pulse is generated.
 * However we need to keep the offset to be as small as possible to
 * reduce the acumulation error introduced by frequency difference
 * between the timer15 and the PTP master.
 * The measurement point will move from the center to the left or right
 * based on the frequency difference and the latch processing state
 * machine will bring it back to the center when it exceeds the window
 * range. With the current window size as 50us from center to edge, the
 * algorithm can handle the frequency difference < 25PPM.
 */

#define CPTS_LATCH_TICK_THRESH_MIN	(50000 / CPTS_TMR_CLK_PERIOD)
#define CPTS_LATCH_TICK_THRESH_MAX	(150000 / CPTS_TMR_CLK_PERIOD)
#define CPTS_LATCH_TICK_THRESH_MID	((CPTS_LATCH_TICK_THRESH_MIN + \
					  CPTS_LATCH_TICK_THRESH_MAX) / 2)
#define CPTS_LATCH_TICK_THRESH_UNSYNC	(1000000 / CPTS_TMR_CLK_PERIOD)

#define CPTS_TMR_LATCH_DELAY		40

#define CPTS_LATCH_INIT_THRESH          2

static u32 tmr_reload_cnt = CPTS_TMR_RELOAD_CNT;
static u32 tmr_reload_cnt_prev = CPTS_TMR_RELOAD_CNT;
static int ts_correct;

static void cpts_tmr_reinit(struct cpts *cpts);
static void cpts_latch_tmr_init(struct cpts *cpts);
static irqreturn_t cpts_1pps_tmr_interrupt(int irq, void *dev_id);
static irqreturn_t cpts_1pps_latch_interrupt(int irq, void *dev_id);
static void cpts_tmr_poll(struct cpts *cpts, bool cpts_poll);
static void cpts_pps_schedule(struct cpts *cpts);
static inline void cpts_latch_pps_stop(struct cpts *cpts);
static void cpts_bc_mux_ctrl(void *ctx, int enable);
#endif

static int cpts_event_port(struct cpts_event *event)
{
	return (event->high >> PORT_NUMBER_SHIFT) & PORT_NUMBER_MASK;
}

static int event_expired(struct cpts_event *event)
{
	return time_after(jiffies, event->tmo);
}

static int event_type(struct cpts_event *event)
{
	return (event->high >> EVENT_TYPE_SHIFT) & EVENT_TYPE_MASK;
}

static int cpts_fifo_pop(struct cpts *cpts, u32 *high, u32 *low)
{
	u32 r = cpts_read32(cpts, intstat_raw);

	if (r & TS_PEND_RAW) {
		*high = cpts_read32(cpts, event_high);
		*low  = cpts_read32(cpts, event_low);
		cpts_write32(cpts, EVENT_POP, event_pop);
		return 0;
	}
	return -1;
}

static int cpts_purge_events(struct cpts *cpts)
{
	struct list_head *this, *next;
	struct cpts_event *event;
	int removed = 0;

	list_for_each_safe(this, next, &cpts->events) {
		event = list_entry(this, struct cpts_event, list);
		if (event_expired(event)) {
			list_del_init(&event->list);
			list_add(&event->list, &cpts->pool);
			++removed;
		}
	}

	if (removed)
		dev_dbg(cpts->dev, "cpts: event pool cleaned up %d\n", removed);
	return removed ? 0 : -1;
}

static void cpts_purge_txq(struct cpts *cpts)
{
	struct cpts_skb_cb_data *skb_cb;
	struct sk_buff *skb, *tmp;
	int removed = 0;

	skb_queue_walk_safe(&cpts->txq, skb, tmp) {
		skb_cb = (struct cpts_skb_cb_data *)skb->cb;
		if (time_after(jiffies, skb_cb->tmo)) {
			__skb_unlink(skb, &cpts->txq);
			dev_consume_skb_any(skb);
			++removed;
		}
	}

	if (removed)
		dev_dbg(cpts->dev, "txq cleaned up %d\n", removed);
}

/*
 * Returns zero if matching event type was found.
 */
static int cpts_fifo_read(struct cpts *cpts, int match)
{
	struct ptp_clock_event pevent;
	struct cpts_event *event;
	unsigned long flags;
	int i, type = -1;
	u32 hi, lo;
	bool need_schedule = false;

	spin_lock_irqsave(&cpts->lock, flags);

	for (i = 0; i < CPTS_FIFO_DEPTH; i++) {
		if (cpts_fifo_pop(cpts, &hi, &lo))
			break;

		if (list_empty(&cpts->pool) && cpts_purge_events(cpts)) {
			dev_info(cpts->dev, "cpts: event pool empty\n");
			break;
		}

		event = list_first_entry(&cpts->pool, struct cpts_event, list);
		event->high = hi;
		event->low = lo;
		event->timestamp = timecounter_cyc2time(&cpts->tc, event->low);
		type = event_type(event);

		dev_dbg(cpts->dev, "CPTS_EV: %d high:%08X low:%08x\n",
			type, event->high, event->low);
		trace_printk("CPTS_EV: %d high:%08X low:%08x ts:%llu\n",
			     type, event->high, event->low, event->timestamp);
		switch (type) {
		case CPTS_EV_PUSH:
			WRITE_ONCE(cpts->cur_timestamp, lo);
			timecounter_read(&cpts->tc);
			if (cpts->mult_new) {
				cpts->cc.mult = cpts->mult_new;
				cpts->mult_new = 0;
#ifdef CONFIG_TI_1PPS_DM_TIMER
				tmr_reload_cnt = cpts->ppb_new < 0 ?
					CPTS_TMR_RELOAD_CNT - (-cpts->ppb_new + 0) / (CPTS_TMR_CLK_PERIOD * 10) :
					CPTS_TMR_RELOAD_CNT + (cpts->ppb_new + 0) / (CPTS_TMR_CLK_PERIOD * 10);
#endif
			}
			if (!cpts->irq_poll)
				complete(&cpts->ts_push_complete);
			break;
		case CPTS_EV_TX:
		case CPTS_EV_RX:
			event->tmo = jiffies +
				msecs_to_jiffies(CPTS_EVENT_RX_TX_TIMEOUT);

			list_del_init(&event->list);
			list_add_tail(&event->list, &cpts->events);
			need_schedule = true;
			break;
		case CPTS_EV_ROLL:
		case CPTS_EV_HALF:
			break;
		case CPTS_EV_HW:
			pevent.timestamp = event->timestamp;
			pevent.type = PTP_CLOCK_EXTTS;
			pevent.index = cpts_event_port(event) - 1;
#ifdef CONFIG_TI_1PPS_DM_TIMER
			event->tmo = jiffies +
				msecs_to_jiffies(CPTS_EVENT_HWSTAMP_TIMEOUT);
			if (pevent.index == cpts->pps_hw_index) {
				list_del_init(&event->list);
				list_add_tail(&event->list, &cpts->events_pps);
			} else if (cpts->hw_ts_enable & BIT(pevent.index)) {
				pevent.timestamp -= cpts->pps_latch_offset;
				if (cpts->pps_latch_receive) {
					ptp_clock_event(cpts->clock, &pevent);
					cpts->pps_latch_receive = false;
				} else {
					cpts_latch_pps_stop(cpts);
					dev_info(cpts->dev, "fifo: enter pps_latch INIT state\n");
				}
			}
#else
			ptp_clock_event(cpts->clock, &pevent);
#endif
			break;
		default:
			dev_err(cpts->dev, "cpts: unknown event type\n");
			break;
		}
		if (type == match)
			break;
	}

	spin_unlock_irqrestore(&cpts->lock, flags);

	if (!cpts->irq_poll && need_schedule)
		ptp_schedule_worker(cpts->clock, 0);

	return type == match ? 0 : -1;
}

void cpts_misc_interrupt(struct cpts *cpts)
{
	cpts_fifo_read(cpts, -1);
}
EXPORT_SYMBOL_GPL(cpts_misc_interrupt);

static u64 cpts_systim_read(const struct cyclecounter *cc)
{
	struct cpts *cpts = container_of(cc, struct cpts, cc);

	return READ_ONCE(cpts->cur_timestamp);
}

static void cpts_update_cur_time(struct cpts *cpts, int match,
				 struct ptp_system_timestamp *sts)
{
	unsigned long flags;

	reinit_completion(&cpts->ts_push_complete);

	/* use spin_lock_irqsave() here as it has to run very fast */
	local_irq_save(flags);
	ptp_read_system_prets(sts);
	cpts_write32(cpts, TS_PUSH, ts_push);
	cpts_read32(cpts, ts_push);
	ptp_read_system_postts(sts);
	local_irq_restore(flags);

	if (cpts->irq_poll && cpts_fifo_read(cpts, match) && match != -1)
		dev_err(cpts->dev, "cpts: unable to obtain a time stamp\n");

	if (!cpts->irq_poll &&
	    !wait_for_completion_timeout(&cpts->ts_push_complete, HZ))
		dev_err(cpts->dev, "cpts: obtain a time stamp timeout\n");
}

/* PTP clock operations */

static int cpts_ptp_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);
	int neg_adj = 0;
	u32 diff, mult, nppb = ppb;
	u64 adj;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}
	mult = cpts->cc_mult;
	adj = mult;
	adj *= ppb;
	diff = div_u64(adj, 1000000000ULL);

	mutex_lock(&cpts->ptp_clk_mutex);

	cpts->mult_new = neg_adj ? mult - diff : mult + diff;
	cpts->ppb_new = nppb;

	cpts_update_cur_time(cpts, CPTS_EV_PUSH, NULL);

	mutex_unlock(&cpts->ptp_clk_mutex);

	return 0;
}

static int cpts_ptp_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);

	mutex_lock(&cpts->ptp_clk_mutex);
	timecounter_adjtime(&cpts->tc, delta);
#ifdef CONFIG_TI_1PPS_DM_TIMER
	cpts->ptp_adjusted = true;
#endif
	mutex_unlock(&cpts->ptp_clk_mutex);

	return 0;
}

static int cpts_ptp_gettimeex(struct ptp_clock_info *ptp,
			      struct timespec64 *ts,
			      struct ptp_system_timestamp *sts)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);
	u64 ns;

	mutex_lock(&cpts->ptp_clk_mutex);

	cpts_update_cur_time(cpts, CPTS_EV_PUSH, sts);

	ns = timecounter_read(&cpts->tc);
	mutex_unlock(&cpts->ptp_clk_mutex);

	*ts = ns_to_timespec64(ns);

	return 0;
}

static int cpts_ptp_settime(struct ptp_clock_info *ptp,
			    const struct timespec64 *ts)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);
	u64 ns;

	ns = timespec64_to_ns(ts);

	mutex_lock(&cpts->ptp_clk_mutex);
	timecounter_init(&cpts->tc, &cpts->cc, ns);
#ifdef CONFIG_TI_1PPS_DM_TIMER
	cpts->ptp_adjusted = true;
#endif
	mutex_unlock(&cpts->ptp_clk_mutex);

	return 0;
}

#ifdef CONFIG_TI_1PPS_DM_TIMER
/* PPS */
static int cpts_proc_pps_ts_events(struct cpts *cpts)
{
	struct list_head *this, *next;
	struct cpts_event *event;
	unsigned long flags;
	LIST_HEAD(events_free);
	LIST_HEAD(events);
	int reported = 0;
	int ev_type;

	spin_lock_irqsave(&cpts->lock, flags);
	list_splice_init(&cpts->events_pps, &events);
	spin_unlock_irqrestore(&cpts->lock, flags);

	list_for_each_safe(this, next, &events) {
		event = list_entry(this, struct cpts_event, list);

		if (time_after(jiffies, event->tmo)) {
			list_del_init(&event->list);
			list_add(&event->list, &events_free);
			dev_err(cpts->dev, "dropped PPS event %lld\n",
				event->timestamp);
			continue;
		}

		ev_type = event_type(event);

		if (ev_type != CPTS_EV_HW ||
		    cpts_event_port(event) != cpts->pps_hw_index + 1) {
			list_del_init(&event->list);
			list_add(&event->list, &events_free);
			dev_err(cpts->dev, "dropped PPS event %lld invalid id\n",
				event->timestamp);
		}

		list_del_init(&event->list);
		list_add(&event->list, &events_free);
		/* record the timestamp only */
		cpts->hw_timestamp = event->timestamp;
		++reported;
	}

	spin_lock_irqsave(&cpts->lock, flags);
	list_splice_tail(&events_free, &cpts->pool);
	spin_unlock_irqrestore(&cpts->lock, flags);

	return reported;
}

static void cpts_pps_kworker(struct kthread_work *work)
{
	struct cpts *cpts = container_of(work, struct cpts, pps_work.work);

	mutex_lock(&cpts->ptp_clk_mutex);
	cpts_pps_schedule(cpts);
	mutex_unlock(&cpts->ptp_clk_mutex);
}

static inline void cpts_pps_stop(struct cpts *cpts)
{
	u32 v;

	/* disable timer */
	v = READ_TCLR(cpts->odt);
	v &= ~BIT(0);
	WRITE_TCLR(cpts->odt, v);
}

static inline void cpts_pps_start(struct cpts *cpts)
{
	u32 v;

	cpts_tmr_reinit(cpts);

	/* enable timer */
	v = READ_TCLR(cpts->odt);
	v |= BIT(0);
	WRITE_TCLR(cpts->odt, v);
}

static int cpts_pps_enable(struct cpts *cpts, int on)
{
	on = (on ? 1 : 0);

	if ((cpts->pps_enable == -1) && on == 0)
		return 0;

	if (cpts->pps_enable == on)
		return 0;

	cpts->pps_enable = on;

	/* will stop after up coming pulse */
	if (!on)
		return 0;

	if (cpts->pps_enable_gpiod) {
		spin_lock_bh(&cpts->bc_mux_lock);
		gpiod_set_value(cpts->pps_enable_gpiod, 1);
		spin_unlock_bh(&cpts->bc_mux_lock);
	}

	if (cpts->ref_enable == -1)
		cpts_pps_start(cpts);

	return 0;
}

static int cpts_ref_enable(struct cpts *cpts, int on)
{
	on = (on ? 1 : 0);

	if ((cpts->ref_enable == -1) && on == 0)
		return 0;

	if (cpts->ref_enable == on)
		return 0;

	cpts->ref_enable = on;

	/* will stop after up coming pulse */
	if (!on)
		return 0;

	if (cpts->pps_enable == -1)
		cpts_pps_start(cpts);

	return 0;
}

static void cpts_pps_schedule(struct cpts *cpts)
{
	bool reported;

	cpts_fifo_read(cpts, -1);
	reported = cpts_proc_pps_ts_events(cpts);

	if (cpts->pps_enable >= 0 || cpts->ref_enable >= 0) {
		if (!cpts->pps_enable) {
			cpts->pps_enable = -1;
			pinctrl_select_state(cpts->pins,
					     cpts->pin_state_pwm_off);
			if (cpts->pps_enable_gpiod) {
				spin_lock_bh(&cpts->bc_mux_lock);
				gpiod_set_value(cpts->pps_enable_gpiod, 0);
				spin_unlock_bh(&cpts->bc_mux_lock);
			}
		}

		if (!cpts->ref_enable) {
			cpts->ref_enable = -1;
			pinctrl_select_state(cpts->pins,
					     cpts->pin_state_ref_off);
		}

		if ((cpts->pps_enable == -1) && (cpts->ref_enable == -1)) {
			cpts_pps_stop(cpts);
		} else {
			if (reported)
				cpts_tmr_poll(cpts, true);
		}
	}

	if (reported != 1)
		dev_info(cpts->dev,
			 "error:%s is called with %d CPTS HW events!\n",
			 __func__, reported);
}
#endif

static int cpts_extts_enable(struct cpts *cpts, u32 index, int on)
{
	u32 v;

#ifdef CONFIG_TI_1PPS_DM_TIMER
	if (index >= cpts->info.n_ext_ts || index == cpts->pps_hw_index)
#else
	if (index >= cpts->info.n_ext_ts)
#endif
		return -ENXIO;

	if (((cpts->hw_ts_enable & BIT(index)) >> index) == on)
		return 0;

	mutex_lock(&cpts->ptp_clk_mutex);

	v = cpts_read32(cpts, control);
	if (on) {
		v |= BIT(8 + index);
		cpts->hw_ts_enable |= BIT(index);
#ifdef CONFIG_TI_1PPS_DM_TIMER
		if (cpts->use_1pps_latch)
			pinctrl_select_state(cpts->pins, cpts->pin_state_latch_on);
#endif
	} else {
		v &= ~BIT(8 + index);
		cpts->hw_ts_enable &= ~BIT(index);
#ifdef CONFIG_TI_1PPS_DM_TIMER
		if (cpts->use_1pps_latch) {
			pinctrl_select_state(cpts->pins, cpts->pin_state_latch_off);
			cpts_latch_pps_stop(cpts);
		}
#endif
	}
	cpts_write32(cpts, v, control);

	mutex_unlock(&cpts->ptp_clk_mutex);

	return 0;
}

static int cpts_ptp_enable(struct ptp_clock_info *ptp,
			   struct ptp_clock_request *rq, int on)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);
#ifdef CONFIG_TI_1PPS_DM_TIMER
	struct timespec64 ts;
	s64 ns;
	bool ok;
#endif

	switch (rq->type) {
	case PTP_CLK_REQ_EXTTS:
#ifdef CONFIG_TI_1PPS_DM_TIMER
		dev_info(cpts->dev, "PTP_CLK_REQ_EXTTS: index = %d, on = %d\n", rq->extts.index, on);
#endif
		return cpts_extts_enable(cpts, rq->extts.index, on);
#ifdef CONFIG_TI_1PPS_DM_TIMER
	case PTP_CLK_REQ_PPS:
		if (cpts->use_1pps_gen) {
			ok = ptp_bc_clock_sync_enable(cpts->bc_clkid, on);
			if (!ok) {
				dev_err(cpts->dev, "cpts error: bc clk sync pps enable denied\n");
				return -EBUSY;
			}
			return cpts_pps_enable(cpts, on);
		} else	{
			return -EOPNOTSUPP;
		}

	case PTP_CLK_REQ_PEROUT:
		/* this enables a pps for external measurement */
		if (!cpts->use_1pps_ref)
			return -EOPNOTSUPP;

		if (rq->perout.index != 0)
			return -EINVAL;

		if (on) {
			ts.tv_sec = rq->perout.period.sec;
			ts.tv_nsec = rq->perout.period.nsec;
			ns = timespec64_to_ns(&ts);
			if (ns != NSEC_PER_SEC) {
				dev_err(cpts->dev, "Unsupported period %llu ns.Device supports only 1 sec period.\n",
					ns);
				return -EOPNOTSUPP;
			}
		}

		return cpts_ref_enable(cpts, on);
	case PTP_CLK_REQ_PPS_OFFSET:
		if (cpts->use_1pps_gen)
			cpts->pps_offset = on;
		return 0;
#endif
	default:
		break;
	}

	return -EOPNOTSUPP;
}

static bool cpts_match_tx_ts(struct cpts *cpts, struct cpts_event *event)
{
	struct sk_buff_head txq_list;
	struct sk_buff *skb, *tmp;
	unsigned long flags;
	bool found = false;
	u32 mtype_seqid;

	mtype_seqid = event->high &
		      ((MESSAGE_TYPE_MASK << MESSAGE_TYPE_SHIFT) |
		       (SEQUENCE_ID_MASK << SEQUENCE_ID_SHIFT) |
		       (EVENT_TYPE_MASK << EVENT_TYPE_SHIFT));

	__skb_queue_head_init(&txq_list);

	spin_lock_irqsave(&cpts->txq.lock, flags);
	skb_queue_splice_init(&cpts->txq, &txq_list);
	spin_unlock_irqrestore(&cpts->txq.lock, flags);

	skb_queue_walk_safe(&txq_list, skb, tmp) {
		struct skb_shared_hwtstamps ssh;
		struct cpts_skb_cb_data *skb_cb =
					(struct cpts_skb_cb_data *)skb->cb;

		if (mtype_seqid == skb_cb->skb_mtype_seqid) {
			memset(&ssh, 0, sizeof(ssh));
			ssh.hwtstamp = ns_to_ktime(event->timestamp);
			skb_tstamp_tx(skb, &ssh);
			found = true;
			__skb_unlink(skb, &txq_list);
			dev_consume_skb_any(skb);
			dev_dbg(cpts->dev, "match tx timestamp mtype_seqid %08x\n",
				mtype_seqid);
			break;
		}

		if (time_after(jiffies, skb_cb->tmo)) {
			/* timeout any expired skbs over 1s */
			dev_dbg(cpts->dev, "expiring tx timestamp from txq\n");
			__skb_unlink(skb, &txq_list);
			dev_consume_skb_any(skb);
		}
	}

	spin_lock_irqsave(&cpts->txq.lock, flags);
	skb_queue_splice(&txq_list, &cpts->txq);
	spin_unlock_irqrestore(&cpts->txq.lock, flags);

	return found;
}

static void cpts_process_events(struct cpts *cpts)
{
	struct list_head *this, *next;
	struct cpts_event *event;
	unsigned long flags;
	LIST_HEAD(events);
	LIST_HEAD(events_free);

	spin_lock_irqsave(&cpts->lock, flags);
	list_splice_init(&cpts->events, &events);
	spin_unlock_irqrestore(&cpts->lock, flags);

	list_for_each_safe(this, next, &events) {
		event = list_entry(this, struct cpts_event, list);
		if (cpts_match_tx_ts(cpts, event) ||
		    time_after(jiffies, event->tmo)) {
			list_del_init(&event->list);
			list_add(&event->list, &events_free);
		}
	}

	spin_lock_irqsave(&cpts->lock, flags);
	list_splice_tail(&events, &cpts->events);
	list_splice_tail(&events_free, &cpts->pool);
	spin_unlock_irqrestore(&cpts->lock, flags);
}

static long cpts_overflow_check(struct ptp_clock_info *ptp)
{
	struct cpts *cpts = container_of(ptp, struct cpts, info);
	unsigned long delay = cpts->ov_check_period;
	unsigned long flags;
	u64 ns;

	mutex_lock(&cpts->ptp_clk_mutex);

	cpts_update_cur_time(cpts, -1, NULL);
	ns = timecounter_read(&cpts->tc);

	cpts_process_events(cpts);

	spin_lock_irqsave(&cpts->txq.lock, flags);
	if (!skb_queue_empty(&cpts->txq)) {
		cpts_purge_txq(cpts);
		if (!skb_queue_empty(&cpts->txq))
			delay = CPTS_SKB_TX_WORK_TIMEOUT;
	}
	spin_unlock_irqrestore(&cpts->txq.lock, flags);

	dev_dbg(cpts->dev, "cpts overflow check at %lld\n", ns);
	mutex_unlock(&cpts->ptp_clk_mutex);
	return (long)delay;
}

static const struct ptp_clock_info cpts_info = {
	.owner		= THIS_MODULE,
	.name		= "CTPS timer",
	.max_adj	= 1000000,
	.n_ext_ts	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfreq	= cpts_ptp_adjfreq,
	.adjtime	= cpts_ptp_adjtime,
	.gettimex64	= cpts_ptp_gettimeex,
	.settime64	= cpts_ptp_settime,
	.enable		= cpts_ptp_enable,
	.do_aux_work	= cpts_overflow_check,
};

static int cpts_skb_get_mtype_seqid(struct sk_buff *skb, u32 *mtype_seqid)
{
	unsigned int ptp_class = ptp_classify_raw(skb);
	u8 *msgtype, *data = skb->data;
	unsigned int offset = 0;
	u16 *seqid;

	if (ptp_class == PTP_CLASS_NONE)
		return 0;

	if (ptp_class & PTP_CLASS_VLAN)
		offset += VLAN_HLEN;

	switch (ptp_class & PTP_CLASS_PMASK) {
	case PTP_CLASS_IPV4:
		offset += ETH_HLEN + IPV4_HLEN(data + offset) + UDP_HLEN;
		break;
	case PTP_CLASS_IPV6:
		offset += ETH_HLEN + IP6_HLEN + UDP_HLEN;
		break;
	case PTP_CLASS_L2:
		offset += ETH_HLEN;
		break;
	default:
		return 0;
	}

	if (skb->len + ETH_HLEN < offset + OFF_PTP_SEQUENCE_ID + sizeof(*seqid))
		return 0;

	if (unlikely(ptp_class & PTP_CLASS_V1))
		msgtype = data + offset + OFF_PTP_CONTROL;
	else
		msgtype = data + offset;

	seqid = (u16 *)(data + offset + OFF_PTP_SEQUENCE_ID);
	*mtype_seqid = (*msgtype & MESSAGE_TYPE_MASK) << MESSAGE_TYPE_SHIFT;
	*mtype_seqid |= (ntohs(*seqid) & SEQUENCE_ID_MASK) << SEQUENCE_ID_SHIFT;

	return 1;
}

static u64 cpts_find_ts(struct cpts *cpts, struct sk_buff *skb,
			int ev_type, u32 skb_mtype_seqid)
{
	struct list_head *this, *next;
	struct cpts_event *event;
	unsigned long flags;
	u32 mtype_seqid;
	u64 ns = 0;

	cpts_fifo_read(cpts, -1);
	spin_lock_irqsave(&cpts->lock, flags);
	list_for_each_safe(this, next, &cpts->events) {
		event = list_entry(this, struct cpts_event, list);
		if (event_expired(event)) {
			list_del_init(&event->list);
			list_add(&event->list, &cpts->pool);
			continue;
		}

		mtype_seqid = event->high &
			      ((MESSAGE_TYPE_MASK << MESSAGE_TYPE_SHIFT) |
			       (SEQUENCE_ID_MASK << SEQUENCE_ID_SHIFT) |
			       (EVENT_TYPE_MASK << EVENT_TYPE_SHIFT));

		if (mtype_seqid == skb_mtype_seqid) {
			ns = event->timestamp;
			list_del_init(&event->list);
			list_add(&event->list, &cpts->pool);
			break;
		}
	}
	spin_unlock_irqrestore(&cpts->lock, flags);

	return ns;
}

void cpts_rx_timestamp(struct cpts *cpts, struct sk_buff *skb)
{
	struct cpts_skb_cb_data *skb_cb = (struct cpts_skb_cb_data *)skb->cb;
	struct skb_shared_hwtstamps *ssh;
	int ret;
	u64 ns;

	ret = cpts_skb_get_mtype_seqid(skb, &skb_cb->skb_mtype_seqid);
	if (!ret)
		return;

	skb_cb->skb_mtype_seqid |= (CPTS_EV_RX << EVENT_TYPE_SHIFT);

	dev_dbg(cpts->dev, "%s mtype seqid %08x\n",
		__func__, skb_cb->skb_mtype_seqid);

	ns = cpts_find_ts(cpts, skb, CPTS_EV_RX, skb_cb->skb_mtype_seqid);
	if (!ns)
		return;
	ssh = skb_hwtstamps(skb);
	memset(ssh, 0, sizeof(*ssh));
	ssh->hwtstamp = ns_to_ktime(ns);
}
EXPORT_SYMBOL_GPL(cpts_rx_timestamp);

void cpts_tx_timestamp(struct cpts *cpts, struct sk_buff *skb)
{
	struct cpts_skb_cb_data *skb_cb = (struct cpts_skb_cb_data *)skb->cb;
	int ret;

	if (!(skb_shinfo(skb)->tx_flags & SKBTX_IN_PROGRESS))
		return;

	ret = cpts_skb_get_mtype_seqid(skb, &skb_cb->skb_mtype_seqid);
	if (!ret)
		return;

	skb_cb->skb_mtype_seqid |= (CPTS_EV_TX << EVENT_TYPE_SHIFT);

	dev_dbg(cpts->dev, "%s mtype seqid %08x\n",
		__func__, skb_cb->skb_mtype_seqid);

	/* Always defer TX TS processing to PTP worker */
	skb_get(skb);
	/* get the timestamp for timeouts */
	skb_cb->tmo = jiffies + msecs_to_jiffies(CPTS_SKB_RX_TX_TMO);
	skb_queue_tail(&cpts->txq, skb);
	ptp_schedule_worker(cpts->clock, 0);
}
EXPORT_SYMBOL_GPL(cpts_tx_timestamp);

#ifdef CONFIG_TI_1PPS_DM_TIMER
static u32 cpts_hw_ts_push_en[4] = {HW1_TS_PUSH_EN,
				    HW2_TS_PUSH_EN,
				    HW3_TS_PUSH_EN,
				    HW4_TS_PUSH_EN};
#endif

int cpts_register(struct cpts *cpts)
{
	int err, i;

	skb_queue_head_init(&cpts->txq);
	INIT_LIST_HEAD(&cpts->events);
	INIT_LIST_HEAD(&cpts->pool);
	for (i = 0; i < CPTS_MAX_EVENTS; i++)
		list_add(&cpts->pool_data[i].list, &cpts->pool);

	INIT_LIST_HEAD(&cpts->events_pps);

	clk_enable(cpts->refclk);

	cpts_write32(cpts, CPTS_EN, control);
	cpts_write32(cpts, TS_PEND_EN, int_enable);

	timecounter_init(&cpts->tc, &cpts->cc, ktime_to_ns(ktime_get_real()));

	cpts->clock = ptp_clock_register(&cpts->info, cpts->dev);
	if (IS_ERR(cpts->clock)) {
		err = PTR_ERR(cpts->clock);
		cpts->clock = NULL;
		goto err_ptp;
	}
	cpts->phc_index = ptp_clock_index(cpts->clock);

	ptp_schedule_worker(cpts->clock, cpts->ov_check_period);
#ifdef CONFIG_TI_1PPS_DM_TIMER

	if (cpts->use_1pps_gen) {
		cpts->odt_ops->enable(cpts->odt);

		cpts->bc_clkid = ptp_bc_clock_register(PTP_BC_CLOCK_TYPE_GMAC);
		dev_info(cpts->dev, "cpts ptp bc clkid %d\n", cpts->bc_clkid);
		ptp_bc_mux_ctrl_register((void *)cpts, &cpts->bc_mux_lock, cpts_bc_mux_ctrl);
		cpts_write32(cpts, cpts_read32(cpts, control) |
			     cpts_hw_ts_push_en[cpts->pps_hw_index], control);

		/* initialize timer16 for 1pps generator */
		writel_relaxed(OMAP_TIMER_INT_OVERFLOW, cpts->odt->irq_ena);
		__omap_dm_timer_write(cpts->odt, OMAP_TIMER_WAKEUP_EN_REG,
				      OMAP_TIMER_INT_OVERFLOW, 0);
	}

	if (cpts->use_1pps_latch) {
		cpts->odt2_ops->enable(cpts->odt2);

		/* initialize timer15 for 1pps latch */
		cpts_latch_tmr_init(cpts);

		writel_relaxed(OMAP_TIMER_INT_CAPTURE, cpts->odt2->irq_ena);
		__omap_dm_timer_write(cpts->odt2, OMAP_TIMER_WAKEUP_EN_REG,
				      OMAP_TIMER_INT_CAPTURE, 0);
	}
#endif
	return 0;

err_ptp:
	clk_disable(cpts->refclk);
	return err;
}
EXPORT_SYMBOL_GPL(cpts_register);

void cpts_unregister(struct cpts *cpts)
{
	if (WARN_ON(!cpts->clock))
		return;

#ifdef CONFIG_TI_1PPS_DM_TIMER

	if (cpts->use_1pps_gen) {
		cpts_pps_stop(cpts);

		writel_relaxed(0, cpts->odt->irq_ena);
		__omap_dm_timer_write(cpts->odt, OMAP_TIMER_WAKEUP_EN_REG, 0, 0);

		kthread_cancel_delayed_work_sync(&cpts->pps_work);

		pinctrl_select_state(cpts->pins, cpts->pin_state_pwm_off);
		if (cpts->pps_enable_gpiod)
			gpiod_set_value(cpts->pps_enable_gpiod, 1);

		if (cpts->use_1pps_ref) {
			pinctrl_select_state(cpts->pins,
					     cpts->pin_state_ref_off);
			if (cpts->ref_enable_gpiod)
				gpiod_set_value(cpts->ref_enable_gpiod, 1);
		}

		ptp_bc_clock_unregister(cpts->bc_clkid);

		cpts->pps_enable = -1;
		cpts->ref_enable = -1;

		cpts->odt_ops->disable(cpts->odt);
	}

	if (cpts->use_1pps_latch) {
		cpts_latch_pps_stop(cpts);

		writel_relaxed(0, cpts->odt2->irq_ena);
		__omap_dm_timer_write(cpts->odt2, OMAP_TIMER_WAKEUP_EN_REG, 0, 0);

		pinctrl_select_state(cpts->pins,
				     cpts->pin_state_latch_off);

		cpts->odt2_ops->disable(cpts->odt2);
	}
#endif

	ptp_clock_unregister(cpts->clock);
	cpts->clock = NULL;
	cpts->phc_index = -1;

	cpts_write32(cpts, 0, int_enable);
	cpts_write32(cpts, 0, control);

	/* Drop all packet */
	skb_queue_purge(&cpts->txq);

	clk_disable(cpts->refclk);
}
EXPORT_SYMBOL_GPL(cpts_unregister);

static void cpts_calc_mult_shift(struct cpts *cpts)
{
	u64 frac, maxsec, ns;
	u32 freq;

	freq = clk_get_rate(cpts->refclk);

	/* Calc the maximum number of seconds which we can run before
	 * wrapping around.
	 */
	maxsec = cpts->cc.mask;
	do_div(maxsec, freq);
	/* limit conversation rate to 10 sec as higher values will produce
	 * too small mult factors and so reduce the conversion accuracy
	 */
	if (maxsec > 10)
		maxsec = 10;

	/* Calc overflow check period (maxsec / 2) */
	cpts->ov_check_period = (HZ * maxsec) / 2;
	dev_info(cpts->dev, "cpts: overflow check period %lu (jiffies)\n",
		 cpts->ov_check_period);

	if (cpts->cc.mult || cpts->cc.shift)
		return;

	clocks_calc_mult_shift(&cpts->cc.mult, &cpts->cc.shift,
			       freq, NSEC_PER_SEC, maxsec);

	frac = 0;
	ns = cyclecounter_cyc2ns(&cpts->cc, freq, cpts->cc.mask, &frac);

	dev_info(cpts->dev,
		 "CPTS: ref_clk_freq:%u calc_mult:%u calc_shift:%u error:%lld nsec/sec\n",
		 freq, cpts->cc.mult, cpts->cc.shift, (ns - NSEC_PER_SEC));
}

static int cpts_of_mux_clk_setup(struct cpts *cpts, struct device_node *node)
{
	struct device_node *refclk_np;
	const char **parent_names;
	unsigned int num_parents;
	struct clk_hw *clk_hw;
	int ret = -EINVAL;
	u32 *mux_table;

	refclk_np = of_get_child_by_name(node, "cpts-refclk-mux");
	if (!refclk_np)
		/* refclk selection supported not for all SoCs */
		return 0;

	num_parents = of_clk_get_parent_count(refclk_np);
	if (num_parents < 1) {
		dev_err(cpts->dev, "mux-clock %s must have parents\n",
			refclk_np->name);
		goto mux_fail;
	}

	parent_names = devm_kzalloc(cpts->dev, (sizeof(char *) * num_parents),
				    GFP_KERNEL);

	mux_table = devm_kzalloc(cpts->dev, sizeof(*mux_table) * num_parents,
				 GFP_KERNEL);
	if (!mux_table || !parent_names) {
		ret = -ENOMEM;
		goto mux_fail;
	}

	of_clk_parent_fill(refclk_np, parent_names, num_parents);

	ret = of_property_read_variable_u32_array(refclk_np, "ti,mux-tbl",
						  mux_table,
						  num_parents, num_parents);
	if (ret < 0)
		goto mux_fail;

	clk_hw = clk_hw_register_mux_table(cpts->dev, refclk_np->name,
					   parent_names, num_parents,
					   0,
					   &cpts->reg->rftclk_sel, 0, 0x1F,
					   0, mux_table, NULL);
	if (IS_ERR(clk_hw)) {
		ret = PTR_ERR(clk_hw);
		goto mux_fail;
	}

	ret = devm_add_action_or_reset(cpts->dev,
				       (void(*)(void *))clk_hw_unregister_mux,
				       clk_hw);
	if (ret) {
		dev_err(cpts->dev, "add clkmux unreg action %d", ret);
		goto mux_fail;
	}

	ret = of_clk_add_hw_provider(refclk_np, of_clk_hw_simple_get, clk_hw);
	if (ret)
		goto mux_fail;

	ret = devm_add_action_or_reset(cpts->dev,
				       (void(*)(void *))of_clk_del_provider,
				       refclk_np);
	if (ret) {
		dev_err(cpts->dev, "add clkmux provider unreg action %d", ret);
		goto mux_fail;
	}

	return ret;

mux_fail:
	of_node_put(refclk_np);
	return ret;
}

#ifdef CONFIG_TI_1PPS_DM_TIMER
static int cpts_of_1pps_parse(struct cpts *cpts, struct device_node *node)
{
	struct device_node *np = NULL;
	struct platform_device *timer_pdev;
	struct dmtimer_platform_data *timer_pdata;
	u32 prop;

	np = of_parse_phandle(node, "pps_timer", 0);
	if (!np) {
		dev_dbg(cpts->dev,
			"device node lookup for pps timer failed\n");
		return -ENXIO;
	}

	timer_pdev = of_find_device_by_node(np);
	if (!timer_pdev) {
		dev_err(cpts->dev, "Unable to find Timer pdev\n");
		return -ENODEV;
	}

	timer_pdata = dev_get_platdata(&timer_pdev->dev);
	if (!timer_pdata) {
		dev_dbg(cpts->dev,
			 "dmtimer pdata structure NULL, deferring probe\n");
		return -EPROBE_DEFER;
	}

	cpts->odt_ops = timer_pdata->timer_ops;

	cpts->odt = cpts->odt_ops->request_by_node(np);
	if (!cpts->odt)
		return -EPROBE_DEFER;

	if (IS_ERR(cpts->odt)) {
		dev_err(cpts->dev, "request for 1pps DM timer failed: %ld\n",
			PTR_ERR(cpts->odt));
		return PTR_ERR(cpts->odt);
	}

	cpts->pps_tmr_irqn = of_irq_get(np, 0);
	if (cpts->pps_tmr_irqn <= 0) {
		dev_err(cpts->dev, "cannot get 1pps timer interrupt number\n");
		if (!cpts->pps_tmr_irqn)
			cpts->pps_tmr_irqn = -ENXIO;
		return cpts->pps_tmr_irqn;
	}

	cpts->pins = devm_pinctrl_get(cpts->dev);
	if (IS_ERR(cpts->pins)) {
		dev_err(cpts->dev, "request for 1pps pins failed: %ld\n",
			PTR_ERR(cpts->pins));
		return PTR_ERR(cpts->pins);
	}

	cpts->pin_state_pwm_on = pinctrl_lookup_state(cpts->pins, "pwm_on");
	if (IS_ERR(cpts->pin_state_pwm_on)) {
		dev_err(cpts->dev, "lookup for pwm_on pin state failed: %ld\n",
			PTR_ERR(cpts->pin_state_pwm_on));
		return PTR_ERR(cpts->pin_state_pwm_on);
	}

	cpts->pin_state_pwm_off = pinctrl_lookup_state(cpts->pins, "pwm_off");
	if (IS_ERR(cpts->pin_state_pwm_off)) {
		dev_err(cpts->dev, "lookup for pwm_off pin state failed: %ld\n",
			PTR_ERR(cpts->pin_state_pwm_off));
		return PTR_ERR(cpts->pin_state_pwm_off);
	}

	/* The 1PPS enable-gpio signal is only optional and therefore it
	 * may not be provided by DTB.
	 */
	cpts->pps_enable_gpiod = devm_gpiod_get_optional(cpts->dev,
							 "pps-enable",
							 GPIOD_OUT_HIGH);
	if (IS_ERR(cpts->pps_enable_gpiod))
		return PTR_ERR(cpts->pps_enable_gpiod);

	if (!cpts->pps_enable_gpiod)
		dev_err(cpts->dev, "pps_enable_gpiod fail\n");

	if (!of_property_read_u32(node, "cpts_pps_hw_event_index", &prop))
		cpts->pps_hw_index = prop;
	else
		cpts->pps_hw_index = CPTS_PPS_HW_INDEX;

	if (cpts->pps_hw_index > CPTS_PPS_HW_INDEX)
		cpts->pps_hw_index = CPTS_PPS_HW_INDEX;

	dev_info(cpts->dev, "cpts pps hw event index = %d\n",
		 cpts->pps_hw_index);

	cpts->use_1pps_gen = true;

	/* The 1PPS reference signal is only optional and therefore the
	 * corresponding pins may not be provided by DTB.
	 */

	cpts->pin_state_ref_on = pinctrl_lookup_state(cpts->pins,
						      "ref_on");
	if (IS_ERR(cpts->pin_state_ref_on)) {
		dev_notice(cpts->dev,
			   "lookup for ref_on pin state failed: %ld\n",
			   PTR_ERR(cpts->pin_state_ref_on));
		return PTR_ERR(cpts->pin_state_ref_on);
	} else {

		cpts->pin_state_ref_off = pinctrl_lookup_state(cpts->pins,
							       "ref_off");
		if (IS_ERR(cpts->pin_state_ref_off)) {
			dev_err(cpts->dev,
				"lookup for ref_off pin state failed: %ld\n",
				PTR_ERR(cpts->pin_state_ref_off));
			return PTR_ERR(cpts->pin_state_ref_off);
		}

		/* The 1PPS ref-enable-gpio signal is only optional and
		 * therefore it	 may not be provided by DTB.
		 */
		cpts->ref_enable_gpiod = devm_gpiod_get_optional(cpts->dev,
								 "ref-enable",
								 GPIOD_OUT_HIGH);
		if (IS_ERR(cpts->ref_enable_gpiod))
			return PTR_ERR(cpts->ref_enable_gpiod);

		if (!cpts->ref_enable_gpiod)
			dev_err(cpts->dev, "ref_enable_gpiod fail\n");

		cpts->use_1pps_ref = true;
	}

	return 0;
}

static int cpts_of_1pps_latch_parse(struct cpts *cpts, struct device_node *node)
{
	struct device_node *np2 = NULL;
	struct platform_device *timer_pdev;
	struct dmtimer_platform_data *timer_pdata;

	np2 = of_parse_phandle(node, "latch_timer", 0);
	if (!np2) {
		dev_dbg(cpts->dev,
			"device node lookup for latch timer input failed\n");
		return -ENXIO;
	}

	timer_pdev = of_find_device_by_node(np2);
	if (!timer_pdev) {
		dev_err(cpts->dev, "Unable to find Timer pdev\n");
		return -ENODEV;
	}

	timer_pdata = dev_get_platdata(&timer_pdev->dev);
	if (!timer_pdata) {
		dev_dbg(cpts->dev,
			 "dmtimer pdata structure NULL, deferring probe\n");
		return -EPROBE_DEFER;
	}

	cpts->odt2_ops = timer_pdata->timer_ops;

	cpts->odt2 = cpts->odt2_ops->request_by_node(np2);
	if (!cpts->odt2)
		return -EPROBE_DEFER;

	if (IS_ERR(cpts->odt2)) {
		dev_err(cpts->dev,
			"request for 1pps latch timer input failed: %ld\n",
			PTR_ERR(cpts->odt2));
		return PTR_ERR(cpts->odt2);
	}

	cpts->pps_latch_irqn = of_irq_get(np2, 0);
	if (cpts->pps_latch_irqn <= 0) {
		dev_err(cpts->dev, "cannot get 1pps latch interrupt number\n");
		if (!cpts->pps_latch_irqn)
			cpts->pps_latch_irqn = -ENXIO;
		return cpts->pps_latch_irqn;
	}

	cpts->pins = devm_pinctrl_get(cpts->dev);
	if (IS_ERR(cpts->pins)) {
		dev_err(cpts->dev, "request for 1pps pins failed: %ld\n",
			PTR_ERR(cpts->pins));
		return PTR_ERR(cpts->pins);
	}

	cpts->pin_state_latch_on = pinctrl_lookup_state(cpts->pins,
							"latch_on");
	if (IS_ERR(cpts->pin_state_latch_on)) {
		dev_err(cpts->dev, "lookup for latch_on pin state failed: %ld\n",
			PTR_ERR(cpts->pin_state_latch_on));
		return PTR_ERR(cpts->pin_state_latch_on);
	}

	cpts->pin_state_latch_off = pinctrl_lookup_state(cpts->pins,
							 "latch_off");
	if (IS_ERR(cpts->pin_state_latch_off)) {
		dev_err(cpts->dev, "lookup for latch_off pin state failed: %ld\n",
			PTR_ERR(cpts->pin_state_latch_off));
		return PTR_ERR(cpts->pin_state_latch_off);
	}

	return 0;
}
#endif

static int cpts_of_parse(struct cpts *cpts, struct device_node *node)
{
	int ret = -EINVAL;
	u32 prop;

	if (!of_property_read_u32(node, "cpts_clock_mult", &prop))
		cpts->cc.mult = prop;

	if (!of_property_read_u32(node, "cpts_clock_shift", &prop))
		cpts->cc.shift = prop;

	if ((cpts->cc.mult && !cpts->cc.shift) ||
	    (!cpts->cc.mult && cpts->cc.shift))
		goto of_error;

	if (!of_property_read_u32(node, "cpts-ext-ts-inputs", &prop))
		cpts->ext_ts_inputs = prop;

#ifdef CONFIG_TI_1PPS_DM_TIMER
	/* get timer for 1PPS */
	ret = cpts_of_1pps_parse(cpts, node);
	if (ret == -EPROBE_DEFER)
		return ret;

	ret = cpts_of_1pps_latch_parse(cpts, node);
	if (ret == -EPROBE_DEFER)
		return ret;

	cpts->use_1pps_latch = (ret == 0);
#endif

	return cpts_of_mux_clk_setup(cpts, node);

of_error:
	dev_err(cpts->dev, "CPTS: Missing property in the DT.\n");
	return ret;
}

struct cpts *cpts_create(struct device *dev, void __iomem *regs,
			 struct device_node *node)
{
	struct cpts *cpts;
	int ret;

	cpts = devm_kzalloc(dev, sizeof(*cpts), GFP_KERNEL);
	if (!cpts)
		return ERR_PTR(-ENOMEM);

	cpts->dev = dev;
	cpts->reg = (struct cpsw_cpts __iomem *)regs;
	cpts->irq_poll = true;
	spin_lock_init(&cpts->lock);
	mutex_init(&cpts->ptp_clk_mutex);
	init_completion(&cpts->ts_push_complete);

	ret = cpts_of_parse(cpts, node);
	if (ret)
		return ERR_PTR(ret);

	cpts->refclk = devm_get_clk_from_child(dev, node, "cpts");
	if (IS_ERR(cpts->refclk))
		/* try get clk from dev node for compatibility */
		cpts->refclk = devm_clk_get(dev, "cpts");

	if (IS_ERR(cpts->refclk)) {
		dev_err(dev, "Failed to get cpts refclk %ld\n",
			PTR_ERR(cpts->refclk));
		return ERR_CAST(cpts->refclk);
	}

	ret = clk_prepare(cpts->refclk);
	if (ret)
		return ERR_PTR(ret);

	cpts->cc.read = cpts_systim_read;
	cpts->cc.mask = CLOCKSOURCE_MASK(32);
	cpts->info = cpts_info;
	cpts->phc_index = -1;

	if (cpts->ext_ts_inputs)
		cpts->info.n_ext_ts = cpts->ext_ts_inputs;

	cpts_calc_mult_shift(cpts);
	/* save cc.mult original value as it can be modified
	 * by cpts_ptp_adjfreq().
	 */
	cpts->cc_mult = cpts->cc.mult;

#ifdef CONFIG_TI_1PPS_DM_TIMER
	spin_lock_init(&cpts->bc_mux_lock);
	kthread_init_delayed_work(&cpts->pps_work, cpts_pps_kworker);
	cpts->pps_kworker = kthread_create_worker(0, "pps0");

	if (IS_ERR(cpts->pps_kworker)) {
		ret = PTR_ERR(cpts->pps_kworker);
		dev_err(cpts->dev,
			"failed to create cpts pps worker %d\n", ret);
		return ERR_PTR(ret);
	}

	cpts->pps_enable = -1;
	cpts->ref_enable = -1;
	cpts->pps_offset = 0;

	if (cpts->use_1pps_gen) {
		ret = devm_request_irq(dev, cpts->pps_tmr_irqn,
				       cpts_1pps_tmr_interrupt,
				       0, "1pps_timer", cpts);
		if (ret < 0) {
			dev_err(dev, "unable to request 1pps timer IRQ %d (%d)\n",
				cpts->pps_tmr_irqn, ret);
			return ERR_PTR(ret);
		}

		pinctrl_select_state(cpts->pins, cpts->pin_state_pwm_off);
		if (cpts->use_1pps_ref)
			pinctrl_select_state(cpts->pins,
					     cpts->pin_state_ref_off);
	}

	if (cpts->use_1pps_latch) {
		ret = devm_request_irq(dev, cpts->pps_latch_irqn,
				       cpts_1pps_latch_interrupt,
				       0, "1pps_latch", cpts);
		if (ret < 0) {
			dev_err(dev, "unable to request 1pps latch IRQ %d (%d)\n",
				cpts->pps_latch_irqn, ret);
			return ERR_PTR(ret);
		}

		pinctrl_select_state(cpts->pins, cpts->pin_state_latch_off);
	}

	/* Enable 1PPS related features	*/
	cpts->info.pps		= (cpts->use_1pps_gen) ? 1 : 0;
	cpts->info.n_ext_ts	= (cpts->use_1pps_gen) ?
				  CPTS_MAX_EXT_TS - 1 : CPTS_MAX_EXT_TS;
	cpts->info.n_per_out	= (cpts->use_1pps_ref) ? 1 : 0;
#endif

	return cpts;
}
EXPORT_SYMBOL_GPL(cpts_create);

void cpts_release(struct cpts *cpts)
{
	if (!cpts)
		return;

#ifdef CONFIG_TI_1PPS_DM_TIMER
	if (cpts->use_1pps_gen) {
		pinctrl_select_state(cpts->pins, cpts->pin_state_pwm_off);
		if (cpts->use_1pps_ref)
			pinctrl_select_state(cpts->pins,
					     cpts->pin_state_ref_off);

		cpts->odt_ops->free(cpts->odt);
	};

	if (cpts->use_1pps_latch) {
		pinctrl_select_state(cpts->pins, cpts->pin_state_latch_off);

		cpts->odt2_ops->free(cpts->odt2);
	}

	kthread_destroy_worker(cpts->pps_kworker);
#endif

	if (WARN_ON(!cpts->refclk))
		return;

	clk_unprepare(cpts->refclk);
}
EXPORT_SYMBOL_GPL(cpts_release);

#ifdef CONFIG_TI_1PPS_DM_TIMER
/* This function will be invoked by the PTP BC module in a pair
 * to disable and then re-enable the BC pps clock multiplexer
 * only if it is enabled during the initial call (enable = 0).
 * And the spin lock bc_mux_lock should be invoked to protect
 * the entire procedure.
 */
static void cpts_bc_mux_ctrl(void *ctx, int enable)
{
	struct cpts *cpts = (struct cpts *)ctx;

	if (!cpts->pps_enable_gpiod)
		return;

	if (enable)
		gpiod_set_value(cpts->pps_enable_gpiod, 0);
	else
		gpiod_set_value(cpts->pps_enable_gpiod, 1);
}

enum cpts_1pps_state {
	/* Initial state: try to SYNC to the CPTS timestamp */
	INIT = 0,
	/* Sync State: track the clock drift, trigger timer
	 * adjustment when the clock drift exceed 1 clock
	 * boundary declare out of sync if the clock difference is more
	 * than a 1ms
	 */
	SYNC = 1,
	/* Adjust state: Wait for time adjust to take effect at the
	 * timer reload time
	 */
	ADJUST = 2,
	/* Wait state: PTP timestamp has been verified,
	 * wait for next check period
	 */
	WAIT = 3,
	/* NonAdjust state: There is too much frequency difference,
	 * No more timing adjustment to get into the latch window
	 */
	NONADJUST = 4,
};

static void cpts_tmr_reinit(struct cpts *cpts)
{
	/* re-initialize timer16 for 1pps generator */
	WRITE_TCLR(cpts->odt, 0);
	WRITE_TLDR(cpts->odt, CPTS_TMR_RELOAD_CNT);
	WRITE_TCRR(cpts->odt, CPTS_TMR_RELOAD_CNT);
	WRITE_TMAR(cpts->odt, CPTS_TMR_CMP_CNT);       /* 10 ms */
	WRITE_TCLR(cpts->odt, BIT(12) | 2 << 10 | BIT(6) | BIT(1));
	WRITE_TSICR(cpts->odt, BIT(2));

	cpts->pps_state = INIT;
}

static void cpts_latch_tmr_init(struct cpts *cpts)
{
	/* re-initialize timer16 for 1pps generator */
	WRITE_TCLR(cpts->odt2, 0);
	WRITE_TLDR(cpts->odt2, CPTS_LATCH_TMR_RELOAD_CNT);
	WRITE_TCRR(cpts->odt2, CPTS_LATCH_TMR_RELOAD_CNT);
	WRITE_TMAR(cpts->odt2, CPTS_LATCH_TMR_CMP_CNT);       /* 10 ms */
	WRITE_TCLR(cpts->odt2, BIT(14) | BIT(12) | BIT(8) | BIT(6) | BIT(1) |
		   BIT(0));
	WRITE_TSICR(cpts->odt2, BIT(2));

	cpts->pps_latch_state = INIT;
	cpts->pps_latch_offset = 0;
}

static inline void cpts_turn_on_off_1pps_output(struct cpts *cpts, u64 ts)
{
	if (ts > (900000000 + CPTS_DEFAULT_PPS_WIDTH_NS)) {
		if (cpts->pps_enable == 1) {
			pinctrl_select_state(cpts->pins,
					     cpts->pin_state_pwm_on);
			if (cpts->pps_enable_gpiod) {
				spin_lock_bh(&cpts->bc_mux_lock);
				gpiod_set_value(cpts->pps_enable_gpiod, 0);
				spin_unlock_bh(&cpts->bc_mux_lock);
			}
		}

		if (cpts->ref_enable == 1) {
			pinctrl_select_state(cpts->pins,
					     cpts->pin_state_ref_on);
			if (cpts->ref_enable_gpiod)
				gpiod_set_value(cpts->ref_enable_gpiod, 0);
		}

		dev_dbg(cpts->dev, "1pps on at %llu cpts->hw_timestamp %llu\n",
			ts, cpts->hw_timestamp);
	} else if ((ts < 100000000) && (ts >= CPTS_DEFAULT_PPS_WIDTH_NS)) {
		if (cpts->pps_enable == 1) {
			pinctrl_select_state(cpts->pins,
					     cpts->pin_state_pwm_off);
			if (cpts->pps_enable_gpiod) {
				spin_lock_bh(&cpts->bc_mux_lock);
				gpiod_set_value(cpts->pps_enable_gpiod, 1);
				spin_unlock_bh(&cpts->bc_mux_lock);
			}
		}

		if (cpts->ref_enable == 1) {
			pinctrl_select_state(cpts->pins,
					     cpts->pin_state_ref_off);
			if (cpts->ref_enable_gpiod)
				gpiod_set_value(cpts->ref_enable_gpiod, 1);
		}
		dev_dbg(cpts->dev, "1pps off at %llu cpts->hw_timestamp %llu\n",
			ts, cpts->hw_timestamp);
	}
}

/* The reload counter value is going to affect all cycles after the next SYNC
 * check. Therefore, we need to change the next expected drift value by
 * updating the ts_correct value
 */
static void update_ts_correct(void)
{
	if (tmr_reload_cnt > tmr_reload_cnt_prev)
		ts_correct -= (tmr_reload_cnt - tmr_reload_cnt_prev) *
			      CPTS_TMR_CLK_PERIOD;
	else
		ts_correct += (tmr_reload_cnt_prev - tmr_reload_cnt) *
			      CPTS_TMR_CLK_PERIOD;
}

static void cpts_tmr_poll(struct cpts *cpts, bool cpts_poll)
{
	u32 tmr_count, tmr_count2, count_exp, tmr_diff_abs;
	s32 tmr_diff = 0;
	int ts_val;
	static int ts_val_prev;
	u64 cpts_ts_short, cpts_ts, tmp64;
	static u64 cpts_ts_trans;
	bool updated = false;
	static bool first;
	unsigned long flags;


	reinit_completion(&cpts->ts_push_complete);

	/* use spin_lock_irqsave() here as it has to run very fast */
	spin_lock_irqsave(&cpts->lock, flags);
	tmr_count = READ_TCRR(cpts->odt);
	cpts_write32(cpts, TS_PUSH, ts_push);
	cpts_read32(cpts, ts_push);
	tmr_count2 = READ_TCRR(cpts->odt);
	spin_unlock_irqrestore(&cpts->lock, flags);

	if (cpts->irq_poll && cpts_fifo_read(cpts, CPTS_EV_PUSH))
		dev_err(cpts->dev, "cpts: unable to obtain a time stamp\n");

	if (!cpts->irq_poll &&
	    !wait_for_completion_timeout(&cpts->ts_push_complete, HZ))
		dev_err(cpts->dev, "cpts: obtain a time stamp timeout\n");

	tmp64 = timecounter_read(&cpts->tc);
	cpts_ts = tmp64;
	cpts_ts_short = do_div(tmp64, 1000000000UL);



	cpts_turn_on_off_1pps_output(cpts, cpts_ts_short);

	tmp64 = cpts_ts;
	cpts_ts_short = do_div(tmp64, 100000000UL);

	dev_dbg(cpts->dev, "%s : tmr_cnt2=%u, cpts_ts=%llu, state = %d\n",
		__func__, tmr_count2, cpts_ts, cpts->pps_state);

	if (cpts->ptp_adjusted) {
		cpts->pps_state = INIT;
		cpts->ptp_adjusted = false;
	}

	/* Timer poll state machine */
	switch (cpts->pps_state) {
	case INIT:
		if (cpts_ts_short < CPTS_TS_THRESH &&
		    ((tmr_count2 - tmr_count) <
		     CPTS_MAX_MMR_ACCESS_TIME / CPTS_TMR_CLK_PERIOD)) {
			/* The nominal delay of this operation about 9 ticks
			 * We are able to compensate for the normal range 8-17
			 * However, the simple compensation fials when the delay
			 * is getting big, just skip this sample
			 *
			 * Calculate the expected tcrr value and update to it
			 */
			tmp64 = (100000000UL - cpts_ts_short) +
				cpts->pps_offset;
			do_div(tmp64, CPTS_TMR_CLK_PERIOD);
			count_exp = (u32)tmp64;
			count_exp = 0xFFFFFFFFUL - count_exp + 1;

			WRITE_TCRR(cpts->odt, count_exp +
				   READ_TCRR(cpts->odt) - tmr_count2 +
				   CPTS_NOM_MMR_ACCESS_TICK);

			{
				WRITE_TLDR(cpts->odt, tmr_reload_cnt);
				WRITE_TMAR(cpts->odt, CPTS_TMR_CMP_CNT);

				cpts->pps_state = WAIT;
				first = true;
				tmr_reload_cnt_prev = tmr_reload_cnt;
				cpts_ts_trans = (cpts_ts - cpts_ts_short) +
					100000000ULL;
				dev_info(cpts->dev, "%s: exit INIT state with pps_offset = %d\n"
					, __func__, cpts->pps_offset);
			}
		}
		break;

	case ADJUST:
		/* Wait for the ldr load to take effect */
		if (cpts_ts >= cpts_ts_trans) {
			u64 ts = cpts->hw_timestamp;
			u32 ts_offset;

			ts_offset = do_div(ts, 100000000UL);

			ts_val = (ts_offset >= 50000000UL) ?
				-(100000000UL - ts_offset) :
				(ts_offset);

			/* HW_EVENT offset range check:
			 * There might be large PPS offset change due to
			 * PTP time adjustment. Switch back to INIT state
			 */
			if (abs(ts_val) > 1000000UL) {
				cpts->pps_state = INIT;
				dev_info(cpts->dev, "%s: re-enter INIT state due to large offset %d\n",
					 __func__, ts_val);
				/* restore the timer period to 100ms */
				WRITE_TLDR(cpts->odt, tmr_reload_cnt);
				break;
			}

			ts_val -= cpts->pps_offset;

			/* restore the timer period to 100ms */
			WRITE_TLDR(cpts->odt, tmr_reload_cnt);

			if (tmr_reload_cnt != tmr_reload_cnt_prev)
				update_ts_correct();

			cpts_ts_trans += 100000000ULL;
			cpts->pps_state = WAIT;

			tmr_reload_cnt_prev = tmr_reload_cnt;
			ts_val_prev = ts_val;
		}
		break;

	case WAIT:
		/* Wait for the next poll period when the adjustment
		 * has been taken effect
		 */
		if (cpts_ts < cpts_ts_trans)
			break;

		cpts->pps_state = SYNC;

		fallthrough;
	case SYNC:
		{
			u64 ts = cpts->hw_timestamp;
			u32 ts_offset;
			int tsAdjust;

			ts_offset = do_div(ts, 100000000UL);
			ts_val = (ts_offset >= 50000000UL) ?
				-(100000000UL - ts_offset) :
				(ts_offset);

			/* HW_EVENT offset range check:
			 * There might be large PPS offset change due to
			 * PTP time adjustment. Switch back to INIT state
			 */
			if (abs(ts_val) > 1000000UL) {
				cpts->pps_state = INIT;
				dev_info(cpts->dev, "%s: re-enter INIT state due to large offset %d\n",
					 __func__, ts_val);
				break;
			}

			ts_val -= cpts->pps_offset;
			/* tsAjust should include the current error and the
			 * expected drift for the next two cycles
			 */
			if (first) {
				tsAdjust = ts_val;
				first = false;
			} else {
				tsAdjust = ts_val +
					(ts_val - ts_val_prev + ts_correct) * 2;
			}

			tmr_diff = (tsAdjust < 0) ?
				   (tsAdjust - CPTS_TMR_CLK_PERIOD / 2) /
				   CPTS_TMR_CLK_PERIOD :
				   (tsAdjust + CPTS_TMR_CLK_PERIOD / 2) /
				   CPTS_TMR_CLK_PERIOD;

			/* adjust for the error in the current cycle due to
			 * the old (incorrect) reload count we only make the
			 * adjustment if the counter change is more than 1
			 * because the couner will change back and forth
			 * at the frequency tick boundary
			 */
			if (tmr_reload_cnt != tmr_reload_cnt_prev) {
				if (tmr_reload_cnt > tmr_reload_cnt_prev)
					tmr_diff += (tmr_reload_cnt -
						     tmr_reload_cnt_prev - 1);
				else
					tmr_diff -= (tmr_reload_cnt_prev -
						     tmr_reload_cnt - 1);
			}

			dev_dbg(cpts->dev, "%s: ts_val = %d, ts_val_prev = %d\n",
				__func__, ts_val, ts_val_prev);

			ts_correct = tmr_diff * CPTS_TMR_CLK_PERIOD;
			ts_val_prev = ts_val;
			tmr_diff_abs = abs(tmr_diff);

			if (tmr_diff_abs ||
			    tmr_reload_cnt != tmr_reload_cnt_prev) {
				updated = true;
				if (tmr_diff_abs <
				    (1000000 / CPTS_TMR_CLK_PERIOD)) {
					/* adjust ldr time for one period
					 * instead of updating the tcrr directly
					 */
					WRITE_TLDR(cpts->odt, tmr_reload_cnt +
						   (u32)tmr_diff);
					cpts->pps_state = ADJUST;
				} else {
					/* The error is more than 1 ms,
					 * declare it is out of sync
					 */
					cpts->pps_state = INIT;
					dev_info(cpts->dev, "%s: enter INIT state\n",
						 __func__);
					break;
				}
			} else {
				cpts->pps_state = WAIT;
			}

			cpts_ts_trans = (cpts_ts - cpts_ts_short) +
					100000000ULL;
			tmr_reload_cnt_prev = tmr_reload_cnt;

			break;
		} /* case SYNC */

	} /* switch */

	if (updated)
		dev_dbg(cpts->dev, "%s (updated=%u): tmr_diff=%d, tmr_reload_cnt=%u, cpts_ts=%llu hw_timestamp=%llu\n",
			__func__, updated, tmr_diff, tmr_reload_cnt,
			cpts_ts, cpts->hw_timestamp);
}

static inline void cpts_latch_pps_stop(struct cpts *cpts)
{
	u32 v;

	/* disable timer PWM (TRIG = 0) */
	v = READ_TCLR(cpts->odt2);
	v &= ~BIT(11);
	WRITE_TCLR(cpts->odt2, v);

	cpts->pps_latch_state = INIT;
}

static inline void cpts_latch_pps_start(struct cpts *cpts)
{
	u32 v;

	/* enable timer PWM (TRIG = 2) */
	v = READ_TCLR(cpts->odt2);
	v |= BIT(11);
	WRITE_TCLR(cpts->odt2, v);
}

static void cpts_latch_proc(struct cpts *cpts, u32 latch_cnt)
{
	u32 offset = 0xFFFFFFFFUL - latch_cnt + 1;
	u32 reload_cnt = CPTS_LATCH_TMR_RELOAD_CNT;
	unsigned long flags;
	static bool skip;
	static int init_cnt;

	if (!cpts)
		return;

	spin_lock_irqsave(&cpts->lock, flags);
	cpts->pps_latch_offset = offset * CPTS_TMR_CLK_PERIOD +
				 CPTS_TMR_LATCH_DELAY;
	cpts->pps_latch_receive = true;
	spin_unlock_irqrestore(&cpts->lock, flags);

	/* Timer poll state machine */
	switch (cpts->pps_latch_state) {
	case INIT:
		if (!skip) {
			if (offset < CPTS_LATCH_TICK_THRESH_MIN) {
				reload_cnt -= (CPTS_LATCH_TICK_THRESH_MID -
					       offset);
			} else if (offset > CPTS_LATCH_TICK_THRESH_MAX) {
				reload_cnt += (offset -
					       CPTS_LATCH_TICK_THRESH_MID);
			} else {
				/* latch offset is within the range,
				 * enter SYNC state
				 */
				cpts_latch_pps_start(cpts);
				cpts->pps_latch_state = SYNC;
				init_cnt = 0;
				dev_info(cpts->dev, "%s: enter SYNC state\n",
					 __func__);
				break;
			}
			init_cnt++;
			skip = true;
		} else {
			skip = false;
			/* Check whether the latch offset is already within
			 * the range because the TLDR load may occur prior
			 * to the initial rollover
			 */
			if (offset >= CPTS_LATCH_TICK_THRESH_MIN &&
			    offset <= CPTS_LATCH_TICK_THRESH_MAX) {
				/* latch offset is within the range,
				 * enter SYNC state
				 */
				cpts_latch_pps_start(cpts);
				cpts->pps_latch_state = SYNC;
				init_cnt = 0;
			}
		}

		WRITE_TLDR(cpts->odt2, reload_cnt);

		if (!skip && init_cnt >= CPTS_LATCH_INIT_THRESH) {
			/* Multiple timing adjustment failures indicate that
			 * the frequency difference is larger than 25PPM,
			 * (out of scope) enter NONADJUST state where PPS event
			 * is relayed without timing window adjustment.
			 */
			cpts->pps_latch_state = NONADJUST;
			cpts_latch_pps_start(cpts);
			init_cnt = 0;
			dev_info(cpts->dev, "%s: enter NONADJUST state\n",
				 __func__);
		}

		if (cpts->pps_latch_state == SYNC)
			dev_info(cpts->dev, "%s: enter SYNC state\n", __func__);
		else
			dev_dbg(cpts->dev, "%s: offset = %u, latch_cnt = %u, reload_cnt =%u\n",
				__func__, offset * 10, latch_cnt, reload_cnt);
		break;

	case ADJUST:
		/* Restore the LDR value */
		WRITE_TLDR(cpts->odt2, reload_cnt);
		cpts->pps_latch_state = SYNC;
		break;

	case SYNC:
		{
			if (offset > CPTS_LATCH_TICK_THRESH_UNSYNC) {
				/* latch offset is well out of the range,
				 * enter INIT (Out of Sync) state
				 */
				cpts_latch_pps_stop(cpts);
				cpts->pps_latch_state = INIT;
				skip = false;
				dev_info(cpts->dev, "%s: re-enter INIT state due to large_offset %d\n",
					 __func__, offset);
				break;
			} else if (offset < CPTS_LATCH_TICK_THRESH_MIN) {
				reload_cnt -= (CPTS_LATCH_TICK_THRESH_MID -
					       offset);
			} else if (offset > CPTS_LATCH_TICK_THRESH_MAX) {
				reload_cnt += (offset -
					       CPTS_LATCH_TICK_THRESH_MID);
			} else {
				/* latch offset is within the range,
				 * no adjustment is required
				 */
				break;
			}

			cpts->pps_latch_state = ADJUST;
			WRITE_TLDR(cpts->odt2, reload_cnt);
			break;
		}

	case NONADJUST:
		break;

	default:
		/* Error handling */
		break;

	} /* switch */
	dev_dbg(cpts->dev, "%s(%d): offset = %u(0x%x)\n",
		__func__, cpts->pps_latch_state, offset, offset);
}

static int int_cnt;
static irqreturn_t cpts_1pps_tmr_interrupt(int irq, void *dev_id)
{
	struct cpts *cpts = (struct cpts *)dev_id;

	writel_relaxed(OMAP_TIMER_INT_OVERFLOW, cpts->odt->irq_stat);
	kthread_queue_delayed_work(cpts->pps_kworker, &cpts->pps_work,
				   msecs_to_jiffies(CPTS_DEFAULT_PPS_WIDTH_MS));

	if (int_cnt <= 1000)
		int_cnt++;
	if ((int_cnt % 100) == 0)
		dev_dbg(cpts->dev, "%s %d\n", __func__, int_cnt);

	return IRQ_HANDLED;
}

static int latch_cnt;
static irqreturn_t cpts_1pps_latch_interrupt(int irq, void *dev_id)
{
	struct cpts *cpts = (struct cpts *)dev_id;

	writel_relaxed(OMAP_TIMER_INT_CAPTURE, cpts->odt2->irq_stat);

	cpts_latch_proc(cpts, READ_TCAP(cpts->odt2));

	if (latch_cnt <= 100)
		latch_cnt++;
	if ((latch_cnt % 10) == 0)
		dev_dbg(cpts->dev, "%s %d\n", __func__, latch_cnt);

	return IRQ_HANDLED;
}
#endif

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI CPTS driver");
MODULE_AUTHOR("Richard Cochran <richardcochran@gmail.com>");

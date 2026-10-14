// SPDX-License-Identifier: GPL-2.0+
#include <linux/bitfield.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/crc32.h>
#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>

#include "mtk.h"

#define MTK_GPHY_ID_MT7530		0x03a29412
#define MTK_GPHY_ID_MT7531		0x03a29441

#define MTK_PHY_PAGE_EXTENDED_2			0x0002
#define MTK_PHY_PAGE_EXTENDED_3			0x0003
#define MTK_PHY_RG_LPI_PCS_DSP_CTRL_REG11	0x11

#define MTK_PHY_PAGE_EXTENDED_2A30		0x2a30

/* Registers on Token Ring debug nodes */
/* ch_addr = 0x1, node_addr = 0xf, data_addr = 0x17 */
#define SLAVE_DSP_READY_TIME_MASK		GENMASK(22, 15)

/* Registers on MDIO_MMD_VEND1 */
#define MTK_PHY_GBE_MODE_TX_DELAY_SEL		0x13
#define MTK_PHY_TEST_MODE_TX_DELAY_SEL		0x14
#define   MTK_TX_DELAY_PAIR_B_MASK		GENMASK(10, 8)
#define   MTK_TX_DELAY_PAIR_D_MASK		GENMASK(2, 0)

#define MTK_PHY_MCC_CTRL_AND_TX_POWER_CTRL	0xa6
#define   MTK_MCC_NEARECHO_OFFSET_MASK		GENMASK(15, 8)

#define MTK_PHY_RXADC_CTRL_RG7			0xc6
#define   MTK_PHY_DA_AD_BUF_BIAS_LP_MASK	GENMASK(9, 8)

#define MTK_PHY_RG_LPI_PCS_DSP_CTRL_REG123	0x123
#define   MTK_PHY_LPI_NORM_MSE_LO_THRESH100_MASK	GENMASK(15, 8)
#define   MTK_PHY_LPI_NORM_MSE_HI_THRESH100_MASK	GENMASK(7, 0)

static void mtk_gephy_config_init(struct phy_device *phydev)
{
	/* Enable HW auto downshift */
	phy_modify_paged(phydev, MTK_PHY_PAGE_EXTENDED_1,
			 MTK_PHY_AUX_CTRL_AND_STATUS,
			 0, MTK_PHY_ENABLE_DOWNSHIFT);

	/* Increase SlvDPSready time */
	mtk_tr_modify(phydev, 0x1, 0xf, 0x17, SLAVE_DSP_READY_TIME_MASK,
		      FIELD_PREP(SLAVE_DSP_READY_TIME_MASK, 0x5e));

	/* Adjust 100_mse_threshold */
	phy_modify_mmd(phydev, MDIO_MMD_VEND1,
		       MTK_PHY_RG_LPI_PCS_DSP_CTRL_REG123,
		       MTK_PHY_LPI_NORM_MSE_LO_THRESH100_MASK |
		       MTK_PHY_LPI_NORM_MSE_HI_THRESH100_MASK,
		       FIELD_PREP(MTK_PHY_LPI_NORM_MSE_LO_THRESH100_MASK,
				  0xff) |
		       FIELD_PREP(MTK_PHY_LPI_NORM_MSE_HI_THRESH100_MASK,
				  0xff));

	/* If echo time is narrower than 0x3, it will be regarded as noise */
	phy_modify_mmd(phydev, MDIO_MMD_VEND1,
		       MTK_PHY_MCC_CTRL_AND_TX_POWER_CTRL,
		       MTK_MCC_NEARECHO_OFFSET_MASK,
		       FIELD_PREP(MTK_MCC_NEARECHO_OFFSET_MASK, 0x3));
}

static int mt7530_phy_probe(struct phy_device *phydev)
{
	/* The MT7530 internal GE PHY has broken EEE: with EEE advertised,
	 * some link partners fail to establish a stable link (on a 2-pair
	 * cable, 1000BASE-T training fails and the port loops instead of
	 * falling back). MediaTek recommends disabling EEE on this PHY.
	 * Clear the advertisement as early as possible, before anything
	 * can negotiate EEE with the link partner.
	 */
	return phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV, 0);
}

static int mt7530_phy_config_init(struct phy_device *phydev)
{
	int ret;

	mtk_gephy_config_init(phydev);

	/* The probe() clear alone is not durable: phy_init_hw() replays only
	 * ->config_init after a PHY reset, with the register back at its
	 * EEE-advertising hardware default, and phy_probe() zeroes
	 * eee_disabled_modes (of_set_phy_eee_broken()) after ->probe already
	 * ran. Clear the advertisement again and mark EEE disabled, so that
	 * neither phylib nor userspace can re-enable it; dp83867 disables
	 * broken EEE from config_init() the same way.
	 */
	ret = phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV, 0);
	if (ret)
		return ret;

	phy_disable_eee(phydev);

	/* Increase post_update_timer */
	phy_write_paged(phydev, MTK_PHY_PAGE_EXTENDED_3,
			MTK_PHY_RG_LPI_PCS_DSP_CTRL_REG11, 0x4b);

	return 0;
}

static int mt7531_phy_config_init(struct phy_device *phydev)
{
	mtk_gephy_config_init(phydev);

	/* PHY link down power saving enable */
	phy_set_bits(phydev, 0x17, BIT(4));
	phy_modify_mmd(phydev, MDIO_MMD_VEND1, MTK_PHY_RXADC_CTRL_RG7,
		       MTK_PHY_DA_AD_BUF_BIAS_LP_MASK,
		       FIELD_PREP(MTK_PHY_DA_AD_BUF_BIAS_LP_MASK, 0x3));

	/* Set TX Pair delay selection */
	phy_modify_mmd(phydev, MDIO_MMD_VEND1, MTK_PHY_GBE_MODE_TX_DELAY_SEL,
		       MTK_TX_DELAY_PAIR_B_MASK | MTK_TX_DELAY_PAIR_D_MASK,
		       FIELD_PREP(MTK_TX_DELAY_PAIR_B_MASK, 0x4) |
		       FIELD_PREP(MTK_TX_DELAY_PAIR_D_MASK, 0x4));
	phy_modify_mmd(phydev, MDIO_MMD_VEND1, MTK_PHY_TEST_MODE_TX_DELAY_SEL,
		       MTK_TX_DELAY_PAIR_B_MASK | MTK_TX_DELAY_PAIR_D_MASK,
		       FIELD_PREP(MTK_TX_DELAY_PAIR_B_MASK, 0x4) |
		       FIELD_PREP(MTK_TX_DELAY_PAIR_D_MASK, 0x4));

	return 0;
}

#define MT7531_LINK_WAIT	msecs_to_jiffies(5000)
#define MT7531_PULSE		msecs_to_jiffies(300)
#define MT7531_BACKOFF_STEP	250
#define MT7531_MAX_ROUNDS	16

struct mt7531_phy_priv {
	struct phy_device *phydev;
	struct delayed_work watch;
	unsigned int round;
	bool in_pulse;
};

static unsigned long mt7531_phy_backoff(struct phy_device *phydev,
					unsigned int round)
{
	struct net_device *ndev = phydev->attached_dev;
	unsigned int nibble;
	u32 key;

	if (ndev)
		key = crc32_le(~0, ndev->dev_addr, ETH_ALEN);
	else
		key = phydev->mdio.addr;

	nibble = (key >> (4 * (round & 7))) & 0xf;

	return msecs_to_jiffies(MT7531_BACKOFF_STEP * nibble);
}

static void mt7531_phy_watch(struct work_struct *work)
{
	struct mt7531_phy_priv *priv = container_of(work, struct mt7531_phy_priv,
						    watch.work);
	struct phy_device *phydev = priv->phydev;
	int bmsr, exp;

	mutex_lock(&phydev->lock);

	if (priv->in_pulse) {
		priv->in_pulse = false;
		genphy_resume(phydev);
		schedule_delayed_work(&priv->watch, MT7531_LINK_WAIT +
				      mt7531_phy_backoff(phydev, priv->round));
		goto out;
	}

	if (phydev->state < PHY_UP || phydev->autoneg != AUTONEG_ENABLE)
		goto out;

	/* Link status is latched low, read it twice. */
	bmsr = phy_read(phydev, MII_BMSR);
	if (bmsr >= 0)
		bmsr = phy_read(phydev, MII_BMSR);
	exp = phy_read(phydev, MII_EXPANSION);
	if (bmsr < 0 || exp < 0)
		goto out;

	if (bmsr & BMSR_LSTATUS)
		goto out;

	/* Nobody on the other end, nothing to break. */
	if (!(exp & EXPANSION_LCWP))
		goto out;

	if (priv->round >= MT7531_MAX_ROUNDS)
		goto out;

	priv->round++;
	priv->in_pulse = true;
	genphy_suspend(phydev);
	schedule_delayed_work(&priv->watch, MT7531_PULSE);
out:
	mutex_unlock(&phydev->lock);
}

static int mt7531_phy_resume(struct phy_device *phydev)
{
	struct mt7531_phy_priv *priv = phydev->priv;
	int ret;

	ret = genphy_resume(phydev);
	if (ret)
		return ret;

	priv->round = 0;
	priv->in_pulse = false;
	mod_delayed_work(system_wq, &priv->watch,
			 MT7531_LINK_WAIT + mt7531_phy_backoff(phydev, 0));

	return 0;
}

static int mt7531_phy_suspend(struct phy_device *phydev)
{
	struct mt7531_phy_priv *priv = phydev->priv;

	cancel_delayed_work_sync(&priv->watch);
	priv->in_pulse = false;

	return genphy_suspend(phydev);
}

static int mt7531_phy_probe(struct phy_device *phydev)
{
	struct mt7531_phy_priv *priv;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->phydev = phydev;
	INIT_DELAYED_WORK(&priv->watch, mt7531_phy_watch);
	phydev->priv = priv;

	return 0;
}

static void mt7531_phy_remove(struct phy_device *phydev)
{
	struct mt7531_phy_priv *priv = phydev->priv;

	cancel_delayed_work_sync(&priv->watch);
}

static struct phy_driver mtk_gephy_driver[] = {
	{
		PHY_ID_MATCH_EXACT(MTK_GPHY_ID_MT7530),
		.name		= "MediaTek MT7530 PHY",
		.probe		= mt7530_phy_probe,
		.config_init	= mt7530_phy_config_init,
		/* Interrupts are handled by the switch, not the PHY
		 * itself.
		 */
		.config_intr	= genphy_no_config_intr,
		.handle_interrupt = genphy_handle_interrupt_no_ack,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
		.read_page	= mtk_phy_read_page,
		.write_page	= mtk_phy_write_page,
	},
	{
		PHY_ID_MATCH_EXACT(MTK_GPHY_ID_MT7531),
		.name		= "MediaTek MT7531 PHY",
		.probe		= mt7531_phy_probe,
		.remove		= mt7531_phy_remove,
		.config_init	= mt7531_phy_config_init,
		/* Interrupts are handled by the switch, not the PHY
		 * itself.
		 */
		.config_intr	= genphy_no_config_intr,
		.handle_interrupt = genphy_handle_interrupt_no_ack,
		.suspend	= mt7531_phy_suspend,
		.resume		= mt7531_phy_resume,
		.read_page	= mtk_phy_read_page,
		.write_page	= mtk_phy_write_page,
	},
};

module_phy_driver(mtk_gephy_driver);

static const struct mdio_device_id __maybe_unused mtk_gephy_tbl[] = {
	{ PHY_ID_MATCH_EXACT(MTK_GPHY_ID_MT7530) },
	{ PHY_ID_MATCH_EXACT(MTK_GPHY_ID_MT7531) },
	{ }
};

MODULE_DESCRIPTION("MediaTek Gigabit Ethernet PHY driver");
MODULE_AUTHOR("DENG, Qingfang <dqfext@gmail.com>");
MODULE_LICENSE("GPL");

MODULE_DEVICE_TABLE(mdio, mtk_gephy_tbl);

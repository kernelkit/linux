// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/bitfield.h>
#include <linux/leds.h>
#include <linux/property.h>

#include "chip.h"
#include "global2.h"
#include "port.h"

/* Offset 0x16: LED control */

static int mv88e6xxx_port_led_write(struct mv88e6xxx_chip *chip, int port, u16 reg)
{
	reg |= MV88E6XXX_PORT_LED_CONTROL_UPDATE;

	return mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_LED_CONTROL, reg);
}

static int mv88e6xxx_port_led_read(struct mv88e6xxx_chip *chip, int port,
				   u16 ptr, u16 *val)
{
	int err;

	err = mv88e6xxx_port_write(chip, port, MV88E6XXX_PORT_LED_CONTROL, ptr);
	if (err)
		return err;

	err = mv88e6xxx_port_read(chip, port, MV88E6XXX_PORT_LED_CONTROL, val);
	*val &= 0x3ff;

	return err;
}

static int mv88e6xxx_led_brightness_set(struct mv88e6xxx_port *p, int led,
					int brightness)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_led_read(p->chip, p->port,
				      MV88E6XXX_PORT_LED_CONTROL_POINTER_LED01_CTRL,
				      &reg);
	if (err)
		return err;

	if (led == 1)
		reg &= ~MV88E6XXX_PORT_LED_CONTROL_LED1_SEL_MASK;
	else
		reg &= ~MV88E6XXX_PORT_LED_CONTROL_LED0_SEL_MASK;

	if (brightness) {
		/* Selector 0x0f == Force LED ON */
		if (led == 1)
			reg |= MV88E6XXX_PORT_LED_CONTROL_LED1_SELF;
		else
			reg |= MV88E6XXX_PORT_LED_CONTROL_LED0_SELF;
	} else {
		/* Selector 0x0e == Force LED OFF */
		if (led == 1)
			reg |= MV88E6XXX_PORT_LED_CONTROL_LED1_SELE;
		else
			reg |= MV88E6XXX_PORT_LED_CONTROL_LED0_SELE;
	}

	reg |= MV88E6XXX_PORT_LED_CONTROL_POINTER_LED01_CTRL;

	return mv88e6xxx_port_led_write(p->chip, p->port, reg);
}

static int mv88e6xxx_led0_brightness_set_blocking(struct led_classdev *ldev,
						  enum led_brightness brightness)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6xxx_led_brightness_set(p, 0, brightness);
	mv88e6xxx_reg_unlock(p->chip);

	return err;
}

static int mv88e6xxx_led1_brightness_set_blocking(struct led_classdev *ldev,
						  enum led_brightness brightness)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6xxx_led_brightness_set(p, 1, brightness);
	mv88e6xxx_reg_unlock(p->chip);

	return err;
}

struct mv88e6xxx_led_hwconfig {
	int led;
	u8 portmask;
	unsigned long rules;
	bool fiber;
	bool blink_activity;
	u16 selector;
};

/* 6393X LED mode flags */
#define MV88E6393X_FLAG_ACT      (BIT(TRIGGER_NETDEV_RX) | BIT(TRIGGER_NETDEV_TX))
#define MV88E6393X_FLAG_LINK     BIT(TRIGGER_NETDEV_LINK)
#define MV88E6393X_FLAG_LINK_10  BIT(TRIGGER_NETDEV_LINK_10)
#define MV88E6393X_FLAG_LINK_100 BIT(TRIGGER_NETDEV_LINK_100)
#define MV88E6393X_FLAG_LINK_1G  BIT(TRIGGER_NETDEV_LINK_1000)
#define MV88E6393X_FLAG_FULL     BIT(TRIGGER_NETDEV_FULL_DUPLEX)

/* 6393X LED modes */
#define MV88E6393X_LED_MODE_BLINK 0xd
#define MV88E6393X_LED_MODE_OFF   0xe
#define MV88E6393X_LED_MODE_ON    0xf
#define MV88E6393X_LED_MODES      0x10

/* 6393X LED mode to flags mapping for ports 1-8 */
static const unsigned long mv88e6393x_led_map_p1_p8[2][MV88E6393X_LED_MODES] = {
	[0] = {
		[0x1] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK_100 | MV88E6393X_FLAG_LINK_1G,
		[0x2] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK_1G,
		[0x3] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK,
		[0x6] = MV88E6393X_FLAG_FULL,
		[0x7] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK_10 | MV88E6393X_FLAG_LINK_1G,
		[0x8] = MV88E6393X_FLAG_LINK,
		[0x9] = MV88E6393X_FLAG_LINK_10,
		[0xa] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK_10,
		[0xb] = MV88E6393X_FLAG_LINK_100 | MV88E6393X_FLAG_LINK_1G,
	},
	[1] = {
		[0x1] = MV88E6393X_FLAG_ACT,
		[0x2] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK_10 | MV88E6393X_FLAG_LINK_100,
		[0x3] = MV88E6393X_FLAG_LINK_1G,
		[0x5] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK,
		[0x6] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK_10 | MV88E6393X_FLAG_LINK_1G,
		[0x7] = MV88E6393X_FLAG_LINK_10 | MV88E6393X_FLAG_LINK_1G,
		[0x9] = MV88E6393X_FLAG_LINK_100,
		[0xa] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK_100,
		[0xb] = MV88E6393X_FLAG_LINK_10 | MV88E6393X_FLAG_LINK_100,
	}
};

/* 6393X LED mode to flags mapping for ports 9-10 */
static const unsigned long mv88e6393x_led_map_p9_p10[2][MV88E6393X_LED_MODES] = {
	[0] = {
		[0x1] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK,
	},
	[1] = {
		[0x6] = MV88E6393X_FLAG_FULL,
		[0x7] = MV88E6393X_FLAG_ACT | MV88E6393X_FLAG_LINK,
		[0x8] = MV88E6393X_FLAG_LINK,
	}
};

/* The following is a lookup table to check what rules we can support on a
 * certain LED given restrictions such as that some rules only work with fiber
 * (SFP) connections and some blink on activity by default.
 */
#define MV88E6XXX_PORTS_0_3 (BIT(0) | BIT(1) | BIT(2) | BIT(3))
#define MV88E6XXX_PORTS_4_5 (BIT(4) | BIT(5))
#define MV88E6XXX_PORT_4 BIT(4)
#define MV88E6XXX_PORT_5 BIT(5)

/* Entries are listed in selector order.
 *
 * These configurations vary across different switch families, list
 * different tables per-family here.
 */
static const struct mv88e6xxx_led_hwconfig mv88e6352_led_hwconfigs[] = {
	{
		.led = 0,
		.portmask = MV88E6XXX_PORT_4,
		.rules = BIT(TRIGGER_NETDEV_LINK),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL0,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORT_5,
		.rules = BIT(TRIGGER_NETDEV_LINK_1000),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL0,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_100) | BIT(TRIGGER_NETDEV_LINK_1000),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL1,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_10) | BIT(TRIGGER_NETDEV_LINK_100),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL1,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_4_5,
		.rules = BIT(TRIGGER_NETDEV_LINK_100),
		.blink_activity = true,
		.fiber = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL1,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_4_5,
		.rules = BIT(TRIGGER_NETDEV_LINK_1000),
		.blink_activity = true,
		.fiber = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL1,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_1000),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL2,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_10) | BIT(TRIGGER_NETDEV_LINK_100),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL2,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_4_5,
		.rules = BIT(TRIGGER_NETDEV_LINK_1000),
		.blink_activity = true,
		.fiber = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL2,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_4_5,
		.rules = BIT(TRIGGER_NETDEV_LINK_100),
		.blink_activity = true,
		.fiber = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL2,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL3,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_1000),
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL3,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_4_5,
		.rules = BIT(TRIGGER_NETDEV_LINK),
		.fiber = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL3,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORT_4,
		.rules = BIT(TRIGGER_NETDEV_LINK),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL4,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORT_5,
		.rules = BIT(TRIGGER_NETDEV_LINK),
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL5,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_FULL_DUPLEX),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL6,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_10) | BIT(TRIGGER_NETDEV_LINK_1000),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL6,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORT_4,
		.rules = BIT(TRIGGER_NETDEV_FULL_DUPLEX),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL6,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORT_5,
		.rules = BIT(TRIGGER_NETDEV_FULL_DUPLEX),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL6,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_10) | BIT(TRIGGER_NETDEV_LINK_1000),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL7,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_10) | BIT(TRIGGER_NETDEV_LINK_1000),
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL7,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK),
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL8,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL8,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORT_5,
		.rules = BIT(TRIGGER_NETDEV_LINK),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL8,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_10),
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SEL9,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_100),
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SEL9,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_10),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SELA,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_100),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SELA,
	},
	{
		.led = 0,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_100) | BIT(TRIGGER_NETDEV_LINK_1000),
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED0_SELB,
	},
	{
		.led = 1,
		.portmask = MV88E6XXX_PORTS_0_3,
		.rules = BIT(TRIGGER_NETDEV_LINK_100) | BIT(TRIGGER_NETDEV_LINK_1000),
		.blink_activity = true,
		.selector = MV88E6XXX_PORT_LED_CONTROL_LED1_SELB,
	},
};

/* mv88e6xxx_led_match_selector() - look up the appropriate LED mode selector
 * @p: port state container
 * @led: LED number, 0 or 1
 * @blink_activity: blink the LED (usually blink on indicated activity)
 * @fiber: the link is connected to fiber such as SFP
 * @rules: LED status flags from the LED classdev core
 * @selector: fill in the selector in this parameter with an OR operation
 */
static int mv88e6xxx_led_match_selector(struct mv88e6xxx_port *p, int led, bool blink_activity,
					bool fiber, unsigned long rules, u16 *selector)
{
	const struct mv88e6xxx_led_hwconfig *conf;
	int i;

	/* No rules means we turn the LED off */
	if (!rules) {
		if (led == 1)
			*selector |= MV88E6XXX_PORT_LED_CONTROL_LED1_SELE;
		else
			*selector |= MV88E6XXX_PORT_LED_CONTROL_LED0_SELE;
		return 0;
	}

	/* TODO: these rules are for MV88E6352, when adding other families,
	 * think about making sure you select the table that match the
	 * specific switch family.
	 */
	for (i = 0; i < ARRAY_SIZE(mv88e6352_led_hwconfigs); i++) {
		conf = &mv88e6352_led_hwconfigs[i];

		if (conf->led != led)
			continue;

		if (!(conf->portmask & BIT(p->port)))
			continue;

		if (conf->blink_activity != blink_activity)
			continue;

		if (conf->fiber != fiber)
			continue;

		if (conf->rules == rules) {
			dev_dbg(p->chip->dev, "port%d LED %d set selector %04x for rules %08lx\n",
				p->port, led, conf->selector, rules);
			*selector |= conf->selector;
			return 0;
		}
	}

	return -EOPNOTSUPP;
}

/* mv88e6xxx_led_match_selector() - find Linux netdev rules from a selector value
 * @p: port state container
 * @selector: the selector value from the LED actity register
 * @led: LED number, 0 or 1
 * @rules: Linux netdev activity rules found from selector
 */
static int
mv88e6xxx_led_match_rule(struct mv88e6xxx_port *p, u16 selector, int led, unsigned long *rules)
{
	const struct mv88e6xxx_led_hwconfig *conf;
	int i;

	/* Find the selector in the table, we just look for the right selector
	 * and ignore if the activity has special properties such as blinking
	 * or is fiber-only.
	 */
	for (i = 0; i < ARRAY_SIZE(mv88e6352_led_hwconfigs); i++) {
		conf = &mv88e6352_led_hwconfigs[i];

		if (conf->led != led)
			continue;

		if (!(conf->portmask & BIT(p->port)))
			continue;

		if (conf->selector == selector) {
			dev_dbg(p->chip->dev, "port%d LED %d has selector %04x, rules %08lx\n",
				p->port, led, selector, conf->rules);
			*rules = conf->rules;
			return 0;
		}
	}

	return -EINVAL;
}

/* mv88e6xxx_led_get_selector() - get the appropriate LED mode selector
 * @p: port state container
 * @led: LED number, 0 or 1
 * @fiber: the link is connected to fiber such as SFP
 * @rules: LED status flags from the LED classdev core
 * @selector: fill in the selector in this parameter with an OR operation
 */
static int mv88e6xxx_led_get_selector(struct mv88e6xxx_port *p, int led,
				      bool fiber, unsigned long rules, u16 *selector)
{
	int err;

	/* What happens here is that we first try to locate a trigger with solid
	 * indicator (such as LED is on for a 1000 link) else we try a second
	 * sweep to find something suitable with a trigger that will blink on
	 * activity.
	 */
	err = mv88e6xxx_led_match_selector(p, led, false, fiber, rules, selector);
	if (err)
		return mv88e6xxx_led_match_selector(p, led, true, fiber, rules, selector);

	return 0;
}

/* Sets up the hardware blinking period */
static int mv88e6xxx_led_set_blinking_period(struct mv88e6xxx_port *p, int led,
					     unsigned long delay_on, unsigned long delay_off)
{
	unsigned long period;
	u16 reg;

	period = delay_on + delay_off;

	reg = 0;

	switch (period) {
	case 21:
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_BLINK_RATE_21MS;
		break;
	case 42:
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_BLINK_RATE_42MS;
		break;
	case 84:
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_BLINK_RATE_84MS;
		break;
	case 168:
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_BLINK_RATE_168MS;
		break;
	case 336:
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_BLINK_RATE_336MS;
		break;
	case 672:
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_BLINK_RATE_672MS;
		break;
	default:
		/* Fall back to software blinking */
		return -EINVAL;
	}

	/* This is essentially PWM duty cycle: how long time of the period
	 * will the LED be on. Zero isn't great in most cases.
	 */
	switch (delay_on) {
	case 0:
		/* This is usually pretty useless and will make the LED look OFF */
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_PULSE_STRETCH_NONE;
		break;
	case 21:
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_PULSE_STRETCH_21MS;
		break;
	case 42:
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_PULSE_STRETCH_42MS;
		break;
	case 84:
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_PULSE_STRETCH_84MS;
		break;
	case 168:
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_PULSE_STRETCH_168MS;
		break;
	default:
		/* Just use something non-zero */
		reg |= MV88E6XXX_PORT_LED_CONTROL_0x06_PULSE_STRETCH_21MS;
		break;
	}

	/* Set up blink rate */
	reg |= MV88E6XXX_PORT_LED_CONTROL_POINTER_STRETCH_BLINK;

	return mv88e6xxx_port_led_write(p->chip, p->port, reg);
}

static int mv88e6xxx_led_blink_set(struct mv88e6xxx_port *p, int led,
				   unsigned long *delay_on, unsigned long *delay_off)
{
	u16 reg;
	int err;

	/* Choose a sensible default 336 ms (~3 Hz) */
	if ((*delay_on == 0) && (*delay_off == 0)) {
		*delay_on = 168;
		*delay_off = 168;
	}

	/* No off delay is just on */
	if (*delay_off == 0)
		return mv88e6xxx_led_brightness_set(p, led, 1);

	err = mv88e6xxx_led_set_blinking_period(p, led, *delay_on, *delay_off);
	if (err)
		return err;

	err = mv88e6xxx_port_led_read(p->chip, p->port,
				      MV88E6XXX_PORT_LED_CONTROL_POINTER_LED01_CTRL,
				      &reg);
	if (err)
		return err;

	if (led == 1)
		reg &= ~MV88E6XXX_PORT_LED_CONTROL_LED1_SEL_MASK;
	else
		reg &= ~MV88E6XXX_PORT_LED_CONTROL_LED0_SEL_MASK;

	/* This will select the forced blinking status */
	if (led == 1)
		reg |= MV88E6XXX_PORT_LED_CONTROL_LED1_SELD;
	else
		reg |= MV88E6XXX_PORT_LED_CONTROL_LED0_SELD;

	reg |= MV88E6XXX_PORT_LED_CONTROL_POINTER_LED01_CTRL;

	return mv88e6xxx_port_led_write(p->chip, p->port, reg);
}

static int mv88e6xxx_led0_blink_set(struct led_classdev *ldev,
				    unsigned long *delay_on,
				    unsigned long *delay_off)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6xxx_led_blink_set(p, 0, delay_on, delay_off);
	mv88e6xxx_reg_unlock(p->chip);

	return err;
}

static int mv88e6xxx_led1_blink_set(struct led_classdev *ldev,
				    unsigned long *delay_on,
				    unsigned long *delay_off)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6xxx_led_blink_set(p, 1, delay_on, delay_off);
	mv88e6xxx_reg_unlock(p->chip);

	return err;
}

static int
mv88e6xxx_led0_hw_control_is_supported(struct led_classdev *ldev, unsigned long rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);
	u16 selector = 0;

	return mv88e6xxx_led_get_selector(p, 0, p->fiber, rules, &selector);
}

static int
mv88e6xxx_led1_hw_control_is_supported(struct led_classdev *ldev, unsigned long rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);
	u16 selector = 0;

	return mv88e6xxx_led_get_selector(p, 1, p->fiber, rules, &selector);
}

static int mv88e6xxx_led_hw_control_set(struct mv88e6xxx_port *p,
					int led, unsigned long rules)
{
	u16 reg;
	int err;

	err = mv88e6xxx_port_led_read(p->chip, p->port,
				      MV88E6XXX_PORT_LED_CONTROL_POINTER_LED01_CTRL,
				      &reg);
	if (err)
		return err;

	if (led == 1)
		reg &= ~MV88E6XXX_PORT_LED_CONTROL_LED1_SEL_MASK;
	else
		reg &= ~MV88E6XXX_PORT_LED_CONTROL_LED0_SEL_MASK;

	err = mv88e6xxx_led_get_selector(p, led, p->fiber, rules, &reg);
	if (err)
		return err;

	reg |= MV88E6XXX_PORT_LED_CONTROL_POINTER_LED01_CTRL;

	if (led == 0)
		dev_dbg(p->chip->dev, "LED 0 hw control on port %d trigger selector 0x%02x\n",
			p->port,
			(unsigned int)(reg & MV88E6XXX_PORT_LED_CONTROL_LED0_SEL_MASK));
	else
		dev_dbg(p->chip->dev, "LED 1 hw control on port %d trigger selector 0x%02x\n",
			p->port,
			(unsigned int)(reg & MV88E6XXX_PORT_LED_CONTROL_LED1_SEL_MASK) >> 4);

	return mv88e6xxx_port_led_write(p->chip, p->port, reg);
}

static int
mv88e6xxx_led_hw_control_get(struct mv88e6xxx_port *p, int led, unsigned long *rules)
{
	u16 val;
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6xxx_port_led_read(p->chip, p->port,
				      MV88E6XXX_PORT_LED_CONTROL_POINTER_LED01_CTRL, &val);
	mv88e6xxx_reg_unlock(p->chip);
	if (err)
		return err;

	/* Mask out the selector bits for this port */
	if (led == 1) {
		val &= MV88E6XXX_PORT_LED_CONTROL_LED1_SEL_MASK;
		/* It's forced blinking/OFF/ON */
		if (val == MV88E6XXX_PORT_LED_CONTROL_LED1_SELD ||
		    val == MV88E6XXX_PORT_LED_CONTROL_LED1_SELE ||
		    val == MV88E6XXX_PORT_LED_CONTROL_LED1_SELF) {
			*rules = 0;
			return 0;
		}
	} else {
		val &= MV88E6XXX_PORT_LED_CONTROL_LED0_SEL_MASK;
		/* It's forced blinking/OFF/ON */
		if (val == MV88E6XXX_PORT_LED_CONTROL_LED0_SELD ||
		    val == MV88E6XXX_PORT_LED_CONTROL_LED0_SELE ||
		    val == MV88E6XXX_PORT_LED_CONTROL_LED0_SELF) {
			*rules = 0;
			return 0;
		}
	}

	err = mv88e6xxx_led_match_rule(p, val, led, rules);
	if (!err)
		return 0;

	dev_dbg(p->chip->dev, "couldn't find matching selector for %04x\n", val);
	*rules = 0;
	return 0;
}

static int
mv88e6xxx_led0_hw_control_set(struct led_classdev *ldev, unsigned long rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6xxx_led_hw_control_set(p, 0, rules);
	mv88e6xxx_reg_unlock(p->chip);

	return err;
}

static int
mv88e6xxx_led1_hw_control_set(struct led_classdev *ldev, unsigned long rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6xxx_led_hw_control_set(p, 1, rules);
	mv88e6xxx_reg_unlock(p->chip);

	return err;
}

static int
mv88e6xxx_led0_hw_control_get(struct led_classdev *ldev, unsigned long *rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);

	return mv88e6xxx_led_hw_control_get(p, 0, rules);
}

static int
mv88e6xxx_led1_hw_control_get(struct led_classdev *ldev, unsigned long *rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);

	return mv88e6xxx_led_hw_control_get(p, 1, rules);
}

static struct device *mv88e6xxx_led_hw_control_get_device(struct mv88e6xxx_port *p)
{
	struct dsa_port *dp;

	dp = dsa_to_port(p->chip->ds, p->port);
	if (!dp)
		return NULL;
	if (dp->user)
		return &dp->user->dev;
	return NULL;
}

static struct device *
mv88e6xxx_led0_hw_control_get_device(struct led_classdev *ldev)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);

	return mv88e6xxx_led_hw_control_get_device(p);
}

static struct device *
mv88e6xxx_led1_hw_control_get_device(struct led_classdev *ldev)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);

	return mv88e6xxx_led_hw_control_get_device(p);
}

int mv88e6xxx_port_setup_leds(struct mv88e6xxx_chip *chip, int port)
{
	struct fwnode_handle *led = NULL, *leds = NULL;
	struct led_init_data init_data = { };
	enum led_default_state state;
	struct mv88e6xxx_port *p;
	struct led_classdev *l;
	struct device *dev;
	u32 led_num;
	int ret;

	/* LEDs are on ports 1,2,3,4, 5 and 6 (index 0..5), no more */
	if (port > 5)
		return -EOPNOTSUPP;

	p = &chip->ports[port];
	if (!p->fwnode)
		return 0;

	dev = chip->dev;

	leds = fwnode_get_named_child_node(p->fwnode, "leds");
	if (!leds) {
		dev_dbg(dev, "No Leds node specified in device tree for port %d!\n",
			port);
		return 0;
	}

	fwnode_for_each_child_node(leds, led) {
		/* Reg represent the led number of the port, max 2
		 * LEDs can be connected to each port, in some designs
		 * only one LED is connected.
		 */
		if (fwnode_property_read_u32(led, "reg", &led_num))
			continue;
		if (led_num > 1) {
			dev_err(dev, "invalid LED specified port %d\n", port);
			ret = -EINVAL;
			goto err_put_led;
		}

		if (led_num == 0)
			l = &p->led0;
		else
			l = &p->led1;

		state = led_init_default_state_get(led);
		switch (state) {
		case LEDS_DEFSTATE_ON:
			l->brightness = 1;
			mv88e6xxx_led_brightness_set(p, led_num, 1);
			break;
		case LEDS_DEFSTATE_KEEP:
			break;
		default:
			l->brightness = 0;
			mv88e6xxx_led_brightness_set(p, led_num, 0);
		}

		l->max_brightness = 1;
		if (led_num == 0) {
			l->brightness_set_blocking = mv88e6xxx_led0_brightness_set_blocking;
			l->blink_set = mv88e6xxx_led0_blink_set;
			l->hw_control_is_supported = mv88e6xxx_led0_hw_control_is_supported;
			l->hw_control_set = mv88e6xxx_led0_hw_control_set;
			l->hw_control_get = mv88e6xxx_led0_hw_control_get;
			l->hw_control_get_device = mv88e6xxx_led0_hw_control_get_device;
		} else {
			l->brightness_set_blocking = mv88e6xxx_led1_brightness_set_blocking;
			l->blink_set = mv88e6xxx_led1_blink_set;
			l->hw_control_is_supported = mv88e6xxx_led1_hw_control_is_supported;
			l->hw_control_set = mv88e6xxx_led1_hw_control_set;
			l->hw_control_get = mv88e6xxx_led1_hw_control_get;
			l->hw_control_get_device = mv88e6xxx_led1_hw_control_get_device;
		}
		l->hw_control_trigger = "netdev";

		init_data.default_label = ":port";
		init_data.fwnode = led;
		init_data.devname_mandatory = true;
		init_data.devicename = kasprintf(GFP_KERNEL, "%s:0%d:0%d", chip->info->name,
						 port, led_num);
		if (!init_data.devicename) {
			ret = -ENOMEM;
			goto err_put_led;
		}

		ret = devm_led_classdev_register_ext(dev, l, &init_data);
		kfree(init_data.devicename);

		if (ret) {
			dev_err(dev, "Failed to init LED %d for port %d", led_num, port);
			goto err_put_led;
		}
	}

	fwnode_handle_put(leds);
	return 0;

err_put_led:
	fwnode_handle_put(led);
	fwnode_handle_put(leds);
	return ret;
}

/* 6393X LED support */

static const unsigned long *mv88e6393x_led_map(int port, int led)
{
	if (port >= 1 && port <= 8)
		return mv88e6393x_led_map_p1_p8[led];
	if (port >= 9 && port <= 10)
		return mv88e6393x_led_map_p9_p10[led];
	return NULL;
}

static int mv88e6393x_led_flags_to_mode(int port, int led, unsigned long flags)
{
	const unsigned long *map = mv88e6393x_led_map(port, led);
	int i;

	if (!map)
		return -ENODEV;

	if (!flags)
		return MV88E6393X_LED_MODE_OFF;

	for (i = 0; i < MV88E6393X_LED_MODES; i++) {
		if (map[i] == flags)
			return i;
	}

	return -EOPNOTSUPP;
}

static int mv88e6393x_led_mode_to_flags(int port, int led, u8 mode,
					unsigned long *flags)
{
	const unsigned long *map = mv88e6393x_led_map(port, led);

	if (!map)
		return -ENODEV;

	if (mode == MV88E6393X_LED_MODE_OFF) {
		*flags = 0;
		return 0;
	}

	if (mode < MV88E6393X_LED_MODES && map[mode]) {
		*flags = map[mode];
		return 0;
	}

	return -EINVAL;
}

static int mv88e6393x_led_set_mode(struct mv88e6xxx_port *p, int led, int mode)
{
	u16 ctrl;
	int err;

	if (mode < 0)
		return mode;

	err = mv88e6393x_port_led_read(p->chip, p->port, 0, &ctrl);
	if (err)
		return err;

	if (led == 0) {
		ctrl &= ~0x0f;
		ctrl |= mode;
	} else {
		ctrl &= ~0xf0;
		ctrl |= mode << 4;
	}

	return mv88e6393x_port_led_write(p->chip, p->port, 0, ctrl);
}

static int mv88e6393x_led_get_mode(struct mv88e6xxx_port *p, int led)
{
	u16 ctrl;
	int err;

	err = mv88e6393x_port_led_read(p->chip, p->port, 0, &ctrl);
	if (err)
		return err;

	if (led == 0)
		return ctrl & 0xf;
	else
		return (ctrl >> 4) & 0xf;
}

static int mv88e6393x_led_brightness_set(struct mv88e6xxx_port *p, int led,
					 int brightness)
{
	if (brightness == LED_OFF)
		return mv88e6393x_led_set_mode(p, led, MV88E6393X_LED_MODE_OFF);

	return mv88e6393x_led_set_mode(p, led, MV88E6393X_LED_MODE_ON);
}

static int mv88e6393x_led0_brightness_set_blocking(struct led_classdev *ldev,
						   enum led_brightness brightness)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6393x_led_brightness_set(p, 0, brightness);
	mv88e6xxx_reg_unlock(p->chip);

	return err;
}

static int mv88e6393x_led1_brightness_set_blocking(struct led_classdev *ldev,
						   enum led_brightness brightness)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6393x_led_brightness_set(p, 1, brightness);
	mv88e6xxx_reg_unlock(p->chip);

	return err;
}

static int mv88e6393x_led_blink_set(struct mv88e6xxx_port *p, int led,
				    unsigned long *delay_on,
				    unsigned long *delay_off)
{
	int err;

	/* Defer anything other than 50% duty cycles to software */
	if (*delay_on != *delay_off)
		return -EINVAL;

	/* Reject values outside ~20% of our default rate (84ms) */
	if (*delay_on && ((*delay_on < 30) || (*delay_on > 50)))
		return -EINVAL;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6393x_led_set_mode(p, led, MV88E6393X_LED_MODE_BLINK);
	mv88e6xxx_reg_unlock(p->chip);
	if (!err)
		*delay_on = *delay_off = 42;

	return err;
}

static int mv88e6393x_led0_blink_set(struct led_classdev *ldev,
				     unsigned long *delay_on,
				     unsigned long *delay_off)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);

	return mv88e6393x_led_blink_set(p, 0, delay_on, delay_off);
}

static int mv88e6393x_led1_blink_set(struct led_classdev *ldev,
				     unsigned long *delay_on,
				     unsigned long *delay_off)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);

	return mv88e6393x_led_blink_set(p, 1, delay_on, delay_off);
}

static int
mv88e6393x_led0_hw_control_is_supported(struct led_classdev *ldev, unsigned long rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);
	int mode = mv88e6393x_led_flags_to_mode(p->port, 0, rules);

	return (mode < 0) ? mode : 0;
}

static int
mv88e6393x_led1_hw_control_is_supported(struct led_classdev *ldev, unsigned long rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);
	int mode = mv88e6393x_led_flags_to_mode(p->port, 1, rules);

	return (mode < 0) ? mode : 0;
}

static int mv88e6393x_led_hw_control_set(struct mv88e6xxx_port *p,
					 int led, unsigned long rules)
{
	int mode = mv88e6393x_led_flags_to_mode(p->port, led, rules);

	if (mode < 0)
		return mode;

	return mv88e6393x_led_set_mode(p, led, mode);
}

static int
mv88e6393x_led0_hw_control_set(struct led_classdev *ldev, unsigned long rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6393x_led_hw_control_set(p, 0, rules);
	mv88e6xxx_reg_unlock(p->chip);

	return err;
}

static int
mv88e6393x_led1_hw_control_set(struct led_classdev *ldev, unsigned long rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);
	int err;

	mv88e6xxx_reg_lock(p->chip);
	err = mv88e6393x_led_hw_control_set(p, 1, rules);
	mv88e6xxx_reg_unlock(p->chip);

	return err;
}

static int
mv88e6393x_led_hw_control_get(struct mv88e6xxx_port *p, int led, unsigned long *rules)
{
	int mode;

	mv88e6xxx_reg_lock(p->chip);
	mode = mv88e6393x_led_get_mode(p, led);
	mv88e6xxx_reg_unlock(p->chip);
	if (mode < 0)
		return mode;

	return mv88e6393x_led_mode_to_flags(p->port, led, mode, rules);
}

static int
mv88e6393x_led0_hw_control_get(struct led_classdev *ldev, unsigned long *rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led0);

	return mv88e6393x_led_hw_control_get(p, 0, rules);
}

static int
mv88e6393x_led1_hw_control_get(struct led_classdev *ldev, unsigned long *rules)
{
	struct mv88e6xxx_port *p = container_of(ldev, struct mv88e6xxx_port, led1);

	return mv88e6393x_led_hw_control_get(p, 1, rules);
}

int mv88e6393x_port_setup_leds(struct mv88e6xxx_chip *chip, int port)
{
	struct fwnode_handle *led = NULL, *leds = NULL;
	struct led_init_data init_data = { };
	enum led_default_state state;
	struct mv88e6xxx_port *p;
	struct led_classdev *l;
	struct device *dev;
	u32 led_num;
	int ret;

	/* 6393X has LEDs on ports 1-10 */
	if (port < 1 || port > 10)
		return -EOPNOTSUPP;

	p = &chip->ports[port];
	if (!p->fwnode)
		return 0;

	dev = chip->dev;

	leds = fwnode_get_named_child_node(p->fwnode, "leds");
	if (!leds) {
		dev_dbg(dev, "No Leds node specified in device tree for port %d!\n",
			port);
		return 0;
	}

	fwnode_for_each_child_node(leds, led) {
		if (fwnode_property_read_u32(led, "reg", &led_num))
			continue;
		if (led_num > 1) {
			dev_err(dev, "invalid LED specified port %d\n", port);
			ret = -EINVAL;
			goto err_put_led;
		}

		if (led_num == 0)
			l = &p->led0;
		else
			l = &p->led1;

		state = led_init_default_state_get(led);
		switch (state) {
		case LEDS_DEFSTATE_ON:
			l->brightness = 1;
			mv88e6393x_led_brightness_set(p, led_num, 1);
			break;
		case LEDS_DEFSTATE_KEEP:
			break;
		default:
			l->brightness = 0;
			mv88e6393x_led_brightness_set(p, led_num, 0);
		}

		l->max_brightness = 1;
		if (led_num == 0) {
			l->brightness_set_blocking = mv88e6393x_led0_brightness_set_blocking;
			l->blink_set = mv88e6393x_led0_blink_set;
			l->hw_control_is_supported = mv88e6393x_led0_hw_control_is_supported;
			l->hw_control_set = mv88e6393x_led0_hw_control_set;
			l->hw_control_get = mv88e6393x_led0_hw_control_get;
			l->hw_control_get_device = mv88e6xxx_led0_hw_control_get_device;
		} else {
			l->brightness_set_blocking = mv88e6393x_led1_brightness_set_blocking;
			l->blink_set = mv88e6393x_led1_blink_set;
			l->hw_control_is_supported = mv88e6393x_led1_hw_control_is_supported;
			l->hw_control_set = mv88e6393x_led1_hw_control_set;
			l->hw_control_get = mv88e6393x_led1_hw_control_get;
			l->hw_control_get_device = mv88e6xxx_led1_hw_control_get_device;
		}
		l->hw_control_trigger = "netdev";

		init_data.default_label = ":port";
		init_data.fwnode = led;
		init_data.devname_mandatory = true;
		init_data.devicename = kasprintf(GFP_KERNEL, "%s:0%d:0%d", chip->info->name,
						 port, led_num);
		if (!init_data.devicename) {
			ret = -ENOMEM;
			goto err_put_led;
		}

		ret = devm_led_classdev_register_ext(dev, l, &init_data);
		kfree(init_data.devicename);

		if (ret) {
			dev_err(dev, "Failed to init LED %d for port %d", led_num, port);
			goto err_put_led;
		}
	}

	fwnode_handle_put(leds);
	return 0;

err_put_led:
	fwnode_handle_put(led);
	fwnode_handle_put(leds);
	return ret;
}

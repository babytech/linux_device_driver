/******************************************************************************
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
*******************************************************************************/
#ifndef __LED_SUPPORT_H__
#define __LED_SUPPORT_H__

#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/leds.h>

#define LED_NAME_SIZE	32

struct dev_led {
	struct led_classdev cdev;
	void __iomem *reg_base;	/* dev base addr (passed via init function) */
	u8 reg_offset;		/* dev register offset for this led */
	u8 reg_mask;		/* bitmask to use when setting this led */
	u8 enb_reg_offset;
	u8 enb_reg_mask;
	u8 enb_reg_value;
	u8 enb_ctrl;		/* default: off (1=on, 0=off) */
	int active_low:1;	/* default: active high (1=on, 0=off) */
	int disabled:1;		/* if 1, registers are not accessed */
	char name[LED_NAME_SIZE];
	enum led_brightness (*led_brightness_get)(struct led_classdev *cdev);
	void (*led_brightness_set)(struct led_classdev *cdev, enum led_brightness led_value);
};

struct dev_work{
	struct work_struct work;
	struct led_classdev *cdev;
	enum led_brightness value;
};

struct led_dev_priv {
	struct workqueue_struct *workqueue;
	unsigned long  flags;
	int use_workqueue;
	int activated;
	void __iomem *reg_base;
	int num_leds;
	struct dev_work *works;
	struct dev_led leds[0];
};

/*
 * In dev_leds, following fields need to be filled:
 *    cdev.name
 *    cdev.brightness (if needed, default = 0)
 *    reg_offset
 *    reg_mask
 *    active_low (if needed, default = 0)
 *
 * num_dev_leds should be set to ARRAY_SIZE(dev_leds).
 *
 * See vipr-b for an example.
 */
int dev_leds_init(struct platform_device *pdev, void __iomem *dev_base,
	struct dev_led dev_leds[], int num_dev_leds);
void dev_leds_cleanup(struct platform_device *pdev,
	struct dev_led dev_leds[], int num_dev_leds);

void dev_leds_brightness_set(struct led_classdev *cdev,
	enum led_brightness led_value);

void dev_led_set_work(struct work_struct *work);

void dev_leds_enable(struct platform_device *pdev,
	struct dev_led dev_leds[], int num_dev_leds);
void dev_leds_disable(struct platform_device *pdev,
	struct dev_led dev_leds[], int num_dev_leds);

#endif /* __LED_SUPPORT_H__ */

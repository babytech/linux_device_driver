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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/leds.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <linux/slab.h>
#include "dev_common.h"
#include "led_support.h"

static enum led_brightness _dev_leds_brightness_get(struct led_classdev *cdev)
{
	struct dev_led *led = container_of(cdev, struct dev_led, cdev);
	u8 reg = 0;
	/* default setting:
	 * (OFF && active low) || (ON && ACTIVE_HIGH) */
	enum led_brightness ret = LED_FULL;

	if (!led->disabled) {
		void __iomem *addr = led->reg_base + led->reg_offset;

/*		dev_dbg(cdev->dev, "Read LED status for '%s'\n", cdev->name);*/

		reg = dev_read(addr) & led->reg_mask;
	}

	if ( (reg && led->active_low) || (!reg && !led->active_low)) {
		/* (ON && active low) || (OFF && active high) */
		ret = LED_OFF;
	}

	return ret;
}

static void _dev_leds_brightness_set(struct led_classdev *cdev,
		enum led_brightness led_value)
{
	struct dev_led *led = container_of(cdev, struct dev_led, cdev);

	if (!led->disabled) {
		void __iomem *addr = led->reg_base + led->reg_offset;

/*		dev_dbg(cdev->dev, "Write LED status %d for '%s'\n", (int)led_value, cdev->name);*/

		if ((led_value && led->active_low) || (!led_value && !led->active_low)) {
			/* (ON && active low) || (OFF && active high) */
			dev_read_modify_write(addr, led->reg_mask, 0);
		} else {
			/* (OFF && active low) || (ON && ACTIVE_HIGH) */
			dev_read_modify_write(addr, led->reg_mask, led->reg_mask);
		}
	}
}
static void dev_leds_enb_reg_set(struct led_classdev *cdev)
{
	struct dev_led *led = container_of(cdev, struct dev_led, cdev);

	if (!led->disabled && led->enb_ctrl) {
		void __iomem *addr = led->reg_base + led->enb_reg_offset;
		dev_read_modify_write(addr, led->enb_reg_mask, led->enb_reg_value);
		}
}

void dev_leds_brightness_set(struct led_classdev *cdev,
		enum led_brightness led_value)
{
	struct platform_device *pdev = to_platform_device(cdev->dev->parent);
	struct led_dev_priv *priv;

	/* Get platform data */
	priv = platform_get_drvdata(pdev);

	if (priv->use_workqueue) {
		priv->works = kzalloc(sizeof(struct dev_work),GFP_ATOMIC);
		if (priv->works == NULL){
			printk(KERN_INFO "ERROR: fail to alloc led_queued_work resource!");
			return;
		}
		INIT_WORK(&priv->works->work, dev_led_set_work);
		priv->works->cdev = cdev;
		priv->works->value = led_value;
		queue_work(priv->workqueue, &priv->works->work);
	} else {
		_dev_leds_brightness_set(cdev, led_value);
	}	
}
EXPORT_SYMBOL_GPL(dev_leds_brightness_set);

void dev_led_set_work(struct work_struct *work)
{
	struct dev_work *led_queued_work = container_of(work, struct dev_work, work);
	_dev_leds_brightness_set(led_queued_work->cdev, led_queued_work->value);
	kfree(led_queued_work);
}
EXPORT_SYMBOL_GPL(dev_led_set_work);

int dev_leds_init(struct platform_device *pdev, void __iomem *dev_base,
	struct dev_led dev_leds[], int num_dev_leds)
{
	int i;
	int ret = 0;
	enum led_brightness initial_brightness;

	for (i = 0; i < num_dev_leds; i++) {
		/* complete the led descriptors */
		dev_leds[i].cdev.brightness_get =
		dev_leds[i].led_brightness_get ? dev_leds[i].led_brightness_get : _dev_leds_brightness_get;
		dev_leds[i].cdev.brightness_set =
		dev_leds[i].led_brightness_set ? dev_leds[i].led_brightness_set : _dev_leds_brightness_set;
		dev_leds[i].cdev.flags |= LED_CORE_SUSPENDRESUME;
		dev_leds[i].reg_base = dev_base;

		/* back up initial_brighness, it might be overwritten during registration */
		initial_brightness = dev_leds[i].cdev.brightness;
		ret = led_classdev_register(&pdev->dev, &dev_leds[i].cdev);
		if (ret < 0)
			goto fail;

		dev_leds[i].cdev.brightness = initial_brightness;
		dev_leds[i].cdev.brightness_set(&dev_leds[i].cdev, dev_leds[i].cdev.brightness);
		dev_leds_enb_reg_set(&dev_leds[i].cdev);
	}

	return ret;

fail:
	while (--i >= 0)
		led_classdev_unregister(&dev_leds[i].cdev);
	return ret;
}

void dev_leds_cleanup(struct platform_device *pdev,
	struct dev_led dev_leds[], int num_dev_leds)
{
	int i;
	for (i = 0; i < num_dev_leds; i++) {
		led_classdev_unregister(&dev_leds[i].cdev);
	}
}

void dev_leds_enable(struct platform_device *pdev,
	struct dev_led dev_leds[], int num_dev_leds)
{
	int i;
	for (i = 0; i < num_dev_leds; i++) {
		dev_leds[i].disabled = 0;
	}
}

void dev_leds_disable(struct platform_device *pdev,
	struct dev_led dev_leds[], int num_dev_leds)
{
	int i;
	for (i = 0; i < num_dev_leds; i++) {
		dev_leds[i].disabled = 1;
	}
}

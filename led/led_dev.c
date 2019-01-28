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
#define DRIVER_VERSION      "0.0.6"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/leds.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/workqueue.h>
#include <linux/of_address.h>
#include "dev_common.h"
#include "led_support.h"
#include "led_dev.h"

#define MIN(a,b)    ((a) <= (b) ? (a) : (b))

#define DEV_ERR(child, dev, x)  \
	do { \
		dev_err(dev, "no or invalid " x " property\n"); \
		of_node_put(child); \
		child = NULL; \
		ret = -EFAULT; \
	} while (0)


/******************************************************************************
 * Module parameters
 ******************************************************************************/
static char version[] = DRIVER_VERSION;
module_param_string(version, version, sizeof(version), 0444);
MODULE_PARM_DESC(version, "Module version");

/******************************************************************************/
static inline size_t sizeof_dev_leds_priv(int num_leds)
{
	return sizeof(struct led_dev_priv) + (sizeof(struct dev_led) * num_leds);
}

/******************************************************************************/
void led_dev_enable(struct platform_device *pdev)
{
	struct led_dev_priv *priv = platform_get_drvdata(pdev);

	if (priv != NULL)
		dev_leds_enable(pdev, priv->leds, priv->num_leds);
}
EXPORT_SYMBOL_GPL(led_dev_enable);

/******************************************************************************/
void led_dev_disable(struct platform_device *pdev)
{
	struct led_dev_priv *priv = platform_get_drvdata(pdev);

	if (priv != NULL)
		dev_leds_disable(pdev, priv->leds, priv->num_leds);
}
EXPORT_SYMBOL_GPL(led_dev_disable);

/******************************************************************************/
int led_dev_activate(struct platform_device *pdev)
{
	struct led_dev_priv	*priv = NULL;
	struct device_node	*node = NULL;
	struct device_node	*child = NULL;
	struct device		*dev = &pdev->dev;
	struct dev_led		*led_dat;
	const __be32		*of_p;
	int			len, length;
	int			ret = -ENODEV;
	int			count = 0;

	/* Count LEDs in this device, so we know how much to allocate */
	for_each_child_of_node(dev->of_node, child)
		count++;
	if (count == 0)
		return 0; /* no error */

	/* Get 'alu,dev' parent of given leds node */
	node = of_get_parent(dev->of_node);
	if ((node == NULL) || !of_device_is_compatible(node, "alu,dev")) {
		dev_err(dev, "leds node does not have a parent 'alu,dev' node in device tree\n");
		goto err;
	}

	priv = devm_kzalloc(dev, sizeof_dev_leds_priv(count), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(dev, "cannot allocate led nodes\n");
		ret = -ENOMEM;
		goto err;
	}
	platform_set_drvdata(pdev, priv);

	priv->reg_base = of_iomap(node, 0);
	if (priv->reg_base == NULL) {
		dev_err(dev, "cannot iomap leds nodes\n");
		ret = -EIO;
		goto err;
	}

	if (of_get_property(dev->of_node, "use-workqueue", NULL) != NULL)
		priv->use_workqueue = 1;

	for_each_child_of_node(dev->of_node, child) {
		led_dat = &priv->leds[priv->num_leds];
		if (priv->use_workqueue) {
			led_dat->led_brightness_set = dev_leds_brightness_set;
		}	
		/* Get name (mandatory) */
		of_p = of_get_property(child, "led-name", &len);
		if (of_p == NULL) {
			DEV_ERR(child, dev, "led-name");
			goto err;
		}
		length = MIN(len, (LED_NAME_SIZE - 1));
		memcpy(led_dat->name, (char *)of_p, length);
		led_dat->name[length] = '\0';
		led_dat->cdev.name = led_dat->name;

		/* Get register offset (mandatory) */
		of_p = of_get_property(child, "reg-offset", &len);
		if ((of_p == NULL) || (len != sizeof(*of_p))) {
			DEV_ERR(child, dev, "reg-offset");
			goto err;
		}
		led_dat->reg_offset = (int)be32_to_cpup(of_p);

		/* Get register index (mandatory) */
		of_p = of_get_property(child, "bit-index", &len);
		if (of_p == NULL)
			of_p = of_get_property(child, "reg-index", &len);
		if ((of_p == NULL) || (len != sizeof(*of_p))) {
			DEV_ERR(child, dev, "bit-index/reg-index");
			goto err;
		}
		led_dat->reg_mask = (1 << be32_to_cpup(of_p));

		led_dat->enb_ctrl = 0;
		led_dat->enb_reg_offset = 0;
		led_dat->enb_reg_mask = 0;
		led_dat->enb_reg_value = 0;

		/* Get enable ctrl (mandatory) */
		of_p = of_get_property(child, "enable-ctrl", &len);
		if ((of_p == NULL) || (len != sizeof(*of_p))) {
			led_dat->enb_ctrl = 0;
		}else {
			led_dat->enb_ctrl = (int)be32_to_cpup(of_p);
		}

		if(led_dat->enb_ctrl == 1){
			/* Get enable register offset (mandatory) */
			of_p = of_get_property(child, "enable-reg-offset", &len);
			if ((of_p == NULL) || (len != sizeof(*of_p))) {
				led_dat->enb_ctrl = 0;
			}else {
				led_dat->enb_reg_offset = (int)be32_to_cpup(of_p);
			}

			/* Get enable register index (mandatory) */
			of_p = of_get_property(child, "enable-bit-index", &len);
			if ((of_p == NULL) || (len != sizeof(*of_p))) {
				led_dat->enb_ctrl = 0;
			}else {
				led_dat->enb_reg_mask = (1 << be32_to_cpup(of_p));
			}

			/* Get enable bit value (mandatory) */
			of_p = of_get_property(child, "enable-bit-value", &len);
			if ((of_p == NULL) || (len != sizeof(*of_p))) {
				led_dat->enb_ctrl = 0;
			}else {
				led_dat->enb_reg_value = (int)( be32_to_cpup(of_p));
			}
		}

		/* Get initial brightness setting (optional) */
		led_dat->cdev.brightness = LED_OFF; //default setting
		of_p = of_get_property(child, "initial-brightness", &len);
		if (of_p != NULL) {
			if (len != sizeof(*of_p)) {
				DEV_ERR(child, dev, "initial-brightness");
				goto err;
			}
			led_dat->cdev.brightness = (int)be32_to_cpup(of_p);
		}

		/* Get active_low setting (optional) */
		led_dat->active_low = 0;
		of_p = of_get_property(child, "active_low", &len);
		if (of_p != NULL) {
			if (len != sizeof(*of_p)) {
				DEV_ERR(child, dev, "active_low");
				goto err;
			}
			led_dat->active_low = (int)be32_to_cpup(of_p);
		}

		dev_dbg(dev, "Register led: name=%s, reg_offset=0x%x, reg_mask=0x%x, brightness=0x%x, active_low=0x%x\n",
			led_dat->cdev.name, led_dat->reg_offset, led_dat->reg_mask, led_dat->cdev.brightness, led_dat->active_low);

		priv->num_leds++;
	}

	/*
	 * In order for the priority of the created thread to be correct,
	 * its name should start with "kworker", cfr. kernel/kthread.c.
	 */
	priv->workqueue = create_singlethread_workqueue("kworker_led");
	if (!priv->workqueue) {
		dev_err(dev, "dev_led: failed to allocate workqueue\n");
		goto err;
	}

	ret = dev_leds_init(pdev, priv->reg_base, priv->leds, priv->num_leds);
	if (ret < 0) {
		dev_err(dev, "dev_leds_init failed\n");
		goto err;
	}

	return 0;

err:
	of_node_put(node);
	of_node_put(child);
	led_dev_deactivate(pdev);
	return ret;
}
EXPORT_SYMBOL_GPL(led_dev_activate);

/******************************************************************************/
int led_dev_deactivate(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct led_dev_priv *priv;

	/* Get platform data */
	priv = platform_get_drvdata(pdev);
	if (priv == NULL)
		return -ENODEV; /* device not active */

	if (priv->workqueue != NULL)
		destroy_workqueue(priv->workqueue);

	/* Unregister platform data */
	platform_set_drvdata(pdev, NULL);

	/* Unregister LEDs */
	dev_leds_cleanup(pdev, priv->leds, priv->num_leds);

	if (priv->reg_base) {
		iounmap(priv->reg_base);
		priv->reg_base = NULL;
	}

	/* Free platform data structure */
	devm_kfree(dev, priv);

	return 0;
}
EXPORT_SYMBOL_GPL(led_dev_deactivate);

/******************************************************************************/
static int led_dev_probe(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "driver probe version %s\n", DRIVER_VERSION);

	return led_dev_activate(pdev);
}

/******************************************************************************/
static int led_dev_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "driver remove version %s\n", DRIVER_VERSION);

	return led_dev_deactivate(pdev);
}

/******************************************************************************/
static const struct of_device_id of_dev_leds_match[] = {
	{ .compatible = "alu, dev-leds", },
	{},
};
MODULE_DEVICE_TABLE(of, of_dev_leds_match);

static struct platform_driver led_dev_driver = {
	.probe      = led_dev_probe,
	.remove     = led_dev_remove,
	.driver     = {
		.name   = "leds_dev",
		.owner  = THIS_MODULE,
		.of_match_table = of_dev_leds_match,
	},
};

/******************************************************************************/
static int __init led_dev_driver_init(void)
{
	printk(KERN_INFO "leds_dev: module init version %s\n", DRIVER_VERSION);

	return platform_driver_register(&led_dev_driver);
}

/******************************************************************************/
static void __exit led_dev_driver_exit(void)
{
	printk(KERN_INFO "leds_dev: module exit version %s\n", DRIVER_VERSION);

	platform_driver_unregister(&led_dev_driver);
}

/******************************************************************************/
module_init(led_dev_driver_init);
module_exit(led_dev_driver_exit);

/******************************************************************************/
MODULE_AUTHOR("babytech@126.com");
MODULE_DESCRIPTION("LED device driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:leds-dev");
MODULE_VERSION(DRIVER_VERSION);

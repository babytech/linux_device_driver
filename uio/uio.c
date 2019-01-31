#include <linux/module.h>		/* Needed by all modules */
#include <linux/kernel.h>		/* Needed for KERN_INFO */

#include <asm/io.h>			/* iounmap */
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/uio_driver.h>

#include <linux/irqreturn.h>


/* need a node in dts:

        ldd_uio@0x7c000000 {
                compatible = "ldd_uio";
                reg = <0x7c000000 0x1000>;
                interrupts = <0 31 4>;
        };
*/

#define DRIVER_VERSION   "0.0.1"
#define UIO_NAME_SIZE    16

struct platdata {
	struct uio_info *uioinfo_ldd;
};

static int uio_irq = -1;
module_param(uio_irq, int, 0644);
MODULE_PARM_DESC(uio_irq, "interrupt for UIO");

static irqreturn_t ldd_uio_irq_handler(int irq, struct uio_info *dev_info)
{
	printk(KERN_INFO "ldd_uio_irq_handler: %d received\n", irq);
	return IRQ_HANDLED;
}

static int ldd_uio_irqcontrol(struct uio_info *dev_info, s32 irq_on)
{
	printk(KERN_INFO "ldd_uio_irqcontrol: %d\n", irq_on);
	return 0;
}

static int ldd_uio_probe(struct platform_device *pdev)
{
	struct uio_info *uioinfo_ldd;
	struct platdata *platdata;
	char *ldd_uio_name;
	struct uio_mem *uiomem;
	struct resource res;
	int irq;
	int ret;

	dev_info(&pdev->dev, "device probe\n");


	/* Allocate data structures */
	platdata = devm_kzalloc(&pdev->dev, sizeof(*platdata), GFP_KERNEL);
	uioinfo_ldd = devm_kzalloc(&pdev->dev, sizeof(*uioinfo_ldd), GFP_KERNEL);
	ldd_uio_name = devm_kzalloc(&pdev->dev, UIO_NAME_SIZE, GFP_KERNEL);
	if (!uioinfo_ldd || !platdata || !ldd_uio_name) {
		dev_err(&pdev->dev, "couldn't allocate driver resources\n");
		goto err_alloc;
	}

	snprintf(ldd_uio_name, UIO_NAME_SIZE, "ldd_uio_drv");

	uioinfo_ldd->name = ldd_uio_name;
	uioinfo_ldd->version = DRIVER_VERSION;
	uioinfo_ldd->priv = platdata;
	uioinfo_ldd->irq = UIO_IRQ_NONE;


	/* Enable access to memory map */
	uiomem = &uioinfo_ldd->mem[0];

	ret = of_address_to_resource(pdev->dev.of_node, 0, &res);
	if (ret < 0) {
		dev_err(&pdev->dev, "couldn't obtain device resource\n");
		goto err_resource;
	}

	uiomem->name = "ldd_uio memory map";
	uiomem->memtype = UIO_MEM_PHYS;
	uiomem->addr = res.start;
	uiomem->size = res.end - res.start + 1;

	dev_info(&pdev->dev, "mapping physical memory at %#llx (size %#x)\n", (unsigned long long) uiomem->addr, uiomem->size);


	/* Map devices registers into kernel memory */
	uiomem->internal_addr = of_iomap(pdev->dev.of_node, 0);
	if (uiomem->internal_addr == NULL) {
		dev_err(&pdev->dev, "couldn't map device registers in driver\n");
		goto err_kernel_map;
	}


	/* Enable IRQ handling */
	irq = of_irq_to_resource(pdev->dev.of_node, 0, NULL);
	if (unlikely(irq == NO_IRQ)) {
		dev_warn(&pdev->dev, "couldn't obtain interrupt information from device tree\n");
	}

	/**************************************************************************************************
 	 * so far we don't know how to trigger a interrupt in QEMU environment,
         * as a test we reuse existed eth0 interrupt that has interrupt when package comming,
         * we can trigger the eth0 interrupt by sending package such as ssh login linux operation.
         * anyway, this is a bad hack in QEMU. For target hardware board, below codes should be removed.
         * the steps to use eth0 interrupt:
         * 1. # cat /proc/interrupts 
         *               CPU0       
         *    ... ...
         *    32:        214     GIC-0  47 Level     eth0
         * 2. get the irq is 32
         * 3. load this driver, run:
         *    insmod uio.ko uio_irq=32
	 */
	if (uio_irq > 0) {
		dev_warn(&pdev->dev, "use customer assigned irq %d, irq_in_device_tree %d\n", uio_irq, irq);
		irq = uio_irq;
	}
	/**************************************************************************************************/

	uioinfo_ldd->irq = irq;
	uioinfo_ldd->handler = ldd_uio_irq_handler;
	uioinfo_ldd->irqcontrol = ldd_uio_irqcontrol;
	uioinfo_ldd->irq_flags = IRQF_SHARED;	/* Good practice to support sharing interrupt lines */

	dev_info(&pdev->dev, "registering interrupt %ld to uio device '%s'\n", uioinfo_ldd->irq, uioinfo_ldd->name);


	/* Register uioinfo_ldd as platform data */
	platdata->uioinfo_ldd = uioinfo_ldd;
	platform_set_drvdata(pdev, platdata);


	/* Register uio devices */
	ret = uio_register_device(&pdev->dev, uioinfo_ldd);
	if (ret) {
		dev_err(&pdev->dev, "couldn't register uio device '%s'\n", uioinfo_ldd->name);
		goto err_reguio;
	}

	return 0;

  err_reguio:
  err_kernel_map:
  err_resource:
  err_alloc:

	if (platdata)
		devm_kfree(&pdev->dev, platdata);
	if (uioinfo_ldd)
		devm_kfree(&pdev->dev, uioinfo_ldd);
	if (ldd_uio_name)
		devm_kfree(&pdev->dev, ldd_uio_name);

	return -ENODEV;
}

static int ldd_uio_remove(struct platform_device *pdev)
{
	struct platdata *platdata = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "ldd_uio: device remove\n");

	uio_unregister_device(platdata->uioinfo_ldd);

	iounmap(platdata->uioinfo_ldd->mem[0].internal_addr);

	devm_kfree(&pdev->dev, platdata->uioinfo_ldd);

	return 0;
}

static const struct of_device_id ldd_uio_ids[] = {
	{
	 .compatible = "ldd_uio",
	 },
	{},
};

static struct platform_driver ldd_uio_driver = {
	.driver = {
			   .owner = THIS_MODULE,
			   .name = "ldd_uio",
			   .of_match_table = ldd_uio_ids,
			   },
	.probe = ldd_uio_probe,
	.remove = ldd_uio_remove,
};

static int __init ldd_uio_init(void)
{
	printk(KERN_INFO "ldd_uio: driver init\n");
	return platform_driver_register(&ldd_uio_driver);
}

static void __exit ldd_uio_exit(void)
{
	printk(KERN_NOTICE "ldd_uio: driver exit\n");
	platform_driver_unregister(&ldd_uio_driver);
}

module_init(ldd_uio_init);
module_exit(ldd_uio_exit);

MODULE_AUTHOR("Pan Guolin <guolinp@gmail.com>");
MODULE_DESCRIPTION("A UIO driver");
MODULE_LICENSE("GPL");

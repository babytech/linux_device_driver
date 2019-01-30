#include <linux/module.h>
#include <linux/interrupt.h>

static int the_irq = 31;
module_param(the_irq, int, 0644);
MODULE_PARM_DESC(the_irq, "interrupt to be regestered");

static irqreturn_t intr_handler(int irq, void *dev_id)
{
	printk(KERN_INFO "interrupt %d received\n", the_irq);
	return IRQ_HANDLED;
}

static int __init simple_interrupt_init(void)
{
	int ret = 0;

	ret = request_irq(the_irq, intr_handler, IRQF_SHARED, "simple_interrupt", &the_irq);

	printk(KERN_INFO "register interrupt %d, result %d\n", the_irq, ret);
	return ret;
}

static void __exit simple_interrupt_exit(void)
{
	free_irq(the_irq, &the_irq);
}

module_init(simple_interrupt_init);
module_exit(simple_interrupt_exit);

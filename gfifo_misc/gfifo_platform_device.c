/*
 * a simple platform device for gfifo
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>

static struct platform_device *gfifo_pdev;

static int __init gfifo_device_init(void)
{
	int ret;

	gfifo_pdev = platform_device_alloc("gfifo", -1);
	if (!gfifo_pdev)
		return -ENOMEM;

	ret = platform_device_add(gfifo_pdev);
	if (ret) {
		platform_device_put(gfifo_pdev);
		return ret;
	}
	return 0;
}
module_init(gfifo_device_init);

static void __exit gfifo_device_exit(void)
{
	platform_device_unregister(gfifo_pdev);
}
module_exit(gfifo_device_exit);

MODULE_AUTHOR("babytech@126.com");
MODULE_LICENSE("GPL v2");

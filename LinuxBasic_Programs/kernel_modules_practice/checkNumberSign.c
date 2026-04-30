#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/errno.h>

#define pr_fmt(fmt) "[CheckNumberSign] : " fmt

static int val = 10;
module_param(val, int, 0444);
MODULE_PARM_DESC(val, "Expect an Interger Parameter to determine its sign");

static int __init prog_init(void)
{
	pr_info("Module Loaded \n Enter a number in the below command format to get the output \n sudo insmod checkNumberSign.ko val=5 \n");

	if(val < 0)
	{
		pr_err("value is negative = %d , i am returning back, Unloading the module \n", val);
		return -EINVAL;
	}
	else if (val > 0) {
		pr_info("value is positive = %d \n", val);
	}
	else {
		pr_info("Value is zero, please enter other value\n");
	}
	return 0;
}

static void __exit prog_exit(void)
{
	pr_info("Module unloaded \n");
}

module_init(prog_init);
module_exit(prog_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SAI");
MODULE_DESCRIPTION("Accept int value and print its sign as a message");

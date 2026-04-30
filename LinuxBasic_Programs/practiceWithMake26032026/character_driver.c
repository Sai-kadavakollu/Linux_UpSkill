#include <linux/module.h>
#include <linux/fs.h>

#define DEVICE_NAME "mychardev"

static int major;

static int dev_open(struct inode *inode, struct file *file)
{
	printk(KERN_INFO "Device Opened \n");
	return 0;
}

static int dev_release(struct inode *inode, struct file *file)
{
	pr_info("Device closed\n");
	return 0;
}

static int __init chardev_init(void)
{
	major = register_chrdev(0, DEVICE_NAME, &fops);

	if(major < 0){
		pr_err(" Failed to register the device \n");
		return major;
	}

	pr_info("Registered with major number = %d\n", major);
	return 0;
}

static void __exit chardev_exit(void)
{
	unregister_chrdev(major, DEVICE_NAME);
	pr_info("Device unregistered \n");

}

module_init(chardev_init);
module_exit(chardev_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sai");
MODULE_DESCRIPTION(" Basic Character Driver");

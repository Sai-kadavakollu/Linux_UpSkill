#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>

#define DEVICE_NAME "gravity"
#define CLASS_NAME  "gravity_class"

static dev_t dev_num;
static struct cdev gravity_cdev;
static struct class *gravity_class;
static struct device *gravity_device;

/* Dummy data */
static char msg[] = "[LIS2DW12] x=100 y=200 z=300\n";

/* ================= FILE OPERATIONS ================= */

static int gravity_open(struct inode *inode, struct file *file)
{
    pr_info("gravity: device opened\n");
    return 0;
}

static int gravity_release(struct inode *inode, struct file *file)
{
    pr_info("gravity: device closed\n");
    return 0;
}

static ssize_t gravity_read(struct file *file, char __user *buf,
                            size_t len, loff_t *offset)
{
    int ret;
    int msg_len = strlen(msg);

    if (*offset >= msg_len)
        return 0;

    if (len > msg_len - *offset)
        len = msg_len - *offset;

    ret = copy_to_user(buf, msg + *offset, len);
    if (ret)
        return -EFAULT;

    *offset += len;

    return len;
}

/* ================= FILE OPERATIONS STRUCT ================= */

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = gravity_open,
    .read = gravity_read,
    .release = gravity_release,
};

/* ================= INIT ================= */

static int __init gravity_init(void)
{
    int ret;

    /* Allocate device number */
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    /* Initialize cdev */
    cdev_init(&gravity_cdev, &fops);

    /* Add cdev */
    ret = cdev_add(&gravity_cdev, dev_num, 1);
    if (ret < 0)
        goto unregister;

    /* Create class */
    gravity_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(gravity_class)) {
        ret = PTR_ERR(gravity_class);
        goto del_cdev;
    }

    /* Create device node */
    gravity_device = device_create(gravity_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(gravity_device)) {
        ret = PTR_ERR(gravity_device);
        goto destroy_class;
    }

    pr_info("gravity: driver loaded\n");
    return 0;

destroy_class:
    class_destroy(gravity_class);
del_cdev:
    cdev_del(&gravity_cdev);
unregister:
    unregister_chrdev_region(dev_num, 1);
    return ret;
}

/* ================= EXIT ================= */

static void __exit gravity_exit(void)
{
    device_destroy(gravity_class, dev_num);
    class_destroy(gravity_class);
    cdev_del(&gravity_cdev);
    unregister_chrdev_region(dev_num, 1);

    pr_info("gravity: driver unloaded\n");
}

module_init(gravity_init);
module_exit(gravity_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sai");
MODULE_DESCRIPTION("Simple Character Driver for Gravity Sensor");

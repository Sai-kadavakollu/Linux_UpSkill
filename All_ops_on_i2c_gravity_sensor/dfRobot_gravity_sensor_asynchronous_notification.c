#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>

/* ================= DEVICE ================= */

#define DEVICE_NAME "gravity"
#define CLASS_NAME  "gravity_class"

/* ================= LIS2DW12 ================= */

#define LIS2DW12_CTRL1    0x20
#define LIS2DW12_CTRL4    0x23

#define LIS2DW12_OUT_X_L  0x28
#define LIS2DW12_OUT_Y_L  0x2A
#define LIS2DW12_OUT_Z_L  0x2C

/* ================= DRIVER DATA ================= */

struct lis2dw12_data {
    struct i2c_client *client;

    /* char device */
    dev_t dev;
    struct cdev cdev;
    struct class *class;
    struct device *device;

    /* interrupt */
    struct gpio_desc *gpiod;
    int irq;

    /* async notification */
    struct fasync_struct *async_queue;

    /* sensor data */
    int x, y, z;
};

/* ================= I2C READ ================= */

static int read16(struct i2c_client *c, u8 reg)
{
    int l = i2c_smbus_read_byte_data(c, reg);
    int h = i2c_smbus_read_byte_data(c, reg + 1);

    return ((short)((h << 8) | l)) >> 2;
}

/* ================= SENSOR READ ================= */

static void read_xyz(struct lis2dw12_data *d)
{
    d->x = read16(d->client, LIS2DW12_OUT_X_L);
    d->y = read16(d->client, LIS2DW12_OUT_Y_L);
    d->z = read16(d->client, LIS2DW12_OUT_Z_L);
}

/* ================= IRQ HANDLER ================= */

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
    struct lis2dw12_data *d = dev_id;

    dev_info(&d->client->dev, "IRQ: GPIO51 triggered → data ready\n");

    /*  Send async signal */
    dev_info(&d->client->dev, "IRQ: Sending SIGIO via kill_fasync\n");

    kill_fasync(&d->async_queue, SIGIO, POLL_IN);

    return IRQ_HANDLED;
}

/* ================= READ ================= */

static ssize_t gravity_read(struct file *f,
                           char __user *buf,
                           size_t len,
                           loff_t *off)
{
    struct lis2dw12_data *d = f->private_data;
    char tmp[64];
    int l;
	
    dev_info(&d->client->dev, "read(): user reading data\n");
    
    read_xyz(d);
    
    dev_info(&d->client->dev, "READ DEBUG\n");

	pr_info("READ: file ptr = %px\n", f);
	pr_info("READ: private_data = %px\n", f->private_data);
	pr_info("READ: inode (via file) = %px\n", f->f_inode);

	pr_info("READ: sensor data struct\n");
	pr_info(" x=%d y=%d z=%d\n", d->x, d->y, d->z);
	    
    l = snprintf(tmp, sizeof(tmp),
                 "x=%d y=%d z=%d\n",
                 d->x, d->y, d->z);

    if (copy_to_user(buf, tmp, l))
        return -EFAULT;

    return l;
}

/* ================= OPEN ================= */

static int gravity_open(struct inode *i, struct file *f)
{
    struct lis2dw12_data *d;

    d = container_of(i->i_cdev, struct lis2dw12_data, cdev);
    f->private_data = d;

    pr_info("OPEN: inode info\n");
    pr_info(" inode->i_ino = %lu\n", i->i_ino);
    pr_info(" inode->i_rdev = %u\n", i->i_rdev);
    pr_info(" inode->i_cdev = %px\n", i->i_cdev);

    /* DEBUG file */
    pr_info("OPEN: file info\n");
    pr_info(" file ptr = %px\n", f);
    pr_info(" f_flags = 0x%x\n", f->f_flags);
    pr_info(" f_mode = 0x%x\n", f->f_mode);

    /* DEBUG private_data */
    pr_info("OPEN: private_data (driver struct)\n");
    pr_info(" data ptr = %px\n", d);
    pr_info(" client ptr = %px\n", d->client);
    pr_info(" irq = %d\n", d->irq);

    return 0;
}

/* ================= FASYNC ================= */

static int gravity_fasync(int fd, struct file *file, int on)
{
    struct lis2dw12_data *d = file->private_data;
    
    dev_info(&d->client->dev, "fasync: registration %s\n", on ? "ENABLED" : "DISABLED");

    return fasync_helper(fd, file, on, &d->async_queue);
}

/* ================= FILE OPS ================= */

static struct file_operations fops = {
    .owner  = THIS_MODULE,
    .open   = gravity_open,
    .read   = gravity_read,
    .fasync = gravity_fasync,
};

/* ================= PROBE ================= */

static int lis2dw12_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    struct lis2dw12_data *d;
    int ret;

    d = devm_kzalloc(&client->dev, sizeof(*d), GFP_KERNEL);
    if (!d)
        return -ENOMEM;

    d->client = client;
    i2c_set_clientdata(client, d);

    /* sensor init */
    i2c_smbus_write_byte_data(client, LIS2DW12_CTRL1, 0x54);

    /* enable DRDY interrupt */
    i2c_smbus_write_byte_data(client, LIS2DW12_CTRL4, 0x10);

    /* char device */
    alloc_chrdev_region(&d->dev, 0, 1, DEVICE_NAME);

    cdev_init(&d->cdev, &fops);
    cdev_add(&d->cdev, d->dev, 1);

    d->class = class_create(THIS_MODULE, CLASS_NAME);
    d->device = device_create(d->class, NULL, d->dev, NULL, DEVICE_NAME);

    /* GPIO */
    d->gpiod = devm_gpiod_get(&client->dev, "irq", GPIOD_IN);
    if (IS_ERR(d->gpiod))
        return PTR_ERR(d->gpiod);

    d->irq = gpiod_to_irq(d->gpiod);

    ret = devm_request_irq(&client->dev,
                           d->irq,
                           gpio_irq_handler,
                           IRQF_TRIGGER_RISING,
                           "gravity_irq",
                           d);
    dev_info(&client->dev, "STRUCT DEBUG\n");
    dev_info(&client->dev, " lis2dw12_data size = %lu\n", sizeof(struct lis2dw12_data));
    dev_info(&client->dev, " cdev offset = %lu\n",
         offsetof(struct lis2dw12_data, cdev));
    if (ret)
        return ret;

    dev_info(&client->dev, "LIS2DW12 async driver ready\n");

    return 0;
}

/* ================= REMOVE ================= */

static int lis2dw12_remove(struct i2c_client *client)
{
    struct lis2dw12_data *d = i2c_get_clientdata(client);

    device_destroy(d->class, d->dev);
    class_destroy(d->class);
    cdev_del(&d->cdev);
    unregister_chrdev_region(d->dev, 1);

    return 0;
}

/* ================= MATCH ================= */

static const struct of_device_id match[] = {
    { .compatible = "dfrobot,lis2dw12" },
    {}
};

static struct i2c_driver drv = {
    .driver = {
        .name = "lis2dw12_async",
        .of_match_table = match,
    },
    .probe = lis2dw12_probe,
    .remove = lis2dw12_remove,
};

module_i2c_driver(drv);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sai");

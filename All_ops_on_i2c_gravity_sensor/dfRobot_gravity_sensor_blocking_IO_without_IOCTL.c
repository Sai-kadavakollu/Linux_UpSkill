#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

/* ================= DEVICE ================= */

#define DEVICE_NAME "gravity"
#define CLASS_NAME  "gravity_class"

/* ================= LIS2DW12 ================= */

#define LIS2DW12_WHO_AM_I  0x0F
#define LIS2DW12_CTRL1     0x20

#define LIS2DW12_OUT_X_L   0x28
#define LIS2DW12_OUT_Y_L   0x2A
#define LIS2DW12_OUT_Z_L   0x2C

/* ================= DRIVER DATA ================= */

struct lis2dw12_data {
    struct i2c_client *client;

    dev_t dev_num;
    struct cdev cdev;
    struct class *class;
    struct device *device;

    /*  Blocking I/O */
    wait_queue_head_t wq;
    int data_ready;

    /* Sensor values */
    int x, y, z;
};

/* ================= I2C HELPERS ================= */

static int read16(struct i2c_client *c, u8 reg)
{
    int l = i2c_smbus_read_byte_data(c, reg);
    int h = i2c_smbus_read_byte_data(c, reg + 1);

    return ((short)((h << 8) | l)) >> 2;
}

static void read_xyz(struct lis2dw12_data *d)
{
    d->x = read16(d->client, LIS2DW12_OUT_X_L);
    d->y = read16(d->client, LIS2DW12_OUT_Y_L);
    d->z = read16(d->client, LIS2DW12_OUT_Z_L);
}

/* ================= BLOCKING READ ================= */

static ssize_t gravity_read(struct file *f,
                           char __user *buf,
                           size_t len,
                           loff_t *off)
{
    struct lis2dw12_data *d = f->private_data;
    char tmp[64];
    int l;

    /*
     * BLOCK HERE
     * First read will sleep until data_ready becomes 1
     */
    wait_event_interruptible(d->wq, d->data_ready);

    /* reset condition */
    d->data_ready = 0;

    /* REAL sensor read */
    read_xyz(d);

    l = snprintf(tmp, sizeof(tmp),
                 "x=%d y=%d z=%d\n",
                 d->x, d->y, d->z);

    if (*off >= l)
        return 0;

    if (len > l - *off)
        len = l - *off;

    if (copy_to_user(buf, tmp + *off, len))
        return -EFAULT;

    *off += len;

    return len;
}

/* ================= OPEN ================= */

static int gravity_open(struct inode *inode, struct file *file)
{
    struct lis2dw12_data *d;

    d = container_of(inode->i_cdev,
                     struct lis2dw12_data,
                     cdev);

    file->private_data = d;

    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open  = gravity_open,
    .read  = gravity_read,
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

    /* init wait queue */
    init_waitqueue_head(&d->wq);
    d->data_ready = 0;

    /* verify sensor */
    if (i2c_smbus_read_byte_data(client, LIS2DW12_WHO_AM_I) != 0x44)
        return -ENODEV;

    /* init sensor */
    i2c_smbus_write_byte_data(client, LIS2DW12_CTRL1, 0x54);

    /* char device */
    ret = alloc_chrdev_region(&d->dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&d->cdev, &fops);

    ret = cdev_add(&d->cdev, d->dev_num, 1);
    if (ret < 0)
        goto unregister;

    d->class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(d->class)) {
        ret = PTR_ERR(d->class);
        goto del_cdev;
    }

    d->device = device_create(d->class, NULL,
                              d->dev_num, NULL,
                              DEVICE_NAME);

    if (IS_ERR(d->device)) {
        ret = PTR_ERR(d->device);
        goto destroy_class;
    }

    /*
     *  Initial wakeup trigger
     * Otherwise read() will block forever
     */
    d->data_ready = 1;
    wake_up_interruptible(&d->wq);

    return 0;

destroy_class:
    class_destroy(d->class);
del_cdev:
    cdev_del(&d->cdev);
unregister:
    unregister_chrdev_region(d->dev_num, 1);
    return ret;
}

/* ================= REMOVE ================= */

static int lis2dw12_remove(struct i2c_client *client)
{
    struct lis2dw12_data *d = i2c_get_clientdata(client);

    device_destroy(d->class, d->dev_num);
    class_destroy(d->class);
    cdev_del(&d->cdev);
    unregister_chrdev_region(d->dev_num, 1);

    return 0;
}

/* ================= MATCH ================= */

static const struct of_device_id match[] = {
    { .compatible = "dfrobot,lis2dw12" },
    {}
};

static struct i2c_driver drv = {
    .driver = {
        .name = "lis_blocking_IO",
        .of_match_table = match,
    },
    .probe = lis2dw12_probe,
    .remove = lis2dw12_remove,
};

module_i2c_driver(drv);
MODULE_LICENSE("GPL");

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/ioctl.h>

/* ================= DEVICE ================= */

#define DEVICE_NAME "gravity"
#define CLASS_NAME  "gravity_class"

/* ================= LIS2DW12 ================= */

#define LIS2DW12_WHO_AM_I  0x0F
#define LIS2DW12_CTRL1     0x20

#define LIS2DW12_OUT_X_L   0x28
#define LIS2DW12_OUT_Y_L   0x2A
#define LIS2DW12_OUT_Z_L   0x2C

/* ================= IOCTL ================= */

#define GRAVITY_MAGIC 'g'

struct gravity_data {
    int x;
    int y;
    int z;
};

#define GET_XYZ   _IOR(GRAVITY_MAGIC, 1, struct gravity_data)
#define SET_ODR   _IOW(GRAVITY_MAGIC, 2, int)

/* ================= DRIVER DATA ================= */

struct lis2dw12_data {
    struct i2c_client *client;

    dev_t dev_num;
    struct cdev cdev;
    struct class *class;
    struct device *device;
};

/* ================= I2C HELPERS ================= */

static int lis2dw12_read8(struct i2c_client *client, u8 reg)
{
    return i2c_smbus_read_byte_data(client, reg);
}

static int lis2dw12_read16(struct i2c_client *client, u8 reg)
{
    s16 val;
    int lsb, msb;

    lsb = i2c_smbus_read_byte_data(client, reg);
    msb = i2c_smbus_read_byte_data(client, reg + 1);

    val = (s16)((msb << 8) | lsb);
    val = val >> 2;   /* 14-bit alignment */

    return val;
}

static void lis2dw12_read_xyz(struct lis2dw12_data *data,
                             struct gravity_data *d)
{
    struct i2c_client *c = data->client;

    d->x = lis2dw12_read16(c, LIS2DW12_OUT_X_L);
    d->y = lis2dw12_read16(c, LIS2DW12_OUT_Y_L);
    d->z = lis2dw12_read16(c, LIS2DW12_OUT_Z_L);
}

static int lis2dw12_write(struct i2c_client *client, u8 reg, u8 val)
{
    return i2c_smbus_write_byte_data(client, reg, val);
}

/* ================= FILE OPS ================= */

static ssize_t gravity_read(struct file *file,
                            char __user *buf,
                            size_t len,
                            loff_t *offset)
{
    struct lis2dw12_data *data = file->private_data;
    struct gravity_data d;
    char msg[64];
    int msg_len;

    lis2dw12_read_xyz(data, &d);

    msg_len = snprintf(msg, sizeof(msg),
                       "[LIS2DW12] x=%d y=%d z=%d\n",
                       d.x, d.y, d.z);

    if (*offset >= msg_len)
        return 0;

    if (len > msg_len - *offset)
        len = msg_len - *offset;

    if (copy_to_user(buf, msg + *offset, len))
        return -EFAULT;

    *offset += len;

    return len;
}

static long gravity_ioctl(struct file *file,
                         unsigned int cmd,
                         unsigned long arg)
{
    struct lis2dw12_data *data = file->private_data;
    struct gravity_data d;
    int odr;

    switch (cmd) {

    case GET_XYZ:
        lis2dw12_read_xyz(data, &d);

        if (copy_to_user((void __user *)arg, &d, sizeof(d)))
            return -EFAULT;
        break;

    case SET_ODR:
        if (copy_from_user(&odr, (void __user *)arg, sizeof(int)))
            return -EFAULT;

        if (odr == 100)
            lis2dw12_write(data->client, LIS2DW12_CTRL1, 0x54);

        break;

    default:
        return -EINVAL;
    }

    return 0;
}

static int gravity_open(struct inode *inode, struct file *file)
{
    struct lis2dw12_data *data;

    data = container_of(inode->i_cdev,
                        struct lis2dw12_data,
                        cdev);

    file->private_data = data;

    return 0;
}

static const struct file_operations gravity_fops = {
    .owner = THIS_MODULE,
    .open = gravity_open,
    .read = gravity_read,
    .unlocked_ioctl = gravity_ioctl,
};

/* ================= PROBE ================= */

static int lis2dw12_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    struct lis2dw12_data *data;
    int ret;

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    i2c_set_clientdata(client, data);

    /* Check device */
    if (lis2dw12_read8(client, LIS2DW12_WHO_AM_I) != 0x44)
        return -ENODEV;

    /* Init sensor */
    lis2dw12_write(client, LIS2DW12_CTRL1, 0x54);

    /* Char device setup */
    ret = alloc_chrdev_region(&data->dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0)
        return ret;

    cdev_init(&data->cdev, &gravity_fops);

    ret = cdev_add(&data->cdev, data->dev_num, 1);
    if (ret < 0)
        goto unregister;

    data->class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(data->class)) {
        ret = PTR_ERR(data->class);
        goto del_cdev;
    }

    data->device = device_create(data->class, NULL,
                                 data->dev_num, NULL,
                                 DEVICE_NAME);

    if (IS_ERR(data->device)) {
        ret = PTR_ERR(data->device);
        goto destroy_class;
    }

    dev_info(&client->dev, "LIS2DW12 driver probed\n");

    return 0;

destroy_class:
    class_destroy(data->class);
del_cdev:
    cdev_del(&data->cdev);
unregister:
    unregister_chrdev_region(data->dev_num, 1);
    return ret;
}

/* ================= REMOVE ================= */

static int lis2dw12_remove(struct i2c_client *client)
{
    struct lis2dw12_data *data = i2c_get_clientdata(client);

    device_destroy(data->class, data->dev_num);
    class_destroy(data->class);
    cdev_del(&data->cdev);
    unregister_chrdev_region(data->dev_num, 1);

    return 0;
}

/* ================= MATCH ================= */

static const struct of_device_id lis2dw12_of_match[] = {
    { .compatible = "dfrobot,lis2dw12" },
    {}
};
MODULE_DEVICE_TABLE(of, lis2dw12_of_match);

static const struct i2c_device_id lis2dw12_id[] = {
    { "lis2dw12", 0 },
    {}
};
MODULE_DEVICE_TABLE(i2c, lis2dw12_id);

/* ================= DRIVER ================= */

static struct i2c_driver lis2dw12_driver = {
    .driver = {
        .name = "lis2dw12_driver",
        .of_match_table = lis2dw12_of_match,
    },
    .probe = lis2dw12_probe,
    .remove = lis2dw12_remove,
    .id_table = lis2dw12_id,
};

module_i2c_driver(lis2dw12_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sai");
MODULE_DESCRIPTION("Proper LIS2DW12 Driver (I2C + Char + IOCTL)");

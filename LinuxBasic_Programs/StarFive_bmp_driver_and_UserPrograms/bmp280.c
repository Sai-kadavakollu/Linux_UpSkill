/*
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
 
#define BMP280_IOCTL_BASE 'B'

#define BMP280_GET_TEMP     _IOR(BMP280_IOCTL_BASE, 1, int)
#define BMP280_GET_PRESSURE _IOR(BMP280_IOCTL_BASE, 2, int)

// Registers 
#define BMP280_REG_CTRL_MEAS 0xF4
#define BMP280_REG_CONFIG    0xF5
 
// char device structure 
static dev_t bmp280_dev;
static struct class *bmp280_class;

struct bmp280_calib {
    u16 dig_T1;
    s16 dig_T2, dig_T3;
    u16 dig_P1;
    s16 dig_P2, dig_P3, dig_P4, dig_P5;
    s16 dig_P6, dig_P7, dig_P8, dig_P9;
};

struct bmp280_data {
    struct i2c_client *client;
    struct bmp280_calib calib;
    int t_fine;
    
    struct cdev cdev;
    
    struct mutex lock;
};

// Forward declarations 
static int bmp280_open(struct inode *inode, struct file *file);
static long bmp280_ioctl(struct file *file, unsigned int cmd, unsigned long arg);

static int bmp280_read_temp(struct bmp280_data *data);
static int bmp280_read_pressure(struct bmp280_data *data);


// Read calibration 
static void bmp280_read_calib(struct bmp280_data *data)
{
    u8 buf[24];
    struct i2c_client *c = data->client;
    int ret;
    ret = i2c_smbus_read_i2c_block_data(c, 0x88, 24, buf);
    if (ret < 0) {
      dev_err(&c->dev, "Failed to read calib\n");
      return;
    }
 
    data->calib.dig_T1 = buf[1] << 8 | buf[0];
    data->calib.dig_T2 = buf[3] << 8 | buf[2];
    data->calib.dig_T3 = buf[5] << 8 | buf[4];
 
    data->calib.dig_P1 = buf[7] << 8 | buf[6];
    data->calib.dig_P2 = buf[9] << 8 | buf[8];
    data->calib.dig_P3 = buf[11] << 8 | buf[10];
    data->calib.dig_P4 = buf[13] << 8 | buf[12];
    data->calib.dig_P5 = buf[15] << 8 | buf[14];
    data->calib.dig_P6 = buf[17] << 8 | buf[16];
    data->calib.dig_P7 = buf[19] << 8 | buf[18];
    data->calib.dig_P8 = buf[21] << 8 | buf[20];
    data->calib.dig_P9 = buf[23] << 8 | buf[22];
}
 
 // IOCTL Function 
static long bmp280_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct bmp280_data *data = file->private_data;
    int value;
    
    
    pr_info("Process %s trying to lock\n", current->comm);

    mutex_lock(&data->lock);

    pr_info("Process %s acquired lock\n", current->comm);

    switch (cmd) {

    case BMP280_GET_TEMP:
        value = bmp280_read_temp(data);
        if (copy_to_user((int __user *)arg, &value, sizeof(value)))
        {
            mutex_unlock(&data->lock);
            return -EFAULT;
        }
        break;

    case BMP280_GET_PRESSURE:
        value = bmp280_read_pressure(data);
        if (copy_to_user((int __user *)arg, &value, sizeof(value)))
        {
            mutex_unlock(&data->lock);
            return -EFAULT;
        }
        break;

    default:
    	mutex_unlock(&data->lock);
        return -EINVAL;
    }
    
    mutex_unlock(&data->lock);
    return 0;
}

// OPEN Fucntion
static int bmp280_open(struct inode *inode, struct file *file)
{
    struct bmp280_data *data;

    data = container_of(inode->i_cdev, struct bmp280_data, cdev);
    file->private_data = data;

    return 0;
}


// Configure sensor 
static void bmp280_configure(struct bmp280_data *data)
{
    struct i2c_client *c = data->client;
 
    i2c_smbus_write_byte_data(c, BMP280_REG_CTRL_MEAS, 0x27);
    i2c_smbus_write_byte_data(c, BMP280_REG_CONFIG, 0xA0);
}
 
// Read temperature (returns value in 0.01 °C) 
static int bmp280_read_temp(struct bmp280_data *data)
{
    u8 buf[3];
    int adc_T, var1, var2;
    struct i2c_client *c = data->client;
 	
    int ret;
    ret = i2c_smbus_read_i2c_block_data(c, 0xFA, 3, buf);
    if (ret < 0)
    return ret;
    adc_T = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);
 
    var1 = ((((adc_T >> 3) - ((int)data->calib.dig_T1 << 1))) *
                ((int)data->calib.dig_T2)) >> 11;
 
    var2 = (((((adc_T >> 4) - ((int)data->calib.dig_T1)) *
                  ((adc_T >> 4) - ((int)data->calib.dig_T1))) >> 12) *
                ((int)data->calib.dig_T3)) >> 14;
 
    data->t_fine = var1 + var2;
 
    return (data->t_fine * 5 + 128) >> 8;
}
 
// Read pressure (Pa) 
static int bmp280_read_pressure(struct bmp280_data *data)
{
    u8 buf[3];
    int adc_P;
    struct i2c_client *c = data->client;
 
    s64 var1, var2, p;
 
    i2c_smbus_read_i2c_block_data(c, 0xF7, 3, buf);
    adc_P = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);
 
    
 
    var1 = (s64)data->t_fine - 128000;
    var2 = var1 * var1 * data->calib.dig_P6;
    var2 += (var1 * data->calib.dig_P5) << 17;
    var2 += ((s64)data->calib.dig_P4) << 35;
 
    var1 = ((var1 * var1 * data->calib.dig_P3) >> 8) +
           ((var1 * data->calib.dig_P2) << 12);
 
    var1 = (((((s64)1) << 47) + var1) * data->calib.dig_P1) >> 33;
 
    if (var1 == 0)
        return 0;
 
    p = 1048576 - adc_P;
    p = div64_s64((p << 31) - var2, var1);
    p = p * 3125;
 
    var1 = (data->calib.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = (data->calib.dig_P8 * p) >> 19;
 
    p = ((p + var1 + var2) >> 8);
 
    return (int)p;
}
 
// SYSFS 
static ssize_t temp_show(struct device *dev,
                         struct device_attribute *attr, char *buf)
{
    struct bmp280_data *data = dev_get_drvdata(dev);
    int t = bmp280_read_temp(data);
 
    return sprintf(buf, "%d.%02d\n", t / 100, t % 100);
}
static DEVICE_ATTR_RO(temp);
 
static ssize_t pressure_show(struct device *dev,
                             struct device_attribute *attr, char *buf)
{
    struct bmp280_data *data = dev_get_drvdata(dev);
    int p = bmp280_read_pressure(data);
 
    return sprintf(buf, "%d\n", p);
}
static DEVICE_ATTR_RO(pressure);
 
static struct attribute *bmp280_attrs[] = {
&dev_attr_temp.attr,
&dev_attr_pressure.attr,
    NULL,
};
 
static const struct attribute_group bmp280_group = {
    .attrs = bmp280_attrs,
};
 
 static const struct file_operations bmp280_fops = {
    .owner = THIS_MODULE,
    .open = bmp280_open,
    .unlocked_ioctl = bmp280_ioctl,
};

// PROBE 
static int bmp280_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct bmp280_data *data;
    int ret;
 
    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;
 
    data->client = client;
    i2c_set_clientdata(client, data);
 
    bmp280_read_calib(data);
    bmp280_configure(data);
    msleep(100);
 	
    alloc_chrdev_region(&bmp280_dev, 0, 1, "bmp280");
    cdev_init(&data->cdev, &bmp280_fops);
    cdev_add(&data->cdev, bmp280_dev, 1);
    
    bmp280_class = class_create(THIS_MODULE, "bmp280_class");
    device_create(bmp280_class, NULL, bmp280_dev, NULL, "bmp280");

    ret = sysfs_create_group(&client->dev.kobj, &bmp280_group);
    if (ret)
        return ret;
    mutex_init(&data->lock);
    
    dev_info(&client->dev, "BMP280 driver loaded\n");
    return 0;
}
 
static int bmp280_remove(struct i2c_client *client)
{
    struct bmp280_data *data = i2c_get_clientdata(client);

    device_destroy(bmp280_class, bmp280_dev);
    class_destroy(bmp280_class);
    cdev_del(&data->cdev);
    unregister_chrdev_region(bmp280_dev, 1);
    
    sysfs_remove_group(&client->dev.kobj, &bmp280_group);
    return 0;
}
 
 
// DTS match
static const struct of_device_id bmp280_of_match[] = {
    { .compatible = "sai,bmp280" },
    {}
};
MODULE_DEVICE_TABLE(of, bmp280_of_match);
 
static struct i2c_driver bmp280_driver = {
    .driver = {
        .name = "bmp280_custom",
        .of_match_table = bmp280_of_match,
    },
    .probe = bmp280_probe,
    .remove = bmp280_remove,
};
 
module_i2c_driver(bmp280_driver);
 
MODULE_LICENSE("GPL");
MODULE_AUTHOR("My team");
*/

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/kernel.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/interrupt.h>

/* ---------------- IOCTL COMMANDS ---------------- */
#define BMP280_IOCTL_BASE 'B'
#define BMP280_GET_TEMP     _IOR(BMP280_IOCTL_BASE, 1, int)
#define BMP280_GET_PRESSURE _IOR(BMP280_IOCTL_BASE, 2, int)

/* ---------------- GLOBAL DEVICE ---------------- */
static dev_t bmp280_dev;
static struct class *bmp280_class;

/* ---------------- CALIB STRUCT ---------------- */
struct bmp280_calib {
    u16 dig_T1;
    s16 dig_T2, dig_T3;
    u16 dig_P1;
    s16 dig_P2, dig_P3, dig_P4, dig_P5;
    s16 dig_P6, dig_P7, dig_P8, dig_P9;
};

/* ---------------- DRIVER DATA ---------------- */
struct bmp280_data {
    struct i2c_client *client;
    struct bmp280_calib calib;
    int t_fine;

    struct cdev cdev;

    struct mutex lock;              //  protects I2C access
    wait_queue_head_t wq;           //  wait queue
    int data_ready;                 // flag for read()

    int irq;                        // interrupt line
};

/* ---------------- SENSOR CONFIG ---------------- */
#define BMP280_REG_CTRL_MEAS 0xF4
#define BMP280_REG_CONFIG    0xF5

/* ---------------- OPEN ---------------- */
static int bmp280_open(struct inode *inode, struct file *file)
{
    struct bmp280_data *data;

    // Get driver structure from cdev
    data = container_of(inode->i_cdev, struct bmp280_data, cdev);

    file->private_data = data;

    return 0;
}

/* ---------------- READ (BLOCKING) ---------------- */
static ssize_t bmp280_read(struct file *file, char __user *buf,
                           size_t count, loff_t *ppos)
{
    struct bmp280_data *data = file->private_data;
    int values[2]; // [temp, pressure]

    /* Wait until interrupt signals data ready */
    wait_event_interruptible(data->wq, data->data_ready);

    mutex_lock(&data->lock);

    values[0] = 25 * 100;   // Dummy temp (replace with real read)
    values[1] = 100000;     // Dummy pressure

    data->data_ready = 0;   // reset flag

    mutex_unlock(&data->lock);

    if (copy_to_user(buf, values, sizeof(values)))
        return -EFAULT;

    return sizeof(values);
}

/* ---------------- IOCTL ---------------- */
static long bmp280_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct bmp280_data *data = file->private_data;
    int value = 0;

    mutex_lock(&data->lock);

    switch (cmd) {

    case BMP280_GET_TEMP:
        value = 2500; // dummy
        break;

    case BMP280_GET_PRESSURE:
        value = 100000; // dummy
        break;

    default:
        mutex_unlock(&data->lock);
        return -EINVAL;
    }

    mutex_unlock(&data->lock);

    if (copy_to_user((int __user *)arg, &value, sizeof(value)))
        return -EFAULT;

    return 0;
}

/* ---------------- INTERRUPT HANDLER ---------------- */
static irqreturn_t bmp280_irq_handler(int irq, void *dev_id)
{
    struct bmp280_data *data = dev_id;

    pr_info("BMP280: Interrupt received\n");

    /* Mark data ready */
    data->data_ready = 1;

    /* Wake up any blocking read() */
    wake_up_interruptible(&data->wq);

    return IRQ_HANDLED;
}

/* ---------------- FILE OPS ---------------- */
static struct file_operations bmp280_fops = {
    .owner = THIS_MODULE,
    .open = bmp280_open,
    .read = bmp280_read,
    .unlocked_ioctl = bmp280_ioctl,
};

/* ---------------- PROBE ---------------- */
static int bmp280_probe(struct i2c_client *client,
                        const struct i2c_device_id *id)
{
    struct bmp280_data *data;
    int ret;

    pr_info("BMP280: Probe start\n");

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    i2c_set_clientdata(client, data);

    /* Initialize sync primitives */
    mutex_init(&data->lock);
    init_waitqueue_head(&data->wq);
    data->data_ready = 0;

    /* ---------------- CHAR DEVICE ---------------- */
    alloc_chrdev_region(&bmp280_dev, 0, 1, "bmp280");

    cdev_init(&data->cdev, &bmp280_fops);
    cdev_add(&data->cdev, bmp280_dev, 1);

    bmp280_class = class_create(THIS_MODULE, "bmp280_class");
    device_create(bmp280_class, NULL, bmp280_dev, NULL, "bmp280");

    /* ---------------- INTERRUPT SETUP ---------------- */
    data->irq = client->irq;

    if (data->irq <= 0) {
        pr_err("BMP280: No IRQ found\n");
        return -EINVAL;
    }
	pr_info("IRQ = %d\n", data->irq);
	
    ret = request_irq(data->irq,
                      bmp280_irq_handler,
                      IRQF_TRIGGER_FALLING,
                      "bmp280_irq",
                      data);

    if (ret) {
        pr_err("BMP280: IRQ request failed\n");
        return ret;
    }

    pr_info("BMP280: Driver loaded successfully\n");

    return 0;
}

/* ---------------- REMOVE ---------------- */
static int bmp280_remove(struct i2c_client *client)
{
    struct bmp280_data *data = i2c_get_clientdata(client);

    free_irq(data->irq, data);

    device_destroy(bmp280_class, bmp280_dev);
    class_destroy(bmp280_class);

    cdev_del(&data->cdev);
    unregister_chrdev_region(bmp280_dev, 1);

    return 0;
}

/* ---------------- DT MATCH ---------------- */
static const struct of_device_id bmp280_of_match[] = {
    { .compatible = "sai,bmp280" },
    {}
};
MODULE_DEVICE_TABLE(of, bmp280_of_match);

/* ---------------- I2C DRIVER ---------------- */
static struct i2c_driver bmp280_driver = {
    .driver = {
        .name = "bmp280_interrupt",
        .of_match_table = bmp280_of_match,
    },
    .probe = bmp280_probe,
    .remove = bmp280_remove,
};

module_i2c_driver(bmp280_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("You");

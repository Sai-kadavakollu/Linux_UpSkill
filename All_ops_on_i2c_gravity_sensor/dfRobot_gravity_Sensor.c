#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/slab.h>

/* ================= REGISTER DEFINITIONS ================= */

#define LIS2DW12_WHO_AM_I  0x0F
#define LIS2DW12_CTRL1     0x20

#define LIS2DW12_OUT_X_L   0x28
#define LIS2DW12_OUT_Y_L   0x2A
#define LIS2DW12_OUT_Z_L   0x2C

/* ================= DRIVER DATA ================= */

struct lis2dw12_data {
    struct i2c_client *client;
    s16 x;
    s16 y;
    s16 z;
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

    /* 14-bit data → shift */
    val = val >> 2;

    return val;
}

static int lis2dw12_write(struct i2c_client *client, u8 reg, u8 val)
{
    return i2c_smbus_write_byte_data(client, reg, val);
}

/* ================= READ SENSOR ================= */

static void lis2dw12_read_data(struct lis2dw12_data *data)
{
    struct i2c_client *c = data->client;

    data->x = lis2dw12_read16(c, LIS2DW12_OUT_X_L);
    data->y = lis2dw12_read16(c, LIS2DW12_OUT_Y_L);
    data->z = lis2dw12_read16(c, LIS2DW12_OUT_Z_L);
}

/* ================= SYSFS ================= */

static ssize_t gravityRaw_show(struct device *dev,
                               struct device_attribute *attr,
                               char *buf)
{
    struct lis2dw12_data *data = dev_get_drvdata(dev);

    lis2dw12_read_data(data);

    return sprintf(buf,
                   "[LIS2DW12] x=%d y=%d z=%d\n",
                   data->x, data->y, data->z);
}
static DEVICE_ATTR_RO(gravityRaw);

static struct attribute *lis2dw12_attrs[] = {
    &dev_attr_gravityRaw.attr,
    NULL,
};

static const struct attribute_group lis2dw12_group = {
    .attrs = lis2dw12_attrs,
};

/* ================= PROBE ================= */

static int lis2dw12_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    struct lis2dw12_data *data;
    int chip_id;
    int ret;

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    i2c_set_clientdata(client, data);

    /* Check device ID */
    chip_id = lis2dw12_read8(client, LIS2DW12_WHO_AM_I);
    if (chip_id != 0x44) {
        dev_err(&client->dev, "LIS2DW12 not found\n");
        return -ENODEV;
    }

    /* Init: 100Hz + High Performance */
    lis2dw12_write(client, LIS2DW12_CTRL1, 0x54);

    /* Create sysfs */
    ret = sysfs_create_group(&client->dev.kobj, &lis2dw12_group);
    if (ret)
        return ret;

    dev_info(&client->dev, "LIS2DW12 initialized\n");

    return 0;
}

/* ================= REMOVE ================= */

static int lis2dw12_remove(struct i2c_client *client)
{
    sysfs_remove_group(&client->dev.kobj, &lis2dw12_group);
    dev_info(&client->dev, "LIS2DW12 driver removed\n");
    return 0;
}

/* ================= DEVICE MATCH ================= */

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
        .name = "lis2dw12_custom",
        .of_match_table = lis2dw12_of_match,
    },
    .probe = lis2dw12_probe,
    .remove = lis2dw12_remove,
    .id_table = lis2dw12_id,
};

module_i2c_driver(lis2dw12_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sai");
MODULE_DESCRIPTION("LIS2DW12 Accelerometer Driver, SYSFS");

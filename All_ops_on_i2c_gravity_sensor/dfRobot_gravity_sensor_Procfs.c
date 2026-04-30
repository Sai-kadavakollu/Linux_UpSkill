#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

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

/* Global pointer for procfs */
static struct lis2dw12_data *g_data;

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

    /* 14-bit alignment */
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

/* ================= PROCFS ================= */

static int gravity_proc_show(struct seq_file *m, void *v)
{
    if (!g_data)
        return -ENODEV;

    lis2dw12_read_data(g_data);

    seq_printf(m, "[LIS2DW12] x=%d y=%d z=%d\n",
               g_data->x, g_data->y, g_data->z);

    return 0;
}

static int gravity_proc_open(struct inode *inode, struct file *file)
{
    return single_open(file, gravity_proc_show, NULL);
}

static const struct proc_ops gravity_proc_fops = {
    .proc_open    = gravity_proc_open,
    .proc_read    = seq_read,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

/* ================= PROBE ================= */

static int lis2dw12_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    struct lis2dw12_data *data;
    int chip_id;

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    i2c_set_clientdata(client, data);

    g_data = data;

    chip_id = lis2dw12_read8(client, LIS2DW12_WHO_AM_I);
    if (chip_id != 0x44) {
        dev_err(&client->dev, "LIS2DW12 not found\n");
        return -ENODEV;
    }

    /* Init sensor */
    lis2dw12_write(client, LIS2DW12_CTRL1, 0x54);

    /* Create proc entry */
    proc_create("gravity", 0, NULL, &gravity_proc_fops);

    dev_info(&client->dev, "LIS2DW12 procfs driver initialized\n");

    return 0;
}

/* ================= REMOVE ================= */

static int lis2dw12_remove(struct i2c_client *client)
{
    remove_proc_entry("gravity", NULL);
    
    dev_info(&client->dev, "LIS2DW12 procfs driver removed..\n");
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
        .name = "lis2dw12_proc",
        .of_match_table = lis2dw12_of_match,
    },
    .probe = lis2dw12_probe,
    .remove = lis2dw12_remove,
    .id_table = lis2dw12_id,
};

module_i2c_driver(lis2dw12_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sai");
MODULE_DESCRIPTION("LIS2DW12 Procfs Driver");

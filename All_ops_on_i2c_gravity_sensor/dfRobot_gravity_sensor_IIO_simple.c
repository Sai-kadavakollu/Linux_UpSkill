#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

/* ================= LIS2DW12 ================= */

#define LIS2DW12_CTRL1    0x20
#define LIS2DW12_OUT_X_L  0x28
#define LIS2DW12_OUT_Y_L  0x2A
#define LIS2DW12_OUT_Z_L  0x2C

/* ================= DRIVER DATA ================= */

struct lis2dw12_data {
    struct i2c_client *client;
};

/* ================= I2C READ ================= */

static int read16(struct i2c_client *c, u8 reg)
{
    int l = i2c_smbus_read_byte_data(c, reg);
    int h = i2c_smbus_read_byte_data(c, reg + 1);

    return ((short)((h << 8) | l)) >> 2;
}

/* ================= READ RAW ================= */
/*
static int lis2dw12_read_raw(struct iio_dev *indio_dev,
                             struct iio_chan_spec const *chan,
                             int *val, int *val2, long mask)
{
    struct lis2dw12_data *data = iio_priv(indio_dev);

    if (mask != IIO_CHAN_INFO_RAW)
        return -EINVAL;

    switch (chan->channel2) {

    case IIO_MOD_X:
        *val = read16(data->client, LIS2DW12_OUT_X_L);
        break;

    case IIO_MOD_Y:
        *val = read16(data->client, LIS2DW12_OUT_Y_L);
        break;

    case IIO_MOD_Z:
        *val = read16(data->client, LIS2DW12_OUT_Z_L);
        break;

    default:
        return -EINVAL;
    }

    return IIO_VAL_INT;
}
*/
static int lis2dw12_read_raw(struct iio_dev *indio_dev,
                             struct iio_chan_spec const *chan,
                             int *val, int *val2, long mask)
{
    struct lis2dw12_data *data = iio_priv(indio_dev);

    switch (mask) {

    case IIO_CHAN_INFO_RAW:

        switch (chan->channel2) {
        case IIO_MOD_X:
            *val = read16(data->client, LIS2DW12_OUT_X_L);
            break;

        case IIO_MOD_Y:
            *val = read16(data->client, LIS2DW12_OUT_Y_L);
            break;

        case IIO_MOD_Z:
            *val = read16(data->client, LIS2DW12_OUT_Z_L);
            break;

        default:
            return -EINVAL;
        }

        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:

        /* scale = 0.000244 g */
        *val = 0;
        *val2 = 244000;  // micro (0.000244)

        return IIO_VAL_INT_PLUS_MICRO;

    default:
        return -EINVAL;
    }
}

/* ================= CHANNELS ================= */
/*
static const struct iio_chan_spec lis2dw12_channels[] = {
    {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_X,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
    {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_Y,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
    {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_Z,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
};
*/
static const struct iio_chan_spec lis2dw12_channels[] = {
    {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_X,
        .info_mask_separate =
            BIT(IIO_CHAN_INFO_RAW) |
            BIT(IIO_CHAN_INFO_SCALE),
    },
    {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_Y,
        .info_mask_separate =
            BIT(IIO_CHAN_INFO_RAW) |
            BIT(IIO_CHAN_INFO_SCALE),
    },
    {
        .type = IIO_ACCEL,
        .modified = 1,
        .channel2 = IIO_MOD_Z,
        .info_mask_separate =
            BIT(IIO_CHAN_INFO_RAW) |
            BIT(IIO_CHAN_INFO_SCALE),
    },
};

/* ================= INFO ================= */

static const struct iio_info lis2dw12_info = {
    .read_raw = lis2dw12_read_raw,
};

/* ================= PROBE ================= */

static int lis2dw12_probe(struct i2c_client *client,
                         const struct i2c_device_id *id)
{
    struct iio_dev *indio_dev;
    struct lis2dw12_data *data;

    /* allocate IIO device */
    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);
    data->client = client;

    i2c_set_clientdata(client, indio_dev);

    /* sensor init */
    i2c_smbus_write_byte_data(client, LIS2DW12_CTRL1, 0x54);

    /* IIO setup */
    indio_dev->name = "lis2dw12";
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->info = &lis2dw12_info;
    indio_dev->channels = lis2dw12_channels;
    indio_dev->num_channels = ARRAY_SIZE(lis2dw12_channels);

    return devm_iio_device_register(&client->dev, indio_dev);
}

/* ================= REMOVE ================= */

static int lis2dw12_remove(struct i2c_client *client)
{
    return 0;
}

/* ================= MATCH ================= */

static const struct of_device_id lis2dw12_of_match[] = {
    { .compatible = "dfrobot,lis2dw12" },
    {}
};

MODULE_DEVICE_TABLE(of, lis2dw12_of_match);

/* ================= DRIVER ================= */

static struct i2c_driver lis2dw12_driver = {
    .driver = {
        .name = "lis2dw12_iio",
        .of_match_table = lis2dw12_of_match,
    },
    .probe = lis2dw12_probe,
    .remove = lis2dw12_remove,
};

module_i2c_driver(lis2dw12_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sai");

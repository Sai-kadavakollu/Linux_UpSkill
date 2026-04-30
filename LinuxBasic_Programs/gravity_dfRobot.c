/**********************************************************************************************
*FILE: gravity_dfRobot.c
*
*DESCRIPTION:
* Custom linux i2c driver for dfRobot gravity sensor for 3-axis
*Features:
* Reads the x, y and z axis acceleration
*
*FLOW:
* DTS -> I2C device -> driver match -> probe() -> read who_Am_I -> set the ODR and mode
* -> create sysfs -> user reads -> driver talks to sensor
***********************************************************************************************/

#include <linux/module.h>  /* module macros */
#include <linux/i2c.h>     /* I2C subsystem */
#include <linux/of.h>      /* device tree */
#include <linux/delay.h>   /*  msleep */


/* ============================ STEP 1: REGISTER DEFINITIONS ===========================*/

/* Gravity internal  register map */
#define LIS2DW12_WHO_AM_I 0x0F  //R
/*Init registers say : write 0x54 in CTRL1 register, ODR = 100Hz, MODE = HIGH POWER*/
#define LIS2DW12_CTRL1    0x20  //R/W
#define LIS2DW12_CTRL6    0x21  //R/W

/*output data of 3 axis can be retreived by reading the below registers*/
#define LIS2DW12_OUT_X_L  0x28  //R
#define LIS2DW12_OUT_X_H  0x29  //R
#define LIS2DW12_OUT_Y_L  0x2A  //R
#define LIS2DW12_OUT_Y_H  0x2B  //R
#define LIS2DW12_OUT_Z_L  0x2C  //R
#define LIS2DW12_OUT_Z_H  0x2D  //R

/*============================= STEP 2: DRIVER PRIVATE DATA ============================*/

struct lis2dw12_data {
	struct i2c_client *client;
 	int x;
	int y;
	int z;
};

/*============================= STEP 3: I2C READ/WRITE HELPER FUNCTIONS=================== */

static int lis2dw12_read8(struct i2c_client *client, u8 reg) {
	return i2c_smbus_read_byte_data(client, reg);
}

static int lis2dw12_read16(struct i2c_client *client, u8 reg) {
	int lsb = i2c_smbus_read_byte_data(client, reg);
	int msb = i2c_smbus_read_byte_data(client, reg + 1);
	return (msb << 8) | lsb;
}
static int lis2dw12_write(struct i2c_client *client, u8 reg, u8 val) {
	return i2c_smbus_write_byte_data(client, reg, val);
}

/*============================== STEP 4: 3-AXIS DATA READ================================= */

static void lis2dw12_read_device_data(struct lis2dw12_data *data) {
	struct i2c_client *c = data->client;
	
	data->x = lis2dw12_read16(c, LIS2DW12_OUT_X_L);
	data->y = lis2dw12_read16(c, LIS2DW12_OUT_Y_L);
	data->z = lis2dw12_read16(c, LIS2DW12_OUT_Z_L);
}

static ssize_t gravityRaw_show(struct device  *dev, struct device_attribute *attr, char *buf)
{
	struct lis2dw12_data *data = dev_get_drvdata(dev);
	lis2dw12_read_device_data(data);
	return sprintf(buf,"[LIS2DW12 Accelerometer] x = %d, y = %d, z = %d \n", data->x, data->y, data->z);
}
static DEVICE_ATTR_RO(gravityRaw);

static ssize_t gravityCooked_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct lis2dw12_data *data = dev_get_drvdata(dev);
	
	return sprintf(buf,"[LIS2DW12 Accelerometer]: Cooked data......... ");
}
static DEVICE_ATTR_RO(gravityCooked);

/*Group attributes */
static struct attribute *lis2dw12_attrs[] = {
	&dev_attr_gravityRaw.attr,
	&dev_attr_gravityCooked.attr,
	NULL,
};

static const struct attribute_group lis2dw12_group = {
	.attrs = lis2dw12_attrs,
};

/*============================STEP-5: PRBE===================================*/
/*Called when device matches driver */

static int lis2dw12_probe(struct i2c_client *client, const struct i2c_device_id *id) {
	struct lis2dw12_data *data;
	/*Allocate Memory */
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if(!data)
		return -ENOMEM;
	
	/* store client */
	data->client = client;

	lis2dw12_write(client, LIS2DW12_CTRL1, 0x54);
	lis2dw12_write(client, LIS2DW12_CTRL6, 0x00);
	/*Attach private data */
	i2c_set_clientdata(client, data);
	
	/*Read device  identity */
	int chip_id;
        chip_id= lis2dw12_read8(client, LIS2DW12_WHO_AM_I);

	if(chip_id != 0x44) {
		dev_err(&client->dev, "LIS2DW12 : Device not found \n");
		return -ENODEV;
	}
	int ret;
	ret = sysfs_create_group(&client->dev.kobj, &lis2dw12_group);	
        if (ret) return ret;
	dev_info(&client->dev, "LIS2DW12 : detected \n");
	
	return 0;
}

static const struct i2c_device_id lis2dw12_id[] = {
	{ "lis2dw12", 0}, 
	{}
};

MODULE_DEVICE_TABLE(i2c, lis2dw12_id);
/*============================== REMOVE ==================================*/
static void lis2dw12_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &lis2dw12_group);
}

/*===============================DEVICE MATCH =============================*/

static const struct of_device_id lis2dw12_of_match[] = {
	{ .compatible = "dfRobot,lis2dw12" },
	{}
};
MODULE_DEVICE_TABLE(of, lis2dw12_of_match);
/*==============================DRIVER STRUCT =============================*/

static struct i2c_driver lis2dw12_driver = {
	.driver = {
	    .name = "lis2dw12_custom",
	    .of_match_table = lis2dw12_of_match,
	},
	.probe = lis2dw12_probe,
	.remove = lis2dw12_remove,
	.id_table = lis2dw12_id,
};
/*===============================REGISTER DRIVER ==========================*/

module_i2c_driver(lis2dw12_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SaiKadavakollu");

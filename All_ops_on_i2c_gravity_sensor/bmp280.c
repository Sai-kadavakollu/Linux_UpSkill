// SPDX-License-Identifier: GPL-2.0.0
/*
 * bmp280_custom.c - BMP280 I2C Temperature & Pressure Sensor Driver
 *
 * Target:  BeagleBone Black (AM335x) with BMP280 on I2C2 bus
 * Wiring:  SDA = P9.20, SCL = P9.19, VCC = 3.3V, GND = GND
 *          SDO → GND (selects I2C address 0x76)
 *          GPIO44 (P8.12) used as manual interrupt trigger
 *
 * Features:
 *   - Character device:  /dev/bmp280  (ioctl for temp & pressure)
 *   - Sysfs attributes:  /sys/bus/i2c/devices/2-0076/temp
 *                         /sys/bus/i2c/devices/2-0076/pressure
 *   - Blocking read():   Sleeps until GPIO44 interrupt fires
 *   - Mutex:             Serializes concurrent I2C access
 *
 * Manual interrupt trigger (simulate from shell):
 *   echo 44 > /sys/class/gpio/export
 *   echo out > /sys/class/gpio/gpio44/direction
 *   echo 1 > /sys/class/gpio/gpio44/value
 *   echo 0 > /sys/class/gpio/gpio44/value   ← falling edge fires IRQ
 *
 * Author: Sai Kadavakollu
 */


/* ================================================================
 * BLOCK 1: INCLUDES
 *
 * Each header serves a specific purpose in the driver:
 *   module.h      → MODULE_LICENSE, module_i2c_driver, etc.
 *   i2c.h         → i2c_client, i2c_driver, i2c_smbus_* functions
 *   of.h          → of_device_id, device tree matching
 *   kernel.h      → pr_info, pr_err, container_of
 *   math64.h      → div64_s64 (64-bit division for pressure calc)
 *   ioctl.h       → _IOR macro for ioctl command definitions
 *   fs.h          → file_operations, alloc_chrdev_region
 *   cdev.h        → cdev_init, cdev_add, cdev_del
 *   device.h      → class_create, device_create, sysfs
 *   delay.h       → msleep (sleep in milliseconds)
 *   uaccess.h     → copy_to_user (safe kernel→user data copy)
 *   mutex.h       → mutex_init, mutex_lock, mutex_unlock
 *   wait.h        → wait_queue_head_t, wait_event_interruptible
 *   interrupt.h   → request_irq, free_irq, irqreturn_t
 *   gpio.h        → gpio_to_irq (map GPIO number to IRQ number)
 * ================================================================ */
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
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>


/* ================================================================
 * BLOCK 2: IOCTL COMMAND DEFINITIONS
 *
 * These MUST match the userspace application header exactly.
 *
 * _IOR(type, nr, datatype) builds a 32-bit command number:
 *   Bits 31-30: direction (10 = read from device)
 *   Bits 29-16: size of data (sizeof(int) = 4)
 *   Bits 15-8:  type ('B' = 0x42)
 *   Bits 7-0:   command number (1 or 2)
 *
 * When userspace calls ioctl(fd, BMP280_GET_TEMP, &val):
 *   - Kernel matches cmd number in our bmp280_ioctl switch
 *   - No "registration" or "linking" is needed
 *   - The link is: fd → file → f_op → unlocked_ioctl → our function
 * ================================================================ */
#define BMP280_IOCTL_BASE       'B'
#define BMP280_GET_TEMP         _IOR(BMP280_IOCTL_BASE, 1, int)
#define BMP280_GET_PRESSURE     _IOR(BMP280_IOCTL_BASE, 2, int)


/* ================================================================
 * BLOCK 3: BMP280 REGISTER DEFINITIONS
 *
 * Register map (from Bosch BMP280 datasheet):
 *   0x88-0x9F : Calibration data (24 bytes, factory-programmed)
 *   0xD0      : Chip ID (should read 0x58)
 *   0xE0      : Reset register
 *   0xF3      : Status (bit 3 = measuring, bit 0 = updating)
 *   0xF4      : ctrl_meas (oversampling + power mode)
 *   0xF5      : config (standby time, filter, SPI)
 *   0xF7-0xF9 : Pressure data (20-bit, MSB first)
 *   0xFA-0xFC : Temperature data (20-bit, MSB first)
 * ================================================================ */

/* ctrl_meas register (0xF4):
 *   Bits 7-5: osrs_t  = 001 → temp oversampling ×1  (16-bit, 0.0050°C resolution)
 *   Bits 4-2: osrs_p  = 001 → pressure oversampling ×1 (16-bit, 2.62 Pa resolution)
 *   Bits 1-0: mode    = 11  → Normal mode (continuous measurement)
 *   Combined: 0b00100111 = 0x27
 */
#define BMP280_REG_CTRL_MEAS            0xF4
#define BMP280_CTRL_MEAS_VALUE          0x27

/* config register (0xF5):
 *   Bits 7-5: t_sb    = 101 → Standby time 1000ms between measurements
 *   Bits 4-2: filter  = 000 → Filter off
 *   Bit  0:   spi3w_en= 0   → SPI 3-wire disabled (we use I2C)
 *   Combined: 0b10100000 = 0xA0
 */
#define BMP280_REG_CONFIG               0xF5
#define BMP280_CONFIG_VALUE             0xA0

/* Data registers: each reading is 3 bytes (20-bit value) */
#define BMP280_REG_PRESSURE             0xF7    /* press_msb[19:12], _lsb[11:4], _xlsb[3:0] */
#define BMP280_REG_TEMP                 0xFA    /* temp_msb[19:12],  _lsb[11:4], _xlsb[3:0] */
#define BMP280_DATA_LEN                 3       /* 3 bytes per reading */

/* Calibration data: 24 bytes starting at 0x88 */
#define BMP280_REG_CALIB_START          0x88
#define BMP280_CALIB_LEN                24

/* GPIO for manual interrupt trigger (P8.12 on BeagleBone Black)
 *
 * GPIO44 = GPIO1_12
 *   GPIO bank 1 base = 32
 *   Pin 12 within bank 1
 *   So: 32 + 12 = 44
 *
 * BMP280 has NO interrupt pin. We use GPIO44 as a manual trigger
 * to learn interrupt-driven driver concepts. Toggle from userspace:
 *   echo 0 > /sys/class/gpio/gpio44/value  (creates falling edge)
 */
#define BMP280_GPIO_IRQ                 44


/* ================================================================
 * BLOCK 4: DATA STRUCTURES
 *
 * bmp280_calib: Factory calibration coefficients.
 *   - Read once during probe from registers 0x88-0x9F
 *   - Used in compensation formulas (Bosch datasheet Section 8.1)
 *   - dig_T1 is unsigned, dig_T2/T3 are signed
 *   - dig_P1 is unsigned, dig_P2-P9 are signed
 *
 * bmp280_data: Per-device driver state.
 *   - client:     I2C client handle (has bus adapter + slave address)
 *   - calib:      Calibration coefficients
 *   - t_fine:     Intermediate temperature used in pressure formula
 *   - cdev:       Character device (maps major:minor → file_operations)
 *   - lock:       Mutex to serialize I2C bus access
 *   - wq:         Wait queue for blocking read()
 *   - data_ready: Flag set by IRQ handler, consumed by read()
 *   - irq:        Linux IRQ number mapped from GPIO44
 * ================================================================ */
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
	wait_queue_head_t wq;
	int data_ready;
	int irq;
};

/* Global char device number and class (shared across instances) */
static dev_t bmp280_devno;
static struct class *bmp280_class;


/* ================================================================
 * BLOCK 5: CALIBRATION READ
 *
 * Called once during probe. Reads 24 bytes from registers 0x88-0x9F.
 *
 * BMP280 stores calibration in little-endian:
 *   buf[0] = low byte of dig_T1
 *   buf[1] = high byte of dig_T1
 *   ... and so on
 *
 * We reconstruct each 16-bit value as: high_byte << 8 | low_byte
 *
 * Why i2c_smbus_read_i2c_block_data()?
 *   - Reads multiple consecutive bytes in one I2C transaction
 *   - On the wire: S|0x76+W|0x88|Sr|0x76+R|b0|b1|...|b23|P
 *   - More efficient than 24 individual byte reads
 *   - Auto-increments the register address inside BMP280
 * ================================================================ */
static int bmp280_read_calib(struct bmp280_data *data)
{
	u8 buf[BMP280_CALIB_LEN];
	struct i2c_client *c = data->client;
	int ret;

	ret = i2c_smbus_read_i2c_block_data(c, BMP280_REG_CALIB_START,
					     BMP280_CALIB_LEN, buf);
	if (ret < 0) {
		dev_err(&c->dev, "Failed to read calibration data (err=%d)\n", ret);
		return ret;
	}

	/* Temperature calibration (3 coefficients) */
	data->calib.dig_T1 = (u16)(buf[1] << 8 | buf[0]);
	data->calib.dig_T2 = (s16)(buf[3] << 8 | buf[2]);
	data->calib.dig_T3 = (s16)(buf[5] << 8 | buf[4]);

	/* Pressure calibration (9 coefficients) */
	data->calib.dig_P1 = (u16)(buf[7]  << 8 | buf[6]);
	data->calib.dig_P2 = (s16)(buf[9]  << 8 | buf[8]);
	data->calib.dig_P3 = (s16)(buf[11] << 8 | buf[10]);
	data->calib.dig_P4 = (s16)(buf[13] << 8 | buf[12]);
	data->calib.dig_P5 = (s16)(buf[15] << 8 | buf[14]);
	data->calib.dig_P6 = (s16)(buf[17] << 8 | buf[16]);
	data->calib.dig_P7 = (s16)(buf[19] << 8 | buf[18]);
	data->calib.dig_P8 = (s16)(buf[21] << 8 | buf[20]);
	data->calib.dig_P9 = (s16)(buf[23] << 8 | buf[22]);

	dev_info(&c->dev, "Calibration loaded: T1=%u T2=%d T3=%d\n",
		 data->calib.dig_T1, data->calib.dig_T2, data->calib.dig_T3);

	return 0;
}


/* ================================================================
 * BLOCK 6: SENSOR CONFIGURATION
 *
 * Writes two registers to set operating mode:
 *
 * i2c_smbus_write_byte_data(client, register, value):
 *   - Sends: S | addr+W | register | value | P
 *   - Single I2C write transaction
 *   - Returns 0 on success, negative errno on failure
 *
 * Note: ctrl_meas MUST be written AFTER config, because writing
 * ctrl_meas with mode != 00 starts measurement immediately.
 * If config is written after, the standby/filter settings may
 * not apply to the first measurement cycle.
 * ================================================================ */
static int bmp280_configure(struct bmp280_data *data)
{
	struct i2c_client *c = data->client;
	int ret;

	/* Write config first (standby time + filter) */
	ret = i2c_smbus_write_byte_data(c, BMP280_REG_CONFIG,
					BMP280_CONFIG_VALUE);
	if (ret < 0) {
		dev_err(&c->dev, "Failed to write config register\n");
		return ret;
	}

	/* Write ctrl_meas (oversampling + normal mode → starts measuring) */
	ret = i2c_smbus_write_byte_data(c, BMP280_REG_CTRL_MEAS,
					BMP280_CTRL_MEAS_VALUE);
	if (ret < 0) {
		dev_err(&c->dev, "Failed to write ctrl_meas register\n");
		return ret;
	}

	dev_info(&c->dev, "Sensor configured: ctrl=0x%02x cfg=0x%02x\n",
		 BMP280_CTRL_MEAS_VALUE, BMP280_CONFIG_VALUE);
	return 0;
}


/* ================================================================
 * BLOCK 7: TEMPERATURE READ + COMPENSATION
 *
 * Reads 3 bytes from 0xFA-0xFC → 20-bit raw ADC value.
 * Applies Bosch compensation formula (datasheet Section 8.1).
 *
 * Returns temperature in units of 0.01°C:
 *   Return value 5123 → 51.23°C
 *   Return value 2500 → 25.00°C
 *
 * IMPORTANT: This function also computes data->t_fine which is
 * required by the pressure compensation. Always read temperature
 * BEFORE pressure, or pressure values will be wrong.
 *
 * Compensation formula (all integer arithmetic):
 *   var1 = ((adc_T/8 - dig_T1*2) * dig_T2) / 2048
 *   var2 = ((adc_T/16 - dig_T1)² / 4096 * dig_T3) / 16384
 *   t_fine = var1 + var2
 *   temperature = (t_fine * 5 + 128) / 256
 * ================================================================ */
static int bmp280_read_temp(struct bmp280_data *data)
{
	u8 buf[BMP280_DATA_LEN];
	int adc_T, var1, var2;
	struct i2c_client *c = data->client;
	int ret;

	/* Read 3 raw bytes: [MSB, LSB, XLSB] */
	ret = i2c_smbus_read_i2c_block_data(c, BMP280_REG_TEMP,
					     BMP280_DATA_LEN, buf);
	if (ret < 0) {
		dev_err(&c->dev, "Failed to read temperature\n");
		return ret;
	}

	/*
	 * Assemble 20-bit raw value:
	 *   buf[0] = MSB  → bits 19:12
	 *   buf[1] = LSB  → bits 11:4
	 *   buf[2] = XLSB → bits 3:0 (upper nibble only)
	 */
	adc_T = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);

	/* Bosch compensation formula */
	var1 = ((((adc_T >> 3) - ((int)data->calib.dig_T1 << 1))) *
		((int)data->calib.dig_T2)) >> 11;

	var2 = (((((adc_T >> 4) - ((int)data->calib.dig_T1)) *
		  ((adc_T >> 4) - ((int)data->calib.dig_T1))) >> 12) *
		((int)data->calib.dig_T3)) >> 14;

	/* t_fine is shared with pressure compensation */
	data->t_fine = var1 + var2;

	/* Convert to 0.01°C units */
	return (data->t_fine * 5 + 128) >> 8;
}


/* ================================================================
 * BLOCK 8: PRESSURE READ + COMPENSATION
 *
 * Reads 3 bytes from 0xF7-0xF9 → 20-bit raw ADC value.
 * Applies Bosch 64-bit compensation formula.
 *
 * PREREQUISITE: bmp280_read_temp() must be called first to
 * compute data->t_fine, which is used here.
 *
 * Returns pressure in Q24.8 format (Pa * 256):
 *   Return value 25600000 → 100000 Pa → 1000.00 hPa
 *   Divide by 256 to get Pa
 *   Divide by 25600 to get hPa (mbar)
 *
 * Why 64-bit (s64)?
 *   The intermediate values in pressure compensation overflow
 *   32-bit integers. The Bosch formula requires at least 64-bit
 *   arithmetic for correct results.
 *
 * Why div64_s64()?
 *   ARM32 has no native 64-bit division instruction. The compiler
 *   would call __aeabi_ldivmod which is not available in the kernel.
 *   div64_s64() is the kernel's portable 64÷64 division helper
 *   from <linux/math64.h>.
 * ================================================================ */
static int bmp280_read_pressure(struct bmp280_data *data)
{
	u8 buf[BMP280_DATA_LEN];
	int adc_P;
	struct i2c_client *c = data->client;
	s64 var1, var2, p;
	int ret;

	/* Read 3 raw bytes: [MSB, LSB, XLSB] */
	ret = i2c_smbus_read_i2c_block_data(c, BMP280_REG_PRESSURE,
					     BMP280_DATA_LEN, buf);
	if (ret < 0) {
		dev_err(&c->dev, "Failed to read pressure\n");
		return ret;
	}

	/* Assemble 20-bit raw value (same bit layout as temperature) */
	adc_P = (buf[0] << 12) | (buf[1] << 4) | (buf[2] >> 4);

	/* Bosch 64-bit compensation formula */
	var1 = (s64)data->t_fine - 128000;
	var2 = var1 * var1 * (s64)data->calib.dig_P6;
	var2 = var2 + ((var1 * (s64)data->calib.dig_P5) << 17);
	var2 = var2 + (((s64)data->calib.dig_P4) << 35);

	var1 = ((var1 * var1 * (s64)data->calib.dig_P3) >> 8) +
	       ((var1 * (s64)data->calib.dig_P2) << 12);
	var1 = ((((s64)1 << 47) + var1) * (s64)data->calib.dig_P1) >> 33;

	/* Avoid division by zero */
	if (var1 == 0)
		return 0;

	p = 1048576 - adc_P;
	p = div64_s64(((p << 31) - var2) * 3125, var1);

	var1 = ((s64)data->calib.dig_P9 * (p >> 13) * (p >> 13)) >> 25;
	var2 = ((s64)data->calib.dig_P8 * p) >> 19;

	/* Result is Pa in Q24.8 format */
	p = ((p + var1 + var2) >> 8) + (((s64)data->calib.dig_P7) << 4);

	return (int)p;
}


/* ================================================================
 * BLOCK 9: CHARACTER DEVICE - open()
 *
 * Called when userspace does: fd = open("/dev/bmp280", O_RDWR);
 *
 * Flow:
 *   1. VFS resolves path → inode → gets major:minor numbers
 *   2. Looks up cdev_map[major:minor] → finds our struct cdev
 *   3. Gets cdev->ops = &bmp280_fops
 *   4. Calls fops->open(inode, file) = this function
 *
 * container_of() is the KEY macro here:
 *   - We know inode->i_cdev points to the cdev member
 *   - container_of() computes: (address of cdev) - (offset of cdev in struct)
 *   - This gives us the address of the enclosing bmp280_data struct
 *   - We store it in file->private_data for later use
 *
 * After this, every read/ioctl call gets bmp280_data via:
 *   struct bmp280_data *data = file->private_data;
 * ================================================================ */
static int bmp280_open(struct inode *inode, struct file *file)
{
	struct bmp280_data *data;

	data = container_of(inode->i_cdev, struct bmp280_data, cdev);
	file->private_data = data;

	pr_info("BMP280: Device opened by process '%s' (pid=%d)\n",
		current->comm, current->pid);
	return 0;
}


/* ================================================================
 * BLOCK 10: CHARACTER DEVICE - read() [BLOCKING]
 *
 * Called when userspace does: read(fd, buf, sizeof(values));
 *
 * This is a BLOCKING read:
 *   1. Process calls wait_event_interruptible()
 *   2. Kernel checks: is data_ready == 1?
 *      - YES → continue immediately
 *      - NO  → set process state to TASK_INTERRUPTIBLE
 *              add to wait queue, remove from run queue
 *              call schedule() → CPU runs other tasks
 *   3. Process SLEEPS until:
 *      - IRQ handler sets data_ready=1 and calls wake_up_interruptible()
 *      - OR a signal is delivered (returns -ERESTARTSYS)
 *   4. Scheduler moves process back to TASK_RUNNING
 *   5. We read sensor data under mutex protection
 *   6. copy_to_user() safely copies kernel buffer to userspace
 *
 * Why mutex around I2C reads?
 *   Multiple processes may have /dev/bmp280 open simultaneously.
 *   I2C is a shared bus — interleaved transactions corrupt data.
 *   mutex_lock() makes other processes sleep until we finish.
 * ================================================================ */
static ssize_t bmp280_read(struct file *file, char __user *buf,
			    size_t count, loff_t *ppos)
{
	struct bmp280_data *data = file->private_data;
	int values[2]; /* [0]=temperature, [1]=pressure */
	int ret;

	/* Block until interrupt fires */
	ret = wait_event_interruptible(data->wq, data->data_ready != 0);
	if (ret)
		return -ERESTARTSYS; /* Signal interrupted us */

	mutex_lock(&data->lock);

	/* Read temperature first (computes t_fine for pressure) */
	values[0] = bmp280_read_temp(data);
	values[1] = bmp280_read_pressure(data);

	/* Reset flag so next read() blocks again */
	data->data_ready = 0;

	mutex_unlock(&data->lock);

	/* Ensure userspace buffer is large enough */
	if (count < sizeof(values))
		return -EINVAL;

	if (copy_to_user(buf, values, sizeof(values)))
		return -EFAULT;

	return sizeof(values);
}


/* ================================================================
 * BLOCK 11: CHARACTER DEVICE - ioctl()
 *
 * Called when userspace does: ioctl(fd, BMP280_GET_TEMP, &val);
 *
 * HOW IOCTL LINKS WITHOUT EXPLICIT REGISTRATION:
 *   There is NO ioctl "registration" system. The linkage is:
 *
 *   1. During probe: cdev_init(&cdev, &bmp280_fops)
 *      → cdev->ops = &bmp280_fops
 *
 *   2. cdev_add() inserts into cdev_map hash table:
 *      → key: major:minor → value: &cdev
 *
 *   3. device_create() triggers mdev to create /dev/bmp280
 *      → /dev/bmp280 inode stores major:minor
 *
 *   4. open() looks up cdev_map[major:minor] → gets fops
 *      → stores fops in file->f_op
 *
 *   5. ioctl() simply calls file->f_op->unlocked_ioctl()
 *      → which is bmp280_ioctl()
 *
 *   The command number (BMP280_GET_TEMP) is just a magic number.
 *   Kernel doesn't validate it — our switch statement does.
 *   If userspace sends wrong cmd → we return -EINVAL.
 * ================================================================ */
static long bmp280_ioctl(struct file *file, unsigned int cmd,
			  unsigned long arg)
{
	struct bmp280_data *data = file->private_data;
	int value;

	mutex_lock(&data->lock);

	switch (cmd) {
	case BMP280_GET_TEMP:
		/* Read temperature first (updates t_fine) */
		value = bmp280_read_temp(data);
		break;

	case BMP280_GET_PRESSURE:
		/*
		 * Read temp first to update t_fine, then pressure.
		 * Pressure compensation depends on t_fine.
		 */
		bmp280_read_temp(data);
		value = bmp280_read_pressure(data);
		break;

	default:
		mutex_unlock(&data->lock);
		return -EINVAL;
	}

	mutex_unlock(&data->lock);

	/* Copy result to userspace pointer */
	if (copy_to_user((int __user *)arg, &value, sizeof(value)))
		return -EFAULT;

	return 0;
}


/* ================================================================
 * BLOCK 12: INTERRUPT HANDLER
 *
 * Runs in INTERRUPT CONTEXT (hardirq):
 *   - Cannot sleep (no mutex_lock, no kmalloc with GFP_KERNEL)
 *   - Cannot call any function that might sleep
 *   - Should be as fast as possible
 *   - Can use spin_lock (not mutex) if needed
 *
 * Triggered by: falling edge on GPIO44 (P8.12)
 *
 * What happens internally when GPIO44 goes low:
 *   1. GPIO1 bank hardware detects falling edge on bit 12
 *   2. GPIO1 sets IRQSTATUS_0 register bit 12
 *   3. GPIO1 asserts interrupt line to AM335x INTC
 *   4. INTC interrupts ARM core (IRQ exception)
 *   5. ARM saves context, jumps to exception vector
 *   6. Kernel's generic IRQ handler reads INTC to find IRQ number
 *   7. Looks up irq_desc[irq] → finds our handler
 *   8. Calls bmp280_irq_handler()
 *   9. We set flag and wake sleeping processes
 *  10. Returns IRQ_HANDLED to tell kernel we consumed this IRQ
 * ================================================================ */
static irqreturn_t bmp280_irq_handler(int irq, void *dev_id)
{
	struct bmp280_data *data = dev_id;

	pr_info("BMP280: IRQ %d fired! (GPIO44 falling edge)\n", irq);

	/* Set flag — consumed by read() after waking */
	data->data_ready = 1;

	/* Wake all processes sleeping in wait_event_interruptible() */
	wake_up_interruptible(&data->wq);

	return IRQ_HANDLED;
}


/* ================================================================
 * BLOCK 13: SYSFS ATTRIBUTE HANDLERS
 *
 * sysfs provides a filesystem interface under /sys/ for exposing
 * driver data to userspace. No ioctl or char device needed.
 *
 * How it works internally:
 *   1. Each struct device has a kobject (kernel object)
 *   2. kobject = a directory in /sys
 *   3. We call sysfs_create_group() to add files to this directory
 *   4. Each file is a struct device_attribute with:
 *      - .attr.name = filename (e.g., "temp")
 *      - .attr.mode = permissions (0444 for read-only)
 *      - .show = function called on read
 *      - .store = function called on write (NULL for RO)
 *
 * When user does: cat /sys/bus/i2c/devices/2-0076/temp
 *   1. VFS → sysfs → sysfs_kf_seq_show()
 *   2. Calls dev_attr_temp.show = temp_show()
 *   3. We read sensor, format string into buf
 *   4. sysfs sends buf contents to userspace
 *
 * DEVICE_ATTR_RO(temp) expands to:
 *   struct device_attribute dev_attr_temp = {
 *       .attr = { .name = "temp", .mode = 0444 },
 *       .show = temp_show,
 *   };
 *
 * The "_RO" suffix means read-only. Other variants:
 *   DEVICE_ATTR_RW(name) → read + write
 *   DEVICE_ATTR_WO(name) → write only
 * ================================================================ */
static ssize_t temp_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct bmp280_data *data = dev_get_drvdata(dev);
	int t;

	mutex_lock(&data->lock);
	t = bmp280_read_temp(data);
	mutex_unlock(&data->lock);

	/* Format: "25.12\n" (degrees Celsius with 2 decimal places) */
	return sprintf(buf, "%d.%02d\n", t / 100, abs(t % 100));
}

static ssize_t pressure_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct bmp280_data *data = dev_get_drvdata(dev);
	int p;

	mutex_lock(&data->lock);
	bmp280_read_temp(data);  /* Must read temp first for t_fine */
	p = bmp280_read_pressure(data);
	mutex_unlock(&data->lock);

	/* Pressure in Pa (Q24.8 format, divide by 256 for true Pa) */
	return sprintf(buf, "%d.%02d\n", p / 256, abs((p % 256) * 100 / 256));
}

static DEVICE_ATTR_RO(temp);
static DEVICE_ATTR_RO(pressure);

/* Attribute group — array of pointers to attributes, NULL-terminated */
static struct attribute *bmp280_attrs[] = {
	&dev_attr_temp.attr,
	&dev_attr_pressure.attr,
	NULL,   /* Sentinel — kernel iterates until NULL */
};

static const struct attribute_group bmp280_attr_group = {
	.attrs = bmp280_attrs,
};


/* ================================================================
 * BLOCK 14: FILE OPERATIONS STRUCTURE
 *
 * This is THE central dispatch table. It maps VFS operations to
 * our driver functions:
 *
 *   file->f_op->open           → bmp280_open
 *   file->f_op->read           → bmp280_read
 *   file->f_op->unlocked_ioctl → bmp280_ioctl
 *
 * The VFS (Virtual File System) treats /dev/bmp280 like any file.
 * When a system call targets our device, VFS looks up f_op and
 * calls the appropriate function pointer. Operations we don't
 * implement (write, mmap, etc.) return -EINVAL by default.
 * ================================================================ */
static const struct file_operations bmp280_fops = {
	.owner          = THIS_MODULE,
	.open           = bmp280_open,
	.read           = bmp280_read,
	.unlocked_ioctl = bmp280_ioctl,
};


/* ================================================================
 * BLOCK 15: PROBE FUNCTION
 *
 * Called by the I2C core when driver matches a device tree node.
 *
 * How probe gets called (the full chain):
 *
 *   module_i2c_driver(bmp280_driver)
 *     ↓ expands to
 *   module_init() → i2c_add_driver(&bmp280_driver)
 *     ↓
 *   driver_register(&bmp280_driver.driver)
 *     ↓
 *   bus_for_each_dev(i2c_bus_type, ...) → tries matching
 *     ↓
 *   i2c_device_match() checks:
 *     a) OF match: driver's .of_match_table vs DT compatible string
 *        "sai,bmp280" == "sai,bmp280" ✓
 *     b) If matched → driver_probe_device()
 *     ↓
 *   driver->probe(client, id) = bmp280_probe()
 *
 * The i2c_client* received here was created by the kernel when it
 * parsed the device tree and found the bmp280@76 node. It contains:
 *   - client->addr = 0x76 (from DT "reg = <0x76>")
 *   - client->adapter = I2C2 adapter (from parent &i2c2 node)
 *   - client->dev = device struct (for devm, sysfs, etc.)
 *   - client->irq = mapped from DT interrupts property
 *
 * For BUILT-IN drivers (obj-y in Makefile):
 *   - Driver code is compiled INTO vmlinux (no .ko file)
 *   - module_i2c_driver expands to device_initcall()
 *   - Called during kernel boot at init level 6
 *   - do_initcalls() iterates .initcall6.init section
 *   - Finds and calls our __init function
 *   - Same registration flow, just triggered at boot not insmod
 * ================================================================ */
static int bmp280_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct bmp280_data *data;
	int ret;

	dev_info(&client->dev, "BMP280 probe started (addr=0x%02x)\n",
		 client->addr);

	/* --- Allocate driver data --- */
	/*
	 * devm_kzalloc: "device-managed" allocation
	 *   - Zeroes memory (kzalloc = kmalloc + memset 0)
	 *   - Automatically freed when device is removed
	 *   - No need for explicit kfree() in remove()
	 *   - GFP_KERNEL: can sleep (we're in process context)
	 */
	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;

	/*
	 * i2c_set_clientdata: stores data pointer in client->dev.driver_data
	 * Later retrievable via i2c_get_clientdata(client)
	 * This is how remove() finds our data structure.
	 */
	i2c_set_clientdata(client, data);

	/* --- Initialize synchronization primitives --- */
	mutex_init(&data->lock);
	init_waitqueue_head(&data->wq);
	data->data_ready = 0;

	/* --- Read calibration and configure sensor --- */
	ret = bmp280_read_calib(data);
	if (ret < 0)
		return ret;

	ret = bmp280_configure(data);
	if (ret < 0)
		return ret;

	/* Wait for first measurement to complete */
	msleep(100);

	/* --- Set up character device --- */
	/*
	 * alloc_chrdev_region: asks kernel to assign a major number
	 *   - &bmp280_devno: receives the assigned dev_t (major:minor)
	 *   - 0: first minor number
	 *   - 1: number of minor numbers to allocate
	 *   - "bmp280": name shown in /proc/devices
	 *
	 * After this: /proc/devices contains "243 bmp280" (or similar)
	 */
	ret = alloc_chrdev_region(&bmp280_devno, 0, 1, "bmp280");
	if (ret < 0) {
		dev_err(&client->dev, "Failed to allocate chrdev region\n");
		return ret;
	}

	/*
	 * cdev_init: links our cdev to the file_operations table
	 *   - After this: data->cdev.ops = &bmp280_fops
	 *
	 * cdev_add: inserts cdev into the kernel's cdev_map
	 *   - Hash table: major:minor → struct cdev → file_operations
	 *   - This is how open() finds our fops from /dev/bmp280's inode
	 */
	cdev_init(&data->cdev, &bmp280_fops);
	data->cdev.owner = THIS_MODULE;
	ret = cdev_add(&data->cdev, bmp280_devno, 1);
	if (ret < 0) {
		unregister_chrdev_region(bmp280_devno, 1);
		return ret;
	}

	/*
	 * class_create: creates /sys/class/bmp280_class/
	 *   - A "class" groups similar devices
	 *   - Used by mdev (Buildroot's udev replacement) to create /dev nodes
	 *
	 * device_create: creates /sys/class/bmp280_class/bmp280/
	 *   - Generates a uevent (uevent = userspace event)
	 *   - mdev receives uevent and creates /dev/bmp280 automatically
	 *   - Also stores major:minor in /sys/.../dev file
	 */
	bmp280_class = class_create(THIS_MODULE, "bmp280_class");
	if (IS_ERR(bmp280_class)) {
		cdev_del(&data->cdev);
		unregister_chrdev_region(bmp280_devno, 1);
		return PTR_ERR(bmp280_class);
	}

	device_create(bmp280_class, NULL, bmp280_devno, NULL, "bmp280");

	/* --- Set up sysfs attributes --- */
	ret = sysfs_create_group(&client->dev.kobj, &bmp280_attr_group);
	if (ret) {
		dev_err(&client->dev, "Failed to create sysfs group\n");
		goto err_sysfs;
	}

	/* --- Set up GPIO44 interrupt --- */
	/*
	 * gpio_to_irq: maps GPIO number → Linux IRQ number
	 *
	 * Internally:
	 *   GPIO44 = GPIO bank 1, pin 12
	 *   AM335x GPIO1 is wired to INTC IRQ lines
	 *   Kernel maintains a mapping: gpio_chip→irq_domain→irq_desc
	 *   Returns the virtual IRQ number (e.g., 172)
	 *
	 * Alternative (if using device tree):
	 *   client->irq is auto-populated from DT interrupt property
	 *   But since BMP280 has no real interrupt pin, we do it manually.
	 */
	data->irq = gpio_to_irq(BMP280_GPIO_IRQ);
	if (data->irq < 0) {
		dev_err(&client->dev, "Failed to map GPIO%d to IRQ\n",
			BMP280_GPIO_IRQ);
		ret = data->irq;
		goto err_irq;
	}

	/*
	 * request_irq: registers our handler for this IRQ line
	 *   - data->irq: Linux IRQ number
	 *   - bmp280_irq_handler: function to call
	 *   - IRQF_TRIGGER_FALLING: fire on 1→0 transition
	 *   - "bmp280_irq": name in /proc/interrupts
	 *   - data: passed as dev_id to handler
	 *
	 * After this, /proc/interrupts shows:
	 *   172:  0  GPIO1  12 Edge  bmp280_irq
	 */
	ret = request_irq(data->irq, bmp280_irq_handler,
			  IRQF_TRIGGER_FALLING, "bmp280_irq", data);
	if (ret) {
		dev_err(&client->dev, "Failed to request IRQ %d\n", data->irq);
		goto err_irq;
	}

	dev_info(&client->dev,
		 "BMP280 driver loaded: /dev/bmp280, IRQ=%d (GPIO%d)\n",
		 data->irq, BMP280_GPIO_IRQ);
	return 0;

err_irq:
	sysfs_remove_group(&client->dev.kobj, &bmp280_attr_group);
err_sysfs:
	device_destroy(bmp280_class, bmp280_devno);
	class_destroy(bmp280_class);
	cdev_del(&data->cdev);
	unregister_chrdev_region(bmp280_devno, 1);
	return ret;
}


/* ================================================================
 * BLOCK 16: REMOVE FUNCTION
 *
 * Called when:
 *   - Module is unloaded: rmmod bmp280_custom
 *   - Device is physically removed (hot-unplug, if supported)
 *   - Driver is unbound via sysfs: echo "2-0076" > /sys/bus/i2c/drivers/.../unbind
 *   - System shutdown
 *
 * Must undo everything probe() did, in REVERSE order:
 *   probe: alloc → calib → config → chrdev → class → sysfs → irq
 *   remove: irq → sysfs → device → class → chrdev → unregister
 *
 * Note: devm_kzalloc memory is freed automatically by the driver
 * core after remove() returns — no explicit kfree needed.
 * ================================================================ */
static int bmp280_remove(struct i2c_client *client)
{
	struct bmp280_data *data = i2c_get_clientdata(client);

	free_irq(data->irq, data);
	sysfs_remove_group(&client->dev.kobj, &bmp280_attr_group);
	device_destroy(bmp280_class, bmp280_devno);
	class_destroy(bmp280_class);
	cdev_del(&data->cdev);
	unregister_chrdev_region(bmp280_devno, 1);

	dev_info(&client->dev, "BMP280 driver removed\n");
	return 0;
}


/* ================================================================
 * BLOCK 17: DEVICE TREE MATCHING
 *
 * This table tells the I2C core which device tree nodes this
 * driver can handle.
 *
 * When kernel parses DTB and finds a node with:
 *   compatible = "sai,bmp280";
 * It calls i2c_device_match() which compares against this table.
 *
 * The matching algorithm (in order of priority):
 *   1. OF match: of_match_table compatible strings
 *   2. ACPI match: acpi_match_table (not used on BBB)
 *   3. id_table match: i2c_device_id name string
 *
 * MODULE_DEVICE_TABLE(of, ...) does two things:
 *   - Creates an alias for module autoloading (modprobe)
 *   - Generates entries in modules.alias file
 *   - For built-in drivers, this is less relevant but still good practice
 *
 * The empty {} at the end is the sentinel (terminator).
 * Kernel iterates the array until it hits a zeroed entry.
 * ================================================================ */
static const struct of_device_id bmp280_of_match[] = {
	{ .compatible = "sai,bmp280" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, bmp280_of_match);


/* ================================================================
 * BLOCK 18: I2C DRIVER STRUCTURE + REGISTRATION
 *
 * struct i2c_driver is the central registration structure:
 *   .driver.name:           driver name in /sys/bus/i2c/drivers/
 *   .driver.of_match_table: DT matching table (Block 17)
 *   .probe:                 called on match
 *   .remove:                called on unbind/unload
 *
 * module_i2c_driver() is a convenience macro that expands to:
 *
 *   static int __init bmp280_driver_init(void) {
 *       return i2c_add_driver(&bmp280_driver);
 *   }
 *   static void __exit bmp280_driver_exit(void) {
 *       i2c_del_driver(&bmp280_driver);
 *   }
 *   module_init(bmp280_driver_init);
 *   module_exit(bmp280_driver_exit);
 *
 * For BUILT-IN drivers (obj-y):
 *   module_init → device_initcall → .initcall6.init section
 *   Kernel's do_initcalls() walks init sections 0-7 at boot
 *   Level 6 = device_initcall, called after subsystems are ready
 *
 * For LOADABLE modules (obj-m):
 *   insmod → calls init_module() → module_init function
 *   rmmod → calls cleanup_module() → module_exit function
 * ================================================================ */
static struct i2c_driver bmp280_driver = {
	.driver = {
		.name           = "bmp280_custom",
		.of_match_table = bmp280_of_match,
	},
	.probe  = bmp280_probe,
	.remove = bmp280_remove,
};

module_i2c_driver(bmp280_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sai Kadavakollu");
MODULE_DESCRIPTION("BMP280 I2C driver with chardev, sysfs, ioctl & GPIO44 interrupt");


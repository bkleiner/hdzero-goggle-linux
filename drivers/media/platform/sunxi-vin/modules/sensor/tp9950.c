
/*
 * A V4L2 driver for TP9950 YUV cameras.
 *
 * Copyright (c) 2019 by Allwinnertech Co., Ltd.  http://www.allwinnertech.com
 *
 * Authors:  Zheng Zequn <zequnzheng@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/videodev2.h>
#include <linux/clk.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <linux/io.h>

#include "camera.h"
#include "sensor_helper.h"

MODULE_AUTHOR("zw");
MODULE_DESCRIPTION("A low-level driver for TP9950 sensors");
MODULE_LICENSE("GPL");

#define MCLK              (27*1000*1000)
#define CLK_POL           V4L2_MBUS_PCLK_SAMPLE_FALLING
#define V4L2_IDENT_SENSOR 0x5028

#define DBG_INFO(format, args...) (printk("[TP9950 INFO] LINE:%04d-->%s:"format, __LINE__, __func__, ##args))
#define DBG_ERR(format, args...) (printk("[TP9950 ERR] LINE:%04d-->%s:"format, __LINE__, __func__, ##args))

/* enable tp9950 sensor detect */
#define SENSOR_DETECT_KTHREAD 1
/* USE DETECT BY GPIO_IRQ OR POLLING
 * DET_USE_POLLING 0 meant by gpio_irq
 * DET_USE_POLLING 1 meant by POLLING
 * */
#define DET_USE_POLLING 1

#define DETECT_GPIO_MAX 4

#if SENSOR_DETECT_KTHREAD

struct sensor_indetect {
	struct class *sensor_class;
	struct task_struct *sensor_task;
	struct device *dev;
	struct cdev *cdev;
	struct gpio_config detect_power;
	struct gpio_config detect_gpio[DETECT_GPIO_MAX];
	struct delayed_work tp9950_work;

	data_type   last_status[DETECT_GPIO_MAX];
#if !DET_USE_POLLING
	unsigned int detect_irq[DETECT_GPIO_MAX];
#endif
	dev_t devno;
	int detect_gpio_count;
} sensor_indetect;
static DEFINE_MUTEX(det_mutex);
#endif

/*
 * Our nominal (default) frame rate.
 */
#define SENSOR_FRAME_RATE 30

/*
 * The TP9920 sits on i2c with ID 0x88 or 0x8a
 * SAD-low:0x88 SAD-high:0x8a
 */
#define I2C_ADDR 0x88
#define SENSOR_NAME "tp9950"

static struct regval_list reg_1080p30_1ch[] = {
	{0x02, 0xCC},
	{0x05, 0x00},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x03},
	{0x0D, 0x72},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0x00},
	{0x11, 0x40},
	{0x12, 0x60},
	{0x13, 0x00},
	{0x14, 0x00},
	{0x15, 0x01},
	{0x16, 0xF0},
	{0x17, 0x80},
	{0x18, 0x29},
	{0x19, 0x38},
	{0x1A, 0x47},
	{0x1B, 0x01},
	{0x1C, 0x08},
	{0x1D, 0x98},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0x38},
	{0x21, 0x46},
	{0x22, 0x36},
	{0x23, 0x3C},
	{0x24, 0x04},
	{0x25, 0xFE},
	{0x26, 0x0D},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x60},
	{0x2C, 0x3A},
	{0x2D, 0x54},
	{0x2E, 0x40},
	{0x2F, 0x00},
	{0x30, 0xA5},
	{0x31, 0x95},
	{0x32, 0xE0},
	{0x33, 0x60},
	{0x34, 0x00},
	{0x35, 0x05},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x00},
	{0x39, 0x1C},
	{0x3A, 0x32},
	{0x3B, 0x26},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x41, 0x00},
	{0x42, 0x00},
	{0x43, 0x00},
	{0x44, 0x00},
	{0x45, 0x00},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x43},
	{0x4D, 0x00},
	{0x4E, 0x17},
	{0x4F, 0x00},
	{0x50, 0x00},
	{0x51, 0x00},
	{0x52, 0x00},
	{0x53, 0x00},
	{0x54, 0x00},

	{0xB3, 0xFA},
	{0xB4, 0x00},
	{0xB5, 0x00},
	{0xB6, 0x00},
	{0xB7, 0x00},
	{0xB8, 0x00},
	{0xB9, 0x00},
	{0xBA, 0x00},
	{0xBB, 0x00},
	{0xBC, 0x00},
	{0xBD, 0x00},
	{0xBE, 0x00},
	{0xBF, 0x00},
	{0xC0, 0x00},
	{0xC1, 0x00},
	{0xC2, 0x0B},
	{0xC3, 0x0C},
	{0xC4, 0x00},
	{0xC5, 0x00},
	{0xC6, 0x1F},
	{0xC7, 0x78},
	{0xC8, 0x27},
	{0xC9, 0x00},
	{0xCA, 0x00},
	{0xCB, 0x07},
	{0xCC, 0x08},
	{0xCD, 0x00},
	{0xCE, 0x00},
	{0xCF, 0x04},
	{0xD0, 0x00},
	{0xD1, 0x00},
	{0xD2, 0x60},
	{0xD3, 0x10},
	{0xD4, 0x06},
	{0xD5, 0xBE},
	{0xD6, 0x39},
	{0xD7, 0x27},
	{0xD8, 0x00},
	{0xD9, 0x00},
	{0xDA, 0x00},
	{0xDB, 0x00},
	{0xDC, 0x00},
	{0xDD, 0x00},
	{0xDE, 0x00},
	{0xDF, 0x00},
	{0xE0, 0x00},
	{0xE1, 0x00},
	{0xE2, 0x00},
	{0xE3, 0x00},
	{0xE4, 0x00},
	{0xE5, 0x00},
	{0xE6, 0x00},
	{0xE7, 0x13},
	{0xE8, 0x03},
	{0xE9, 0x00},
	{0xEA, 0x00},
	{0xEB, 0x00},
	{0xEC, 0x00},
	{0xED, 0x00},
	{0xEE, 0x00},
	{0xEF, 0x00},
	{0xF0, 0x00},
	{0xF1, 0x00},
	{0xF2, 0x00},
	{0xF3, 0x00},
	{0xF4, 0x20},
	{0xF5, 0x10},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x88},
	{0xFB, 0x00},
	{0xFC, 0x00},

	{0x40, 0x08},
	{0x00, 0x00},
	{0x01, 0xf8},
	{0x02, 0x01},
	{0x08, 0xF0},
	{0x13, 0x04},
	{0x14, 0x73},
	{0x15, 0x08},
	{0x20, 0x12},
	{0x34, 0x1b},
	{0x23, 0x02},
	{0x23, 0x00},

	{0x40, 0x00},
};

static struct regval_list reg_1080p25_1ch[] = {
	{0x02, 0xCC},
	{0x05, 0x00},
	{0x06, 0x32},
	{0x07, 0xC0},
	{0x08, 0x00},
	{0x09, 0x24},
	{0x0A, 0x48},
	{0x0B, 0xC0},
	{0x0C, 0x03},
	{0x0D, 0x73},
	{0x0E, 0x00},
	{0x0F, 0x00},
	{0x10, 0x00},
	{0x11, 0x40},
	{0x12, 0x60},
	{0x13, 0x00},
	{0x14, 0x00},
	{0x15, 0x01},
	{0x16, 0xF0},
	{0x17, 0x80},
	{0x18, 0x29},
	{0x19, 0x38},
	{0x1A, 0x47},
	{0x1B, 0x01},
	{0x1C, 0x0A},
	{0x1D, 0x50},
	{0x1E, 0x80},
	{0x1F, 0x80},
	{0x20, 0x3C},
	{0x21, 0x46},
	{0x22, 0x36},
	{0x23, 0x3C},
	{0x24, 0x04},
	{0x25, 0xFE},
	{0x26, 0x0D},
	{0x27, 0x2D},
	{0x28, 0x00},
	{0x29, 0x48},
	{0x2A, 0x30},
	{0x2B, 0x60},
	{0x2C, 0x1A},
	{0x2D, 0x54},
	{0x2E, 0x40},
	{0x2F, 0x00},
	{0x30, 0xA5},
	{0x31, 0x86},
	{0x32, 0xFB},
	{0x33, 0x60},
	{0x34, 0x00},
	{0x35, 0x05},
	{0x36, 0xDC},
	{0x37, 0x00},
	{0x38, 0x00},
	{0x39, 0x1C},
	{0x3A, 0x32},
	{0x3B, 0x26},
	{0x3C, 0x00},
	{0x3D, 0x60},
	{0x3E, 0x00},
	{0x3F, 0x00},
	{0x40, 0x00},
	{0x41, 0x00},
	{0x42, 0x00},
	{0x43, 0x00},
	{0x44, 0x00},
	{0x45, 0x00},
	{0x46, 0x00},
	{0x47, 0x00},
	{0x48, 0x00},
	{0x49, 0x00},
	{0x4A, 0x00},
	{0x4B, 0x00},
	{0x4C, 0x43},
	{0x4D, 0x00},
	{0x4E, 0x17},
	{0x4F, 0x00},
	{0x50, 0x00},
	{0x51, 0x00},
	{0x52, 0x00},
	{0x53, 0x00},
	{0x54, 0x00},

	{0xB3, 0xFA},
	{0xB4, 0x00},
	{0xB5, 0x00},
	{0xB6, 0x00},
	{0xB7, 0x00},
	{0xB8, 0x00},
	{0xB9, 0x00},
	{0xBA, 0x00},
	{0xBB, 0x00},
	{0xBC, 0x00},
	{0xBD, 0x00},
	{0xBE, 0x00},
	{0xBF, 0x00},
	{0xC0, 0x00},
	{0xC1, 0x00},
	{0xC2, 0x0B},
	{0xC3, 0x0C},
	{0xC4, 0x00},
	{0xC5, 0x00},
	{0xC6, 0x1F},
	{0xC7, 0x78},
	{0xC8, 0x27},
	{0xC9, 0x00},
	{0xCA, 0x00},
	{0xCB, 0x07},
	{0xCC, 0x08},
	{0xCD, 0x00},
	{0xCE, 0x00},
	{0xCF, 0x04},
	{0xD0, 0x00},
	{0xD1, 0x00},
	{0xD2, 0x60},
	{0xD3, 0x10},
	{0xD4, 0x06},
	{0xD5, 0xBE},
	{0xD6, 0x39},
	{0xD7, 0x27},
	{0xD8, 0x00},
	{0xD9, 0x00},
	{0xDA, 0x00},
	{0xDB, 0x00},
	{0xDC, 0x00},
	{0xDD, 0x00},
	{0xDE, 0x00},
	{0xDF, 0x00},
	{0xE0, 0x00},
	{0xE1, 0x00},
	{0xE2, 0x00},
	{0xE3, 0x00},
	{0xE4, 0x00},
	{0xE5, 0x00},
	{0xE6, 0x00},
	{0xE7, 0x13},
	{0xE8, 0x03},
	{0xE9, 0x00},
	{0xEA, 0x00},
	{0xEB, 0x00},
	{0xEC, 0x00},
	{0xED, 0x00},
	{0xEE, 0x00},
	{0xEF, 0x00},
	{0xF0, 0x00},
	{0xF1, 0x00},
	{0xF2, 0x00},
	{0xF3, 0x00},
	{0xF4, 0x20},
	{0xF5, 0x10},
	{0xF6, 0x00},
	{0xF7, 0x00},
	{0xF8, 0x00},
	{0xF9, 0x00},
	{0xFA, 0x88},
	{0xFB, 0x00},
	{0xFC, 0x00},

	{0x40, 0x08},
	{0x00, 0x00},
	{0x01, 0xf8},
	{0x02, 0x01},
	{0x08, 0xF0},
	{0x13, 0x04},
	{0x14, 0x73},
	{0x15, 0x08},
	{0x20, 0x12},
	{0x34, 0x1b},
	{0x23, 0x02},
	{0x23, 0x00},

	{0x40, 0x00},
};

static int sensor_s_sw_stby(struct v4l2_subdev *sd, int on_off)
{
	if (on_off)
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
	else
		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
	return 0;
}

static int sensor_power(struct v4l2_subdev *sd, int on)
{
	switch (on) {
	case STBY_ON:
		sensor_dbg("CSI_SUBDEV_STBY_ON!\n");
		sensor_s_sw_stby(sd, ON);
		break;
	case STBY_OFF:
		sensor_dbg("CSI_SUBDEV_STBY_OFF!\n");
		sensor_s_sw_stby(sd, OFF);
		break;
	case PWR_ON:
		DBG_INFO("CSI_SUBDEV_PWR_ON!\n");
		cci_lock(sd);
		vin_gpio_set_status(sd, PWDN, 1);
		vin_gpio_set_status(sd, RESET, 1);
		vin_gpio_set_status(sd, SM_HS, 1);
		vin_gpio_set_status(sd, POWER_EN, 1);

		vin_gpio_write(sd, PWDN, CSI_GPIO_HIGH);
		vin_gpio_write(sd, SM_HS, CSI_GPIO_HIGH);
		vin_gpio_write(sd, POWER_EN, CSI_GPIO_HIGH);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);

		vin_set_pmu_channel(sd, IOVDD, ON);
		vin_set_pmu_channel(sd, AVDD, ON);
		vin_set_pmu_channel(sd, DVDD, ON);

		usleep_range(5000, 6000);
		vin_set_mclk_freq(sd, MCLK);
		vin_set_mclk(sd, ON);
		usleep_range(30000, 31000);

		vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
		usleep_range(30000, 31000);
		cci_unlock(sd);
		break;
	case PWR_OFF:
		DBG_INFO("CSI_SUBDEV_PWR_OFF!\n");
		break;
		cci_lock(sd);
		vin_set_mclk(sd, OFF);
		vin_set_pmu_channel(sd, DVDD, OFF);
		vin_set_pmu_channel(sd, AVDD, OFF);
		vin_set_pmu_channel(sd, IOVDD, OFF);
		usleep_range(100, 120);
		vin_gpio_write(sd, SM_HS, CSI_GPIO_LOW);
		vin_gpio_write(sd, PWDN, CSI_GPIO_LOW);
		vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
		vin_gpio_set_status(sd, SM_HS, 0);
		vin_gpio_set_status(sd, RESET, 0);
		vin_gpio_set_status(sd, PWDN, 0);
		vin_gpio_set_status(sd, POWER_EN, 0);
		cci_unlock(sd);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int sensor_reset(struct v4l2_subdev *sd, u32 val)
{
	vin_gpio_write(sd, RESET, CSI_GPIO_LOW);
	usleep_range(5000, 6000);
	vin_gpio_write(sd, RESET, CSI_GPIO_HIGH);
	usleep_range(5000, 6000);
	return 0;
}

static int sensor_detect(struct v4l2_subdev *sd)
{
	data_type rdval, rdval1, rdval2;
	int cnt = 0;

	rdval = 0;
	rdval1 = 0;
	rdval2 = 0;
	
	DBG_INFO("\n");	

	sensor_read(sd, 0xfe, &rdval1);
	sensor_read(sd, 0xff, &rdval2);
	rdval = ((rdval2<<8) & 0xff00) | rdval1;
	DBG_INFO("V4L2_IDENT_SENSOR = 0x%x\n", rdval);

	while ((rdval != V4L2_IDENT_SENSOR) && (cnt < 5)) {
		sensor_read(sd, 0xff, &rdval);
		DBG_INFO("retry = %d, V4L2_IDENT_SENSOR = %x\n", cnt,
			     rdval);
		cnt++;
	}

	if (rdval != V4L2_IDENT_SENSOR)
		return -ENODEV;
    DBG_INFO("tp9950 detect ok !!!");
	return 0;
}

static int sensor_init(struct v4l2_subdev *sd, u32 val)
{
	int ret;
	struct sensor_info *info = to_state(sd);

	sensor_dbg("sensor_init\n");

	/*Make sure it is a target sensor */
	ret = sensor_detect(sd);
	if (ret) {
		sensor_err("chip found is not an target chip.\n");
		return ret;
	}

	info->focus_status = 0;
	info->low_speed = 0;
	info->width = HD1080_WIDTH;
	info->height = HD1080_HEIGHT;
	info->hflip = 0;
	info->vflip = 0;

	info->tpf.numerator = 1;
	info->tpf.denominator = 25;	/* 25fps */

	info->preview_first_flag = 1;
	return 0;
}

static long sensor_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	int ret = 0;
	struct sensor_info *info = to_state(sd);

	switch (cmd) {
	case GET_CURRENT_WIN_CFG:
		if (info->current_wins != NULL) {
			memcpy(arg,
			       info->current_wins,
			       sizeof(struct sensor_win_size));
			ret = 0;
		} else {
			sensor_err("empty wins!\n");
			ret = -1;
		}
		break;
	case SET_FPS:
		break;
	case VIDIOC_VIN_SENSOR_CFG_REQ:
		sensor_cfg_req(sd, (struct sensor_config *)arg);
		break;
	default:
		return -EINVAL;
	}
	return ret;
}

/*
 * Store information about the video data format.
 */
static struct sensor_format_struct sensor_formats[] = {
	{
	.desc = "BT656 1CH",
	.mbus_code = MEDIA_BUS_FMT_UYVY8_2X8,
	.regs = NULL,
	.regs_size = 0,
	.bpp = 1,
	},
};
#define N_FMTS ARRAY_SIZE(sensor_formats)

/*
 * Then there is the issue of window sizes.  Try to capture the info here.
 */

static struct sensor_win_size sensor_win_sizes[] = {
	{
	 .width = HD1080_WIDTH,
	 .height = HD1080_HEIGHT,
	 .hoffset = 0,
	 .voffset = 0,
	 .fps_fixed = 30,
	 .regs = reg_1080p30_1ch,
	 .regs_size = ARRAY_SIZE(reg_1080p30_1ch),
	 .set_size = NULL,
	},
	{
	 .width = HD1080_WIDTH,
	 .height = HD1080_HEIGHT,
	 .hoffset = 0,
	 .voffset = 0,
	 .fps_fixed = 25,
	 .regs = reg_1080p25_1ch,
	 .regs_size = ARRAY_SIZE(reg_1080p25_1ch),
	 .set_size = NULL,
	},
};
#define N_WIN_SIZES (ARRAY_SIZE(sensor_win_sizes))

static int sensor_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_BT656;
	cfg->flags = CLK_POL | CSI_CH_0;
	return 0;
}

static int sensor_g_ctrl(struct v4l2_ctrl *ctrl)
{
	return -EINVAL;
}

static int sensor_s_ctrl(struct v4l2_ctrl *ctrl)
{
	return -EINVAL;
}

static int sensor_reg_init(struct sensor_info *info)
{
	struct v4l2_subdev *sd = &info->sd;
	struct sensor_format_struct *sensor_fmt = info->fmt;
	struct sensor_win_size *wsize = info->current_wins;

	sensor_dbg("sensor_reg_init\n");

	sensor_write_array(sd, sensor_fmt->regs, sensor_fmt->regs_size);

	if (wsize->regs)
		sensor_write_array(sd, wsize->regs, wsize->regs_size);

	if (wsize->set_size)
		wsize->set_size(sd);

	info->fmt = sensor_fmt;
	info->width = wsize->width;
	info->height = wsize->height;

	return 0;
}

#if SENSOR_DETECT_KTHREAD
static int __sensor_insert_detect(data_type *val, int index)
{
	int ret;

	mutex_lock(&det_mutex);
	/*detecting the sensor insert by detect_gpio
	 * if detect_gpio was height meant sensor was insert
	 * if detect_gpio was low, meant sensor was remove
	 * */
	gpio_direction_input(sensor_indetect.detect_gpio[index].gpio);
	ret = gpio_get_value_cansleep(sensor_indetect.detect_gpio[index].gpio);
	//DBG_INFO("index:%d, ret:%d\n", index, ret);
	*val = !!ret;
//	if (ret) {
//		*val = 0x00; //remore
//	} else {
//		 *val = 0x01; //insert
//	}

	mutex_unlock(&det_mutex);

	return 0;
}

void sensor_msg_sent(char *buf)
{
	char *envp[2];

	DBG_INFO("\n");
	envp[0] = buf;
	envp[1] = NULL;
	kobject_uevent_env(&sensor_indetect.dev->kobj, KOBJ_CHANGE, envp);
}

static void sensor_det_work(struct work_struct *work)
{
	char buf[32];
	data_type val;
	int index;

	for(index = 0; index < sensor_indetect.detect_gpio_count; ++index){
		__sensor_insert_detect(&val, index);
//		DBG_INFO("index:%d, val = 0x%x\n", index, val);
		if (sensor_indetect.last_status[index] != val) {
			memset(buf, 0, 32);
			snprintf(buf, sizeof(buf), "SENSOR_RAVAL=0x%x", val);
			sensor_indetect.last_status[index] = val;
			sensor_msg_sent(buf);			
			DBG_INFO("index:%d, val = 0x%x, Sent msg to user\n", index, val);
		}
	}

#if DET_USE_POLLING
	schedule_delayed_work(&sensor_indetect.tp9950_work, (msecs_to_jiffies(1 * 1000)));
#endif
#if 0
	memset(buf, 0, 32);
	snprintf(buf, sizeof(buf), "SENSOR_RAVAL=0x%x", val);

	sensor_msg_sent(buf);
	vin_print("val = 0x%x, Sent msg to user\n", val);
	
#endif

}


#if !DET_USE_POLLING
static irqreturn_t sensor_det_irq_func(int irq, void *priv)
{
	/* the work of detected was be run in workquen */
	//schedule_delayed_work(&sensor_indetect.tp9950_work, 0);
	DBG_INFO("irq:%d\n", irq);
	return IRQ_HANDLED;
}
#endif

#endif

static int sensor_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct sensor_info *info = to_state(sd);

	DBG_INFO("%s on = %d, %d*%d fps: %d code: %x\n", __func__, enable,
			info->current_wins->width, info->current_wins->height,
			info->current_wins->fps_fixed, info->fmt->mbus_code);


	if (!enable)
		return 0;

	return sensor_reg_init(info);
}

/* ----------------------------------------------------------------------- */
static const struct v4l2_ctrl_ops sensor_ctrl_ops = {
	.g_volatile_ctrl = sensor_g_ctrl,
	.s_ctrl = sensor_s_ctrl,
};

static const struct v4l2_subdev_core_ops sensor_core_ops = {
	.reset = sensor_reset,
	.init = sensor_init,
	.s_power = sensor_power,
	.ioctl = sensor_ioctl,
};

static const struct v4l2_subdev_video_ops sensor_video_ops = {
	.s_parm = sensor_s_parm,
	.g_parm = sensor_g_parm,
	.s_stream = sensor_s_stream,
	.g_mbus_config = sensor_g_mbus_config,
};

static const struct v4l2_subdev_pad_ops sensor_pad_ops = {
	.enum_mbus_code = sensor_enum_mbus_code,
	.enum_frame_size = sensor_enum_frame_size,
	.get_fmt = sensor_get_fmt,
	.set_fmt = sensor_set_fmt,
};

static const struct v4l2_subdev_ops sensor_ops = {
	.core = &sensor_core_ops,
	.video = &sensor_video_ops,
	.pad = &sensor_pad_ops,
};


/* ----------------------------------------------------------------------- */
static struct cci_driver cci_drv = {
	.name = SENSOR_NAME,
	.addr_width = CCI_BITS_8,
	.data_width = CCI_BITS_8,
};

static int sensor_init_controls(struct v4l2_subdev *sd,
				const struct v4l2_ctrl_ops *ops)
{
	struct sensor_info *info = to_state(sd);
	struct v4l2_ctrl_handler *handler = &info->handler;
	struct v4l2_ctrl *ctrl;
	int ret = 0;

	v4l2_ctrl_handler_init(handler, 2);

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_GAIN, 1 * 1600,
			      256 * 1600, 1, 1 * 1600);
	ctrl = v4l2_ctrl_new_std(handler, ops, V4L2_CID_EXPOSURE, 0,
			      65536 * 16, 1, 0);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	if (handler->error) {
		ret = handler->error;
		v4l2_ctrl_handler_free(handler);
	}

	sd->ctrl_handler = handler;

	return ret;
}


#if SENSOR_DETECT_KTHREAD
static int sensor_dev_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int sensor_dev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t sensor_dev_read(struct file *file, char __user *buf,
						size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static ssize_t sensor_dev_write(struct file *file, const char __user *buf,
						size_t count, loff_t *ppos)
{
	return -EINVAL;
}

static int sensor_dev_mmap(struct file *file, struct vm_area_struct *vma)
{
	return 0;
}

static long sensor_dev_ioctl(struct file *file, unsigned int cmd,
			     unsigned long arg)
{
	return 0;
}

static const struct file_operations sensor_dev_fops = {
	.owner          = THIS_MODULE,
	.open           = sensor_dev_open,
	.release        = sensor_dev_release,
	.write          = sensor_dev_write,
	.read           = sensor_dev_read,
	.unlocked_ioctl = sensor_dev_ioctl,
	.mmap           = sensor_dev_mmap,
};

static ssize_t get_det_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int val = 0;
	int index;
//	__sensor_insert_detect(&val);
	for(index = 0; index < sensor_indetect.detect_gpio_count; ++index){
		DBG_INFO("index:%d, last_status:0x%x\n", index, sensor_indetect.last_status[index]);
		val |= (sensor_indetect.last_status[index] << (4 * index));
	}
	DBG_INFO("val:0x%x\n", val);
	return sprintf(buf, "0x%x\n", val);
}
#ifdef DUMP_TEST
static ssize_t get_tp9950_dump0_show(struct device *dev,
        struct device_attribute *attr, char *buf)
{
    ssize_t val;
    data_type  value_ = 0;
    int i= 0;
    sensor_print("habo-->ready to write the 0x40=0x00\n");
    sensor_write(cci_drv.sd, 0x40, 0x00);
    for(i = 0 ; i< ARRAY_SIZE(reg_dump) ; i++)
    {
        
        sensor_read(cci_drv.sd,reg_dump[i].addr,&value_);
        sensor_print("habo--->0x40=0x00 0x%x = 0x%x \n",reg_dump[i].addr,value_);
        value_ = 0;
    }
    return val;
}

static ssize_t get_tp9950_dump1_show(struct device *dev,
struct device_attribute *attr, char *buf)
{
    ssize_t val;
    data_type  value_ = 0;
    int i= 0;
    sensor_print("habo-->ready to write the 0x40=0x01\n");
    sensor_write(cci_drv.sd, 0x40, 0x01);
    for(i = 0 ; i< ARRAY_SIZE(reg_dump) ; i++)
    {
        
        sensor_read(cci_drv.sd,reg_dump[i].addr,&value_);
        sensor_print("habo--->0x40=0x01 0x%x = 0x%x \n",reg_dump[i].addr,value_);
        value_ = 0;
    }

    return val;
}

static ssize_t get_tp9950_dump2_show(struct device *dev,
struct device_attribute *attr, char *buf)
{
ssize_t val;
data_type  value_ = 0;
int i= 0;

sensor_print("habo-->ready to write the 0x40=0x02\n");
sensor_write(cci_drv.sd, 0x40, 0x02);
for(i = 0 ; i< ARRAY_SIZE(reg_dump) ; i++)
{
    
    sensor_read(cci_drv.sd,reg_dump[i].addr,&value_);
    sensor_print("habo--->0x40=0x02 0x%x = 0x%x \n",reg_dump[i].addr,value_);
    value_ = 0;
}

return val;
}

static ssize_t get_tp9950_dump3_show(struct device *dev,
struct device_attribute *attr, char *buf)
{
ssize_t val;
int i= 0;

data_type  value_ = 0;
sensor_print("habo-->ready to write the 0x40=0x03\n");
sensor_write(cci_drv.sd, 0x40, 0x03);
for(i = 0 ; i< ARRAY_SIZE(reg_dump) ; i++)
{
    
    sensor_read(cci_drv.sd,reg_dump[i].addr,&value_);
    sensor_print("habo--->0x40=0x03 0x%x = 0x%x \n",reg_dump[i].addr,value_);
    value_ = 0;
}

return val;
}


#endif
#ifdef DUMP_TEST

static struct device_attribute  detect_dev_attrs[] = {
    {
    	.attr = {
    		.name = "online",
    		.mode =  S_IRUGO,
    	},
    	.show =  get_det_status_show,
    	.store = NULL,
    },
    
    {
    	.attr = {
    		.name = "dump0",
    		.mode =  S_IRUGO,
    	},
    	.show =  get_tp9950_dump0_show,
    	.store = NULL,
    },
        {
    	.attr = {
    		.name = "dump1",
    		.mode =  S_IRUGO,
    	},
    	.show =  get_tp9950_dump1_show,
    	.store = NULL,
    },
        {
    	.attr = {
    		.name = "dump2",
    		.mode =  S_IRUGO,
    	},
    	.show =  get_tp9950_dump2_show,
    	.store = NULL,
    },
        {
    	.attr = {
    		.name = "dump3",
    		.mode =  S_IRUGO,
    	},
    	.show =  get_tp9950_dump3_show,
    	.store = NULL,
    },
    
};
#else
static struct device_attribute  detect_dev_attrs = {
	.attr = {
		.name = "online",
		.mode =  S_IRUGO,
	},
	.show =  get_det_status_show,
	.store = NULL,

};


#endif
static int tp9950_sensor_det_init(struct i2c_client *client)
{
	int ret;
	struct device_node *np = NULL;
	int index;
	char key_buf[64] = {0};

	DBG_INFO("\n");
	/* enable detect work queue */
	INIT_DELAYED_WORK(&sensor_indetect.tp9950_work, sensor_det_work);
	np = of_find_node_by_name(NULL, "tp9950_detect");
	if (np == NULL) {
		sensor_err("can not find the tp9950_detect node, will not \
			   enable detect kthread\n");
		return -1;
	}
	sensor_indetect.detect_power.gpio = of_get_named_gpio_flags(np, "gpio_power", 0,
	    (enum of_gpio_flags *)(&(sensor_indetect.detect_power)));

	DBG_INFO("detect_power:%d\n", sensor_indetect.detect_power.gpio);
	if(gpio_is_valid(sensor_indetect.detect_power.gpio)){
		/* enable the detect power*/
		ret = gpio_request(sensor_indetect.detect_power.gpio, NULL);
		DBG_INFO("ret:%d\n", ret);
		if (ret < 0) {
			sensor_err("enable tp9950 sensor detect fail!!\n");		
		}else{
			gpio_direction_output(sensor_indetect.detect_power.gpio, 1);
		}
	}
	sensor_indetect.detect_gpio_count = 0;
	for(index = 0; index < DETECT_GPIO_MAX; ++index){
		memset(key_buf, 0, 64);
		sprintf(key_buf, "gpio_detect_%d", index);
		DBG_INFO("key_buf:%s\n", key_buf);
		sensor_indetect.detect_gpio[sensor_indetect.detect_gpio_count].gpio = of_get_named_gpio_flags(np, key_buf, 0,
	    (enum of_gpio_flags *)(&(sensor_indetect.detect_gpio[sensor_indetect.detect_gpio_count])));
		DBG_INFO("%s:%d\n", key_buf, sensor_indetect.detect_gpio[sensor_indetect.detect_gpio_count].gpio);

		if(gpio_is_valid(sensor_indetect.detect_gpio[sensor_indetect.detect_gpio_count].gpio)){
			ret = gpio_request(sensor_indetect.detect_gpio[sensor_indetect.detect_gpio_count].gpio, NULL);
			if (ret < 0) {
				sensor_err("enable  %s  fail! \n", key_buf);
			}else{
				gpio_direction_input(sensor_indetect.detect_gpio[sensor_indetect.detect_gpio_count].gpio);
#if !DET_USE_POLLING				
				/* request gpio to irq  */
				sensor_indetect.detect_irq[sensor_indetect.detect_gpio_count] = gpio_to_irq(sensor_indetect.detect_gpio[sensor_indetect.detect_gpio_count].gpio);
				//ret = request_irq(sensor_indetect.detect_irq[sensor_indetect.detect_gpio_count], sensor_det_irq_func,	IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,	"tp9950_sensor_detect", client);
				ret = devm_request_threaded_irq(sensor_indetect.dev, sensor_indetect.detect_irq[sensor_indetect.detect_gpio_count],
					  NULL, sensor_det_irq_func, IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING  | IRQF_ONESHOT, "tp9950_sensor_detect", client);	
				DBG_INFO("ret:%d\n", ret);
				enable_irq(sensor_indetect.detect_irq[sensor_indetect.detect_gpio_count]);
#endif				
				sensor_indetect.detect_gpio_count++;
			}
		}

	}

#if DET_USE_POLLING	
	DBG_INFO("sensor_indetect.detect_gpio_count:%d\n", sensor_indetect.detect_gpio_count);
	/* start detect work  */
	if(sensor_indetect.detect_gpio_count){
		schedule_delayed_work(&sensor_indetect.tp9950_work, 0);
	}
#endif
	return 0;
}

static void tp9950_sensor_det_remove(void)
{
	cancel_delayed_work_sync(&sensor_indetect.tp9950_work);

#if !DET_USE_POLLING
	/*free irq*/
	if (free_irq > 0) {
		disable_irq(sensor_indetect.detect_irq);
		free_irq(sensor_indetect.detect_irq, NULL);
	}
#endif

	//if (!(sensor_indetect.detect_power.gpio < 0 ||
	//      sensor_indetect.detect_gpio.gpio < 0)) {
	//	gpio_free(sensor_indetect.detect_power.gpio);
	//	gpio_free(sensor_indetect.detect_gpio.gpio);
	//}
}

#endif

static int sensor_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct sensor_info *info;
	int ret;
    int i = 0;
	info = kzalloc(sizeof(struct sensor_info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;
	sd = &info->sd;

	cci_dev_probe_helper(sd, client, &sensor_ops, &cci_drv);
	sensor_init_controls(sd, &sensor_ctrl_ops);
	mutex_init(&info->lock);

	info->fmt = &sensor_formats[0];
	info->fmt_pt = &sensor_formats[0];
	info->win_pt = &sensor_win_sizes[0];
	info->fmt_num = N_FMTS;
	info->win_size_num = N_WIN_SIZES;
	info->sensor_field = V4L2_FIELD_NONE;

#if SENSOR_DETECT_KTHREAD
	alloc_chrdev_region(&sensor_indetect.devno, 0, 1, "csi");
	sensor_indetect.cdev = cdev_alloc();
	if (IS_ERR(sensor_indetect.cdev)) {
		sensor_err("cdev_alloc fail!\n");
		goto free_devno;
	}

	cdev_init(sensor_indetect.cdev, &sensor_dev_fops);
	sensor_indetect.cdev->owner = THIS_MODULE;
	ret = cdev_add(sensor_indetect.cdev, sensor_indetect.devno, 1);
	if (ret) {
		sensor_err("cdev_add fail.\n");
		goto free_cdev;
	}

	sensor_indetect.sensor_class = class_create(THIS_MODULE, "csi");
	if (IS_ERR(sensor_indetect.sensor_class)) {
		sensor_err("class_create fail!\n");
		goto unregister_cdev;
	}



	sensor_indetect.dev =
	    device_create(sensor_indetect.sensor_class, NULL,
			  sensor_indetect.devno, NULL, "ahdCamera");
	if (IS_ERR(sensor_indetect.dev)) {
		sensor_err("device_create fail!\n");
		goto free_class;
	}
    #ifdef DUMP_TEST
    for (i = 0; i < ARRAY_SIZE(detect_dev_attrs); i++) {
            ret = device_create_file(sensor_indetect.dev,&detect_dev_attrs[i]);
            if (ret) {
                sensor_err("class_create  file fail!\n");
                goto free_class;
            }
        }
    #else
	ret = device_create_file(sensor_indetect.dev, &detect_dev_attrs);
	if (ret) {
		sensor_err("class_create  file fail!\n");
		goto free_class;
	}
    #endif
	DBG_INFO("\n");
	/* init tp9950 detect mode */
	ret = tp9950_sensor_det_init(client);
	if (ret) {
		goto free_det;
	}

	return 0;


free_det:
	tp9950_sensor_det_remove();

free_class:
	class_destroy(sensor_indetect.sensor_class);
    #ifdef DUMP_TEST
    for (i--; i >= 0; i--)
        device_remove_file(sensor_indetect.dev,
                        &detect_dev_attrs[i]);
    #endif

unregister_cdev:
	cdev_del(sensor_indetect.cdev);

free_cdev:
	kfree(sensor_indetect.cdev);

free_devno:
	unregister_chrdev_region(sensor_indetect.devno, 1);


#endif

	return 0;
}

static int sensor_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd;

	sd = cci_dev_remove_helper(client, &cci_drv);

#if SENSOR_DETECT_KTHREAD
	tp9950_sensor_det_remove();

	device_destroy(sensor_indetect.sensor_class, sensor_indetect.devno);
	class_destroy(sensor_indetect.sensor_class);
	cdev_del(sensor_indetect.cdev);
	kfree(sensor_indetect.cdev);
	unregister_chrdev_region(sensor_indetect.devno, 1);
#endif

	kfree(to_state(sd));
	return 0;
}


static void sensor_shutdown(struct i2c_client *client)
{
	struct v4l2_subdev *sd;

	sd = cci_dev_remove_helper(client, &cci_drv);

#if SENSOR_DETECT_KTHREAD
	tp9950_sensor_det_remove();

	device_destroy(sensor_indetect.sensor_class, sensor_indetect.devno);
	class_destroy(sensor_indetect.sensor_class);
	cdev_del(sensor_indetect.cdev);
	kfree(sensor_indetect.cdev);
	unregister_chrdev_region(sensor_indetect.devno, 1);

#endif
	kfree(to_state(sd));
}

static const struct i2c_device_id sensor_id[] = {
	{SENSOR_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sensor_id);

static struct i2c_driver sensor_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = SENSOR_NAME,
		   },
	.probe = sensor_probe,
	.remove = sensor_remove,
	.shutdown = sensor_shutdown,
	.id_table = sensor_id,
};
static __init int init_sensor(void)
{
	return cci_dev_init_helper(&sensor_driver);
}

static __exit void exit_sensor(void)
{
	cci_dev_exit_helper(&sensor_driver);
}

module_init(init_sensor);
module_exit(exit_sensor);

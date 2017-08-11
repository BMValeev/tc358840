/*
 * Copyright (C) 2011-2012 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2014 Boundary Devices
 */

/*
 * Modifyed by: Edison Fernández <edison.fernandez@ridgerun.com>
 * Added support to use it with Nitrogen6x
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#define DEBUG 1
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/fsl_devices.h>
#include <linux/mutex.h>
#include <linux/mipi_csi2.h>
#include <media/v4l2-chip-ident.h>
#include "v4l2-int-device.h"
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/soc-dapm.h>
#include <asm/mach-types.h>
//#include <mach/audmux.h>
#include <linux/slab.h>
#include "mxc_v4l2_capture.h"

#include "tc358743_i2c.c"


static int tc358840_probe(struct i2c_client *adapter,
				const struct i2c_device_id *device_id);
static int tc358840_remove(struct i2c_client *client);

static const struct i2c_device_id tc358840_id[] = 
{
	{"tc358840_mipi", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, tc358840_id);

static struct i2c_driver tc358840_i2c_driver = 
{
	.driver = 
		{
			.owner = THIS_MODULE,
			.name  = "tc358840_mipi",
		},
	.probe  = tc358840_probe,
	.remove = tc358840_remove,
	.id_table = tc358840_id,
};
#include "tc358743_lowl.c"
#include "tc358743_ioctl.c"


struct tc_mode_list 
{
	const char *name;
	enum tc358840_mode mode;
};

static const struct tc_mode_list tc358840_mode_list[] =
{
	{"None", tc358840_mode_unknown},					/* 0 */
	{"VGA", tc358840_mode_VGA},		/* 1 */
	{"240p/480i", tc358840_mode_240p},				/* 2 */
	{"288p/576i", tc358840_mode_288p},				/* 3 */
	{"W240p/480i", tc358840_mode_w240p},				/* 4 */
	{"W288p/576i", tc358840_mode_w288p},				/* 5 */
	{"480p", tc358840_mode_480p},					/* 6 */
	{"576p", tc358840_mode_576p},					/* 7 */
	{"W480p", tc358840_mode_w480p},		/* 8 */
	{"W576p", 0},					/* 9 */
	{"WW480p", 0},					/* 10 */
	{"WW576p", 0},					/* 11 */
	{"720p", tc358840_mode_720p},	/* 12 */
	{"1035i", 0},					/* 13 */
	{"1080i", 0},					/* 14 */
	{"1080p", tc358840_mode_1080p},	/* 15 */
};

/*static int tc358840_audio_list[16] =
{
	44100,
	0,
	48000,
	32000,
	22050,
	384000,
	24000,
	352800,
	88200,
	768000,
	96000,
	705600,
	176400,
	0,
	192000,
	0
};*/

static char str_on[80];
/*static void report_netlink(struct tc_data *td)
{
	struct sensor_data *sensor = &td->sensor;
	char *envp[2];
	envp[0] = &str_on[0];
	envp[1] = NULL;
	sprintf(envp[0], "HDMI RX: %d (%s) %d %d",td->mode,
			tc358743_mode_info_data[td->fps][td->mode].name,
			tc358743_fps_list[td->fps], tc358840_audio_list[td->audio]);
	kobject_uevent_env(&(sensor->i2c_client->dev.kobj), KOBJ_CHANGE, envp);
	td->det_work_timeout = DET_WORK_TIMEOUT_DEFAULT;
	pr_debug("%s: HDMI RX (%d) mode: %s fps: %d (%d, %d) audio: %d\n",__func__, td->mode,
		tc358743_mode_info_data[td->fps][td->mode].name, td->fps, td->bounce,
		td->det_work_timeout, tc358840_audio_list[td->audio]);
}*/

static void tc_det_worker(struct work_struct *work)
{
	struct tc_data *td = container_of(work, struct tc_data, det_work.work);
	struct sensor_data *sensor = &td->sensor;
	int ret;
	u32 u32val, u8520,u8521;
	enum tc358840_mode mode = tc358840_mode_unknown;
	if(!td->det_work_enable) { return; }
        mutex_lock(&td->access_lock);
	if(!td->det_work_enable) { goto out2; }
        u8521 = 0;

	if(tc358840_read_reg(sensor, 0x8520, &u8520) < 0) { goto out ;}
		/*pr_err("%s: Error reading lock\n", __func__);*/

	if ((u8520 & HDMI_8520_S)==0x00) 
	{
		td->lock = u8520 & HDMI_8520_S;
		u8521 = 0;
		(tc358840_read_reg(sensor, 0x8521, &u8521) < 0) ? 
			pr_err("%s: Error reading mode\n", __func__): 
			pr_info("%s: detect 8521=%x 8520=%x\n", __func__, u8521, u8520);
		u8521 &= 0x0f;
		td->fps = tc358840_50_fps;
		if (u8521==0x00) 
		{
			int hsize, vsize, fps;
			hsize = tc358840_read_reg_val16(sensor, 0x8582);
			vsize = tc358840_read_reg_val16(sensor, 0x858C);
			fps = tc_get_fps(sensor);
			//pr_info("%s: detect hsize=%d, vsize=%d\n", __func__, hsize, vsize);
			/*if ((hsize == 1024) && (vsize == 768))
				mode = tc358840_mode_1024x768;
			else if (hsize == 1280)
				mode = tc358840_mode_720P_60_1280_720;
			else if (hsize == 1920)
				mode = tc358840_mode_1080P_1920_1080;*/
			if (fps && tc_fps_to_index(fps)) { td->fps = tc_fps_to_index(fps) ;}
		} 
		else 
		{
			mode = tc358840_mode_list[u8521].mode;
			if(td->mode != mode) { pr_debug("%s: %s detected\n", __func__, tc358840_mode_list[u8521].name) ;}
			if (u8520 >= 0xe) { td->fps = ((u8520 & 0x0f) > 0xa) ? tc358840_50_fps: tc358840_60_fps ;}
		}
	}
 	else 
	{
		if (td->lock) { td->lock = 0 ;}
		u8521 = 0;
		(tc358840_read_reg(sensor, 0x8521, &u8521)< 0) ? pr_err("%s: Error reading mode\n", __func__) :
			pr_info("%s: lost hdmi_detect 8521=%x 852f=%x\n", __func__, u8521, u8520) ;
//		if (u8521)
//			mode = tc358840_mode_list[u8521].mode;
	}

	if (td->mode != mode) 
	{
		td->det_work_timeout = DET_WORK_TIMEOUT_DEFAULT;
		td->bounce = MAX_BOUNCE;
		/*pr_debug("%s: HDMI RX (%d != %d) mode: %s fps: %d (%d, %d)\n",
				__func__, td->mode, mode,
				tc358743_mode_info_data[td->fps][mode].name,
				td->fps, td->bounce, td->det_work_timeout);*/
		td->mode = mode;
		sensor->streamcap.capturemode = mode;
		sensor->spix.swidth = tc358840_mode_info_data[mode].width;
		sensor->spix.sheight = tc358840_mode_info_data[mode].height;
		td->det_changed = 1;
	} 
	else if (td->bounce) 
	{
		td->bounce--;
		td->det_work_timeout =((!td->bounce) && (td->mode)) ? DET_WORK_TIMEOUT_DEFERRED : DET_WORK_TIMEOUT_DEFAULT;
		if ((!td->bounce) && (td->mode)) { goto out2;}
	} 
	else if (td->mode && !td->bounce) 
	{
		goto out2;
	}
out:
	td->det_work_timeout = DET_WORK_TIMEOUT_DEFERRED;
	schedule_delayed_work(&td->det_work, msecs_to_jiffies(td->det_work_timeout));
out2:
	mutex_unlock(&td->access_lock);
}

static irqreturn_t tc358840_detect_handler(int irq, void *data)
{
	struct tc_data *td = data;
	struct sensor_data *sensor = &td->sensor;
	pr_debug("%s: IRQ %d\n", __func__, sensor->i2c_client->irq);
	schedule_delayed_work(&td->det_work, msecs_to_jiffies(10));
	return IRQ_HANDLED;
}

static	u16 regoffs = 0;

static ssize_t tc358840_store_regdump(struct device *device,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct tc_data *td = g_td;
	struct sensor_data *sensor = &td->sensor;
	u32 val;
	int retval,size;
	if (sscanf(buf, "%x", &val) == 1) 
	{
		size = get_reg_size(regoffs, 0);
		retval = tc358840_write_reg(sensor, regoffs, val, size);
		if (retval < 0) { pr_info("%s: err %d\n", __func__, retval);}
	}
	return count;
}

/*!
 * tc358840 I2C probe function
 *
 * @param adapter	    struct i2c_adapter *
 * @return  Error code indicating success or failure
 */
#define DUMP_LENGTH 256

static ssize_t tc358840_show_regdump(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_data *td = g_td;
	struct sensor_data *sensor = &td->sensor;
	int i, len = 0;
	int retval,size;
	if(!td) { return len ;}
	mutex_lock(&td->access_lock);
	for (i=0; i<DUMP_LENGTH; i+=size) 
        {
		u32 u32val = 0;
		int reg = regoffs+i;
		size = get_reg_size(reg, 0);
		if(!(i & 0xf)) { len += sprintf(buf+len, "\n%04X:", reg) ;}
		if (size == 0) 
                {
			len += sprintf(buf+len, " xx");
			size = 1;
			continue;
		}
		retval = tc358840_read_reg(sensor, reg, &u32val);
		if (tc358840_read_reg(sensor, reg, &u32val) < 0) { u32val = 0xff ;}
		switch (size)
		{
		case 1  : len += sprintf(buf+len, " %02X", u32val&0xff);
		case 2  : len += sprintf(buf+len, " %04X", u32val&0xffff);
		default : len += sprintf(buf+len, " %08X", u32val);
		}
			
	}
	mutex_unlock(&td->access_lock);
	len += sprintf(buf+len, "\n");
	return len;
}
static DEVICE_ATTR(regdump, S_IRUGO|S_IWUSR, tc358840_show_regdump, tc358840_store_regdump);


static ssize_t tc358840_store_regoffs(struct device *device,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u32 val;
	int retval;
	retval = sscanf(buf, "%x", &val);
	 if(1 == retval) { regoffs = (u16)val;}
	return count;
}
static ssize_t tc358840_show_regoffs(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int len = 0;
	len += sprintf(buf+len, "0x%04X\n", regoffs);
	return len;
}
static DEVICE_ATTR(regoffs, S_IRUGO|S_IWUSR, tc358840_show_regoffs, tc358840_store_regoffs);


static ssize_t tc358840_store_hpd(struct device *device,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct tc_data *td = g_td;
	u32 val;
	int retval;
	retval = sscanf(buf, "%d", &val);
	if (1 == retval) { td->hpd_active = (u16)val; }
	return count;
}
static ssize_t tc358840_show_hpd(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_data *td = g_td;
	int len = 0;
	len += sprintf(buf+len, "%d\n", td->hpd_active);
	return len;
}
static DEVICE_ATTR(hpd, S_IRUGO|S_IWUSR, tc358840_show_hpd, tc358840_store_hpd);

static ssize_t tc358840_show_hdmirx(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_data *td = g_td;
	int len = 0;
	len += sprintf(buf+len, "%d\n", td->mode);
	return len;
}
static DEVICE_ATTR(hdmirx, S_IRUGO, tc358840_show_hdmirx, NULL);

static ssize_t tc358840_show_fps(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct tc_data *td = g_td;
	int len = 0;
	len += sprintf(buf+len, "%d\n", tc358840_fps_list[td->fps]);
	return len;
}
static DEVICE_ATTR(fps, S_IRUGO, tc358840_show_fps, NULL);


static int tc358840_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	int retval;
	struct tc_data *td;
	struct sensor_data *sensor;
	u8 chip_id_high;
	u32 u32val;
	int mode = tc358840_mode_unknown;
	td = kzalloc(sizeof(*td), GFP_KERNEL);
	if (!td) { return -ENOMEM; }
	td->hpd_active = 1;
	td->det_work_timeout = DET_WORK_TIMEOUT_DEFAULT;
	td->audio = 2;
	mutex_init(&td->access_lock);
	mutex_lock(&td->access_lock);
	sensor = &td->sensor;
	/* request power down pin */
	td->pwn_gpio = of_get_named_gpio(dev->of_node, "pwn-gpios", 0);
	if (!gpio_is_valid(td->pwn_gpio))  { dev_warn(dev, "no sensor pwdn pin available"); } 
        else 
        {
		retval = devm_gpio_request_one(dev, td->pwn_gpio, GPIOF_OUT_INIT_HIGH,"tc_mipi_pwdn");
		if (retval < 0) { return retval;}
			//dev_warn(dev, "request of pwn_gpio failed");
	}
	/* request reset pin */
	td->rst_gpio = of_get_named_gpio(dev->of_node, "rst-gpios", 0);
	if (!gpio_is_valid(td->rst_gpio)) { return -EINVAL;}
		//dev_warn(dev, "no sensor reset pin available");
	retval = devm_gpio_request_one(dev, td->rst_gpio, GPIOF_OUT_INIT_HIGH,"tc_mipi_reset");
	if (retval < 0) {return retval;}
		//dev_warn(dev, "request of tc_mipi_reset failed");
	/* Set initial values for the sensor struct. */
	sensor->mipi_camera = 1;
	sensor->virtual_channel = 0;
	sensor->sensor_clk = devm_clk_get(dev, "csi_mclk");
	retval = of_property_read_u32(dev->of_node, "mclk",&(sensor->mclk));
	if (retval) { return retval;}
		//dev_err(dev, "mclk missing or invalid\n");
	retval = of_property_read_u32(dev->of_node, "mclk_source",(u32 *) &(sensor->mclk_source));
	if (retval) { return retval;}
		//dev_err(dev, "mclk_source missing or invalid\n");
	retval = of_property_read_u32(dev->of_node, "ipu_id",&sensor->ipu_id);
	if (retval) { return retval;}
		//dev_err(dev, "ipu_id missing or invalid\n");
	retval = of_property_read_u32(dev->of_node, "csi_id",&(sensor->csi));
	if (retval) { return retval;}
		//dev_err(dev, "csi id missing or invalid\n");
	if ((unsigned)sensor->ipu_id || (unsigned)sensor->csi) { return -EINVAL;}
		//dev_err(dev, "invalid ipu/csi\n");
	if (!IS_ERR(sensor->sensor_clk)) { clk_prepare_enable(sensor->sensor_clk);}
	sensor->io_init = tc_io_init;
	sensor->i2c_client = client;
	sensor->streamcap.capability = V4L2_MODE_HIGHQUALITY | V4L2_CAP_TIMEPERFRAME;
	sensor->streamcap.capturemode = mode;
	sensor->streamcap.timeperframe.denominator = DEFAULT_FPS;
	sensor->streamcap.timeperframe.numerator = 1;
	sensor->pix.pixelformat = get_pixelformat(mode);
	sensor->pix.width = tc358840_mode_info_data[mode].width;
	sensor->pix.height = tc358840_mode_info_data[mode].height;
	/*sensor structure inited*/
	tc_regulator_init(td, dev);
	power_control(td, 1);
	tc_reset(td);
	u32val = 0;
	if (tc358840_read_reg(sensor, TC358743_CHIP_ID_HIGH_BYTE, &u32val) < 0) 
        {
		pr_err("%s:cannot find camera\n", __func__);
		retval = -ENODEV;
		goto err4;
	}
	chip_id_high = (u8)u32val;
	tc358840_int_device.priv = td;
	if(!g_td){g_td = td;}
	INIT_DELAYED_WORK(&td->det_work, tc_det_worker);
	if (sensor->i2c_client->irq) 
        {
		retval = request_irq(sensor->i2c_client->irq, tc358840_detect_handler,
				IRQF_SHARED | IRQF_TRIGGER_FALLING,"tc358840_det", td);
		if (retval < 0) 
			{ dev_warn(&sensor->i2c_client->dev,"cound not request det irq %d\n",sensor->i2c_client->irq);}
        }
	schedule_delayed_work(&td->det_work, msecs_to_jiffies(td->det_work_timeout));
	retval = tc358840_reset(td);
	if(retval){goto err4;}
	i2c_set_clientdata(client, td);
	mutex_unlock(&td->access_lock);
	retval = v4l2_int_device_register(&tc358840_int_device);
	mutex_lock(&td->access_lock);
	if (retval) 
        {
		pr_err("%s:  v4l2_int_device_register failed, error=%d\n",__func__, retval);
		goto err4;
	}
	power_control(td, 0);
	retval = device_create_file(dev, &dev_attr_fps);
	retval = device_create_file(dev, &dev_attr_hdmirx);
	retval = device_create_file(dev, &dev_attr_hpd);
	retval = device_create_file(dev, &dev_attr_regoffs);
	retval = device_create_file(dev, &dev_attr_regdump);
	if (retval) { goto err3;}
	mutex_unlock(&td->access_lock);
	dev_err(dev, "%s: finished, error=%d\n", __func__, retval);
	return retval;
err3:
	pr_err("%s:  create bin file failed, error=%d\n",__func__, retval);
	device_remove_file(dev, &dev_attr_fps);
	device_remove_file(dev, &dev_attr_hdmirx);
	device_remove_file(dev, &dev_attr_hpd);
	device_remove_file(dev, &dev_attr_regoffs);
	device_remove_file(dev, &dev_attr_regdump);
err4:
	power_control(td, 0);
	mutex_unlock(&td->access_lock);
	pr_err("%s: failed, error=%d\n", __func__, retval);
	if (g_td == td){g_td = NULL;}
	mutex_destroy(&td->access_lock);
	kfree(td);
	return retval;
}





/*!
 * tc358840 I2C detach function
 *
 * @param client	    struct i2c_client *
 * @return  Error code indicating success or failure
 */
static int tc358840_remove(struct i2c_client *client)
{
	int i;
	struct tc_data *td = i2c_get_clientdata(client);
	struct sensor_data *sensor = &td->sensor;
	// Stop delayed work
	cancel_delayed_work_sync(&td->det_work);
	mutex_lock(&td->access_lock);
	power_control(td, 0);
	// Remove IRQ
	if (sensor->i2c_client->irq) { free_irq(sensor->i2c_client->irq,  sensor);}
	/*Remove sysfs entries*/
	device_remove_file(&client->dev, &dev_attr_fps);
	device_remove_file(&client->dev, &dev_attr_hdmirx);
	device_remove_file(&client->dev, &dev_attr_hpd);
	device_remove_file(&client->dev, &dev_attr_regoffs);
	device_remove_file(&client->dev, &dev_attr_regdump);
	mutex_unlock(&td->access_lock);
	v4l2_int_device_unregister(&tc358840_int_device);
	for (i = REGULATOR_CNT - 1; i >= 0; i--) 
        {
		if (td->regulator[i]) { regulator_disable(td->regulator[i]); }
	}
	mutex_destroy(&td->access_lock);
	if (g_td == td) { g_td = NULL;}
	kfree(td);
	return 0;
}

/*!
 * tc358840 init function
 * Called by insmod tc358840_camera.ko.
 *
 * @return  Error code indicating success or failure
 */
static __init int tc358840_init(void)
{
	int err;
	err = i2c_add_driver(&tc358840_i2c_driver);
	if(err != 0) { pr_err("%s:driver registration failed, error=%d\n",__func__, err);}
	return err;
}

/*!
 * tc358840 cleanup function
 * Called on rmmod tc358743_camera.ko
 *
 * @return  Error code indicating success or failure
 */
static void __exit tc358840_clean(void)
{
	i2c_del_driver(&tc358840_i2c_driver);
}

module_init(tc358840_init);
module_exit(tc358840_clean);

MODULE_AUTHOR("ELEPS.");
MODULE_DESCRIPTION("Toshiba TC358840 HDMI-to-CSI2 Bridge MIPI Input Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS("CSI");

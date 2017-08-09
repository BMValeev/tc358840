#define CODEC_CLOCK 16500000
/* SSI clock sources */
#define IMX_SSP_SYS_CLK		0

#define MIN_FPS 50
#define MAX_FPS 60
#define DEFAULT_FPS 50

#define TC358743_XCLK_MIN 27000000
#define TC358743_XCLK_MAX 42000000

#define TC358743_CHIP_ID_HIGH_BYTE		0x0
#define TC358743_CHIP_ID_LOW_BYTE		0x0
#define TC3587430_HDMI_DETECT			0x0f //0x10

#define TC_VOLTAGE_DIGITAL_IO           1800000
#define TC_VOLTAGE_DIGITAL_CORE         1500000
#define TC_VOLTAGE_DIGITAL_GPO		1500000
#define TC_VOLTAGE_ANALOG               2800000

#define MAX_COLORBAR	tc358840_mode_unknown
#define IS_COLORBAR(a) (a <= MAX_COLORBAR)

#define FV_CNT_LO                             0x85A1 /* Not in REF_01 */
#define FV_CNT_HI                             0x85A2 /* Not in REF_01 */
#define REGULATOR_IO		0
#define REGULATOR_CORE		1
#define REGULATOR_GPO		2
#define REGULATOR_ANALOG	3
#define REGULATOR_CNT		4
#define DET_WORK_TIMEOUT_DEFAULT 100
#define DET_WORK_TIMEOUT_DEFERRED 2000
#define MAX_BOUNCE 5



/*registers*/
#define HDMI_8520_S 0x10

struct reg_value 
{
	u16 u16RegAddr;
	u32 u32Val;
	u32 u32Mask;
	u8  u8Length;
	u32 u32Delay_ms;
};


enum tc358840_mode 
{
	tc358840_mode_unknown /*not found */,
	tc358840_mode_VGA /*(631-649),(471-489)*/,
	tc358840_mode_240p /*(1401-1449),(231-249)*/,
	tc358840_mode_288p /*(1401-1449),(279-297)*/,
	tc358840_mode_w240p /*(2801-2899),(231-249)*/,
	tc358840_mode_w288p /*(2801-2899),(279-297)*/,
	tc358840_mode_480p /*(701-729),(471-489)*/,
	tc358840_mode_576p /*(2801-2899),(279-297)*/,
	tc358840_mode_w480p /*(2801-2899),(279-297)*/,
	tc358840_mode_w576p /*(2801-2899),(279-297)*/,
	tc358840_mode_ww480p /*(2801-2899),(279-297)*/,
	tc358840_mode_ww576p /*(2801-2899),(279-297)*/,
	tc358840_mode_720p /*(2801-2899),(279-297)*/,
   	tc358840_mode_1035i /*(2801-2899),(279-297)*/,
	tc358840_mode_1080i /*(2801-2899),(279-297)*/,
	tc358840_mode_1080p /*(2801-2899),(279-297)*/,
};


enum tc358840_frame_rate 
{
	tc358840_50_fps,
	tc358840_60_fps,
	tc358840_max_fps
};
static char tc358840_fps_list[tc358840_max_fps+1] =
{
	[tc358840_50_fps] = 50,
	[tc358840_60_fps] = 60,
	[tc358840_max_fps] = 0
};
/*!
 * Maintains the information on the current state of the sensor.
 */
struct tc_data 
{
	struct sensor_data sensor;
	struct delayed_work det_work;
	struct mutex access_lock;
	int det_work_enable;
	int det_work_timeout;
	int det_changed;
	struct regulator *regulator[REGULATOR_CNT];
	u32 lock;
	u32 bounce;
	enum tc358840_mode mode;
	u32 fps;
	u32 audio;
	int pwn_gpio;
	int rst_gpio;
	u16 hpd_active;
	int edid_initialized;
};

static struct tc_data *g_td;

static void tc_standby(struct tc_data *td, s32 standby)
{
	if(gpio_is_valid(td->pwn_gpio)){gpio_set_value(td->pwn_gpio, standby ? 1 : 0);}
	pr_debug("tc_standby: powerdown=%x, power_gp=0x%x\n", standby, td->pwn_gpio);
	msleep(2);
}

static void tc_reset(struct tc_data *td)
{
	/* camera reset */
	gpio_set_value(td->rst_gpio, 1);
	/* camera power dowmn */
	if (gpio_is_valid(td->pwn_gpio)) 
	{
		gpio_set_value(td->pwn_gpio, 1);
		msleep(5);
		gpio_set_value(td->pwn_gpio, 0);
	}
	msleep(5);
	gpio_set_value(td->rst_gpio, 0);
	msleep(1);
	gpio_set_value(td->rst_gpio, 1);
	msleep(20);
	if(gpio_is_valid(td->pwn_gpio)){ gpio_set_value(td->pwn_gpio, 1);}
}

static void tc_io_init(void)
{
	return tc_reset(g_td);
}

const char * const sregulator[REGULATOR_CNT] = 
{
	[REGULATOR_IO] = "DOVDD",
	[REGULATOR_CORE] = "DVDD",
	[REGULATOR_GPO] = "DGPO",
	[REGULATOR_ANALOG] = "AVDD",
};

static const int voltages[REGULATOR_CNT] = 
{
	[REGULATOR_IO] = TC_VOLTAGE_DIGITAL_IO,
	[REGULATOR_CORE] = TC_VOLTAGE_DIGITAL_CORE,
	[REGULATOR_GPO] = TC_VOLTAGE_DIGITAL_GPO,
	[REGULATOR_ANALOG] = TC_VOLTAGE_ANALOG,
};

static int tc_regulator_init(struct tc_data *td, struct device *dev)
{
	int i;
	int ret = 0;
	for (i = 0; i < REGULATOR_CNT; i++) 
	{
		td->regulator[i] = devm_regulator_get(dev, sregulator[i]);
		if(!IS_ERR(td->regulator[i]))	
			{regulator_set_voltage(td->regulator[i], voltages[i], voltages[i]);}
                else    {td->regulator[i] = NULL;}
			//pr_err("%s:%s devm_regulator_get failed\n", __func__, sregulator[i]);
	}
	return ret;
}

static void det_work_enable(struct tc_data *td, int enable)
{
	td->det_work_enable = enable;
	td->det_work_timeout = DET_WORK_TIMEOUT_DEFERRED;
	if(enable) { schedule_delayed_work(&td->det_work, msecs_to_jiffies(10));}
	pr_debug("%s: %d %d\n", __func__, td->det_work_enable, td->det_work_timeout);
}







static const struct v4l2_fmtdesc tc358840_formats[] = 
{
	{
		.description	= "RGB888 (RGB24)",
		.pixelformat	= V4L2_PIX_FMT_RGB24,		/* 24  RGB-8-8-8     */
		.flags		= MIPI_DT_RGB888		//	0x24
	},
	{
		.description	= "RAW12 (Y/CbCr 4:2:0)",
		.pixelformat	= V4L2_PIX_FMT_UYVY,		/* 12  Y/CbCr 4:2:0  */
		.flags		= MIPI_DT_RAW12			//	0x2c
	},
	{
		.description	= "YUV 4:2:2 8-bit",
		.pixelformat	= V4L2_PIX_FMT_YUYV, 		/*  8  8-bit color   */
		.flags		= MIPI_DT_YUV422		//	0x1e /* UYVY...		*/
	},
};
struct tc358840_mode_info 
{
	const char *name;
	enum tc358840_mode mode;
	u32 width;
	u32 height;
	u32 vformat;
	u32 fps;
	u32 lanes;
	u32 freq;
	const struct reg_value *init_data_ptr;
	u32 init_data_size;
	__u32 flags;
};
static const struct reg_value tc358840_unknown_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_VGA_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_240p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_288p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_w240p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_w288p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_480p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_576p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_w480p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_w576p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_ww480p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_ww576p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_720p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_1035i_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_1080i_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};
static const struct reg_value tc358840_1080p_settings[] = 
{
{0x0006, 0x00000000, 0x00000000, 2, 0},
};

static const struct tc358840_mode_info tc358840_mode_info_data[tc358840_mode_1080p+1] = 
{
	[tc358840_mode_unknown] =
		{"unknown", tc358840_mode_unknown, 640, 480,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_unknown_settings,ARRAY_SIZE(tc358840_unknown_settings),
		MIPI_DT_YUV422},	
	[tc358840_mode_VGA] =
		{"unknown", tc358840_mode_unknown, 640, 480,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_VGA_settings,ARRAY_SIZE(tc358840_VGA_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_240p] =
		{"unknown", tc358840_mode_unknown, 640, 240,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_240p_settings,ARRAY_SIZE(tc358840_240p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_288p] =
		{"unknown", tc358840_mode_unknown, 640, 288,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_288p_settings,ARRAY_SIZE(tc358840_288p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_w240p] =
		{"unknown", tc358840_mode_unknown, 640, 240,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_w240p_settings,ARRAY_SIZE(tc358840_w240p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_w288p] =
		{"unknown", tc358840_mode_unknown, 640, 288,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_w288p_settings,ARRAY_SIZE(tc358840_w288p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_480p] =
		{"unknown", tc358840_mode_unknown, 640, 480,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_480p_settings,ARRAY_SIZE(tc358840_480p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_576p] =
		{"unknown", tc358840_mode_unknown, 640, 576,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_576p_settings,ARRAY_SIZE(tc358840_576p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_w480p] =
		{"unknown", tc358840_mode_unknown, 640, 480,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_w480p_settings,ARRAY_SIZE(tc358840_w480p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_w576p] =
		{"unknown", tc358840_mode_unknown, 640, 576,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_w576p_settings,ARRAY_SIZE(tc358840_w576p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_ww480p] =
		{"unknown", tc358840_mode_unknown, 640, 480,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_ww480p_settings,ARRAY_SIZE(tc358840_ww480p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_ww576p] =
		{"unknown", tc358840_mode_unknown, 640, 576,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_ww576p_settings,ARRAY_SIZE(tc358840_ww576p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_720p] =
		{"unknown", tc358840_mode_unknown, 640, 720,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_720p_settings,ARRAY_SIZE(tc358840_720p_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_1035i] =
		{"unknown", tc358840_mode_unknown, 640, 1035,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_1035i_settings,ARRAY_SIZE(tc358840_1035i_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_1080i] =
		{"unknown", tc358840_mode_unknown, 640, 1080,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_1080i_settings,ARRAY_SIZE(tc358840_1080i_settings),
		MIPI_DT_YUV422},
	[tc358840_mode_1080p] =
		{"unknown", tc358840_mode_unknown, 640, 1080,1, (0x02)<<8|(0x00), 2, 125,
		tc358840_1080p_settings,ARRAY_SIZE(tc358840_1080p_settings),
		MIPI_DT_YUV422}
};


















/*
static const struct tc358743_mode_info tc358743_mode_info_data[tc358743_max_fps][tc358743_mode_MAX] = 
{
	[tc358743_30_fps][tc358743_mode_INIT] =
		{"cb640x480-108MHz@30", tc358743_mode_INIT,  640, 480,6, 1, 2, 108,
		tc358743_setting_YUV422_2lane_color_bar_640_480_108MHz_cont,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_color_bar_640_480_108MHz_cont),MIPI_DT_YUV422
		},
	[tc358743_60_fps][tc358743_mode_INIT] =
		{"cb640x480-108MHz@60", tc358743_mode_INIT,  640, 480,6, 1, 2, 108,
		tc358743_setting_YUV422_2lane_color_bar_640_480_108MHz_cont,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_color_bar_640_480_108MHz_cont),MIPI_DT_YUV422
		},

	[tc358743_30_fps][tc358743_mode_INIT4] =
		{"cb640x480-174Mhz@30", tc358743_mode_INIT4,  640, 480,6, 1, 2, 174,
		tc358743_setting_YUV422_2lane_color_bar_640_480_174MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_color_bar_640_480_174MHz),MIPI_DT_YUV422
		},
	[tc358743_60_fps][tc358743_mode_INIT4] =
		{"cb640x480-174MHz@60", tc358743_mode_INIT4,  640, 480,6, 1, 2, 174,
		tc358743_setting_YUV422_2lane_color_bar_640_480_174MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_color_bar_640_480_174MHz),MIPI_DT_YUV422
		},

	[tc358743_30_fps][tc358743_mode_INIT3] =
		{"cb1024x720-4lane@30", tc358743_mode_INIT3,  1024, 720,6, 1, 4, 300,
		tc358743_setting_YUV422_4lane_color_bar_1024_720_200MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_color_bar_1024_720_200MHz),MIPI_DT_YUV422
		},
	[tc358743_60_fps][tc358743_mode_INIT3] =
		{"cb1024x720-4lane@60", tc358743_mode_INIT3,  1024, 720,6, 1, 4, 300,
		tc358743_setting_YUV422_4lane_color_bar_1024_720_200MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_color_bar_1024_720_200MHz),MIPI_DT_YUV422
		},
	[tc358743_30_fps][tc358743_mode_INIT1] =
		{"cb1280x720-2lane@30", tc358743_mode_INIT1,  1280, 720,12, 0, 2, 125,
		tc358743_setting_YUV422_2lane_color_bar_1280_720_125MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_color_bar_1280_720_125MHz),MIPI_DT_YUV422
		},
	[tc358743_60_fps][tc358743_mode_INIT1] =
		{"cb1280x720-2lane@60", tc358743_mode_INIT1,  1280, 720,12, 0, 2, 125,
		tc358743_setting_YUV422_2lane_color_bar_1280_720_125MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_color_bar_1280_720_125MHz),MIPI_DT_YUV422
		},
	[tc358743_30_fps][tc358743_mode_INIT2] =
		{"cb1280x720-4lane-125MHz@30", tc358743_mode_INIT2,  1280, 720,12, 0, 4, 125,
		tc358743_setting_YUV422_4lane_color_bar_1280_720_125MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_color_bar_1280_720_125MHz),MIPI_DT_YUV422
		},
	[tc358743_60_fps][tc358743_mode_INIT2] =
		{"cb1280x720-4lane-125MHz@60", tc358743_mode_INIT2,  1280, 720,12, 0, 4, 125,
		tc358743_setting_YUV422_4lane_color_bar_1280_720_125MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_color_bar_1280_720_125MHz),MIPI_DT_YUV422
		},
	[tc358743_30_fps][tc358743_mode_INIT5] =
		{"cb1280x720-4lane-300MHz@30", tc358743_mode_INIT5,  1280, 720,12, 0, 4, 300,
		tc358743_setting_YUV422_4lane_color_bar_1280_720_300MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_color_bar_1280_720_300MHz),MIPI_DT_YUV422
		},
	[tc358743_60_fps][tc358743_mode_INIT5] =
		{"cb1280x720-4lane-300MHz@60", tc358743_mode_INIT5,  1280, 720,12, 0, 4, 300,
		tc358743_setting_YUV422_4lane_color_bar_1280_720_300MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_color_bar_1280_720_300MHz),MIPI_DT_YUV422
		},
	[tc358743_30_fps][tc358743_mode_INIT6] =
		{"cb1920x1023@30", tc358743_mode_INIT6,  1920, 1023,15, 0, 4, 300,
		tc358743_setting_YUV422_4lane_color_bar_1920_1023_300MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_color_bar_1920_1023_300MHz),MIPI_DT_YUV422
		},
	[tc358743_60_fps][tc358743_mode_INIT6] =
		{"cb1920x1023@60", tc358743_mode_INIT6,  1920, 1023,15, 0, 4, 300,
		tc358743_setting_YUV422_4lane_color_bar_1920_1023_300MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_color_bar_1920_1023_300MHz),MIPI_DT_YUV422
		},
	[tc358743_60_fps][tc358743_mode_480P_640_480] =
		{"640x480@60", tc358743_mode_480P_640_480, 640, 480,1, (0x02)<<8|(0x00), 2, 125,
		tc358743_setting_YUV422_2lane_60fps_640_480_125Mhz,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_60fps_640_480_125Mhz),MIPI_DT_YUV422,
		},
	[tc358743_30_fps][tc358743_mode_480P_720_480] =
		{"720x480@30", tc358743_mode_480P_720_480,  720, 480,6, (0x02)<<8|(0x00), 2, 125,
		tc358743_setting_YUV422_2lane_60fps_720_480_125Mhz,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_60fps_720_480_125Mhz),MIPI_DT_YUV422,
		},
	[tc358743_60_fps][tc358743_mode_480P_720_480] =
		{"720x480@60", tc358743_mode_480P_720_480,  720, 480,6, (0x02)<<8|(0x00), 2, 125,
		tc358743_setting_YUV422_2lane_60fps_720_480_125Mhz,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_60fps_720_480_125Mhz),MIPI_DT_YUV422,
		},


	[tc358743_60_fps][tc358743_mode_1024x768] =
		{"1024x768@60", tc358743_mode_1024x768,  1024, 768,16, 60, 4, 125,
		tc358743_setting_YUV422_4lane_1024x768_60fps_125MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_1024x768_60fps_125MHz),MIPI_DT_YUV422
		},
	[tc358743_75_fps][tc358743_mode_1024x768] =
		{"1024x768@75", tc358743_mode_1024x768,  1024, 768,16, 75, 4, 125,
		tc358743_setting_YUV422_4lane_1024x768_75fps_300MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_1024x768_75fps_300MHz),MIPI_DT_YUV422
		},


	[tc358743_30_fps][tc358743_mode_720P_1280_720] =
		{"1280x720-2lane@30", tc358743_mode_720P_1280_720,  1280, 720,12, (0x3e)<<8|(0x3c), 2, 125,
		tc358743_setting_YUV422_2lane_30fps_720P_1280_720_125MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_30fps_720P_1280_720_125MHz),MIPI_DT_YUV422,
		},
	[tc358743_60_fps][tc358743_mode_720P_1280_720] =
		{"1280x720-2lane@60", tc358743_mode_720P_1280_720,  1280, 720,12, (0x3e)<<8|(0x3c), 2, 125,
		tc358743_setting_YUV422_2lane_30fps_720P_1280_720_125MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_2lane_30fps_720P_1280_720_125MHz),MIPI_DT_YUV422,
		},

	[tc358743_30_fps][tc358743_mode_720P_60_1280_720] =
		{"1280x720-4lane-133Mhz@30", tc358743_mode_720P_60_1280_720,  1280, 720,12, 0, 4, 133,
		tc358743_setting_YUV422_4lane_720P_60fps_1280_720_133Mhz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_720P_60fps_1280_720_133Mhz),MIPI_DT_YUV422
		},
	[tc358743_60_fps][tc358743_mode_720P_60_1280_720] =
		{"1280x720-4lane@60", tc358743_mode_720P_60_1280_720,  1280, 720,12, 0, 4, 133,
		tc358743_setting_YUV422_4lane_720P_60fps_1280_720_133Mhz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_720P_60fps_1280_720_133Mhz),MIPI_DT_YUV422
		},

	[tc358743_30_fps][tc358743_mode_1080P_1920_1080] =
		{"1920x1080@30", tc358743_mode_1080P_1920_1080,  1920, 1080,15, 0xa, 4, 300,
		tc358743_setting_YUV422_4lane_1080P_30fps_1920_1080_300MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_1080P_30fps_1920_1080_300MHz),MIPI_DT_YUV422
		},
	[tc358743_60_fps][tc358743_mode_1080P_1920_1080] =
		{"1920x1080@60", tc358743_mode_1080P_1920_1080,  1920, 1080,15, 0x0b, 4, 300,
		tc358743_setting_YUV422_4lane_1080P_60fps_1920_1080_300MHz,
		ARRAY_SIZE(tc358743_setting_YUV422_4lane_1080P_60fps_1920_1080_300MHz),MIPI_DT_YUV422
		},
};
*/
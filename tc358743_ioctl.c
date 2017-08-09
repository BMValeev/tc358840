
/* --------------- IOCTL functions from v4l2_int_ioctl_desc --------------- */

static int ioctl_g_ifparm(struct v4l2_int_device *s, struct v4l2_ifparm *p)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;

	pr_debug("%s\n", __func__);

	memset(p, 0, sizeof(*p));
	p->u.bt656.clock_curr = TC358743_XCLK_MIN; //sensor->mclk;
	pr_debug("%s: clock_curr=mclk=%d\n", __func__, sensor->mclk);
	p->if_type = V4L2_IF_TYPE_BT656;
	p->u.bt656.mode = V4L2_IF_TYPE_BT656_MODE_NOBT_8BIT;
	p->u.bt656.clock_min = TC358743_XCLK_MIN;
	p->u.bt656.clock_max = TC358743_XCLK_MAX;

	return 0;
}

/*!
 * ioctl_s_power - V4L2 sensor interface handler for VIDIOC_S_POWER ioctl
 * @s: pointer to standard V4L2 device structure
 * @on: indicates power mode (on or off)
 *
 * Turns the power on or off, depending on the value of on and returns the
 * appropriate error code.
 */
static int ioctl_s_power(struct v4l2_int_device *s, int on)
{
	struct tc_data *td = s->priv;
	int ret;

	mutex_lock(&td->access_lock);
	ret =(on && !td->mode) ? tc358840_reset(td): power_control(td, on);
	mutex_unlock(&td->access_lock);
	return ret;
}
/*GET SET parameters for streaming*/
/*!
 * ioctl_g_parm - V4L2 sensor interface handler for VIDIOC_G_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_G_PARM ioctl structure
 *
 * Returns the sensor's video CAPTURE parameters.
 */
static int ioctl_g_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;
	struct v4l2_captureparm *cparm = &a->parm.capture;
	int ret = 0;
	pr_debug("%s type: %x\n", __func__, a->type);
	switch (a->type) 
	{
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		memset(a, 0, sizeof(*a));
		a->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		cparm->capability = sensor->streamcap.capability;
		cparm->timeperframe = sensor->streamcap.timeperframe;
		cparm->capturemode = sensor->streamcap.capturemode;
		cparm->extendedmode = sensor->streamcap.extendedmode;
		ret = 0;
		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		ret = -EINVAL;
		break;

	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}
	pr_debug("%s done %d\n", __func__, ret);
	return ret;
}

/*!
 * ioctl_s_parm - V4L2 sensor interface handler for VIDIOC_S_PARM ioctl
 * @s: pointer to standard V4L2 device structure
 * @a: pointer to standard V4L2 VIDIOC_S_PARM ioctl structure
 *
 * Configures the sensor to use the input parameters, if possible.  If
 * not possible, reverts to the old parameters and returns the
 * appropriate error code.
 */
static int ioctl_s_parm(struct v4l2_int_device *s, struct v4l2_streamparm *a)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;
	struct v4l2_fract *timeperframe = &a->parm.capture.timeperframe;
	u32 tgt_fps;	/* target frames per secound */
	enum tc358840_frame_rate frame_rate = tc358840_60_fps, frame_rate_now = tc358840_60_fps;
	enum tc358840_mode mode;
	int ret = 0;
	pr_debug("%s\n", __func__);
	mutex_lock(&td->access_lock);
	det_work_enable(td, 0);
	/* Make sure power on */
	power_control(td, 1);
	switch (a->type) {
	/* This is the only case currently handled. */
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		/* Check that the new frame rate is allowed. */
		if ((timeperframe->numerator == 0) ||  (timeperframe->denominator == 0)) 
		{
			timeperframe->denominator = DEFAULT_FPS;
			timeperframe->numerator = 1;
		}
		tgt_fps = timeperframe->denominator /timeperframe->numerator;

		if (tgt_fps > MAX_FPS) 
		{
			timeperframe->denominator = MAX_FPS;
			timeperframe->numerator = 1;
		}
 		else if (tgt_fps < MIN_FPS) 
		{
			timeperframe->denominator = MIN_FPS;
			timeperframe->numerator = 1;
		}

		/* Actual frame rate we use */
		tgt_fps = timeperframe->denominator /  timeperframe->numerator;
		frame_rate = tc_fps_to_index(tgt_fps);
		if (frame_rate < 0) 
		{
			pr_err(" The camera frame rate is not supported!\n");
			ret = -EINVAL;
			break;
		}

		if ((u32)a->parm.capture.capturemode > tc358840_mode_1080p) 
		{
			a->parm.capture.capturemode = 0;
			pr_debug("%s: Force mode: %d \n", __func__,(u32)a->parm.capture.capturemode);
		}

		tgt_fps = sensor->streamcap.timeperframe.denominator / sensor->streamcap.timeperframe.numerator;
		frame_rate_now = tc_fps_to_index(tgt_fps);
		mode = td->mode;
		if (IS_COLORBAR(mode)) 
		{
			mode = (u32)a->parm.capture.capturemode;
		} 
		else 
		{
			a->parm.capture.capturemode = mode;
			frame_rate = td->fps;
			timeperframe->denominator = tc358840_fps_list[frame_rate];
			timeperframe->numerator = 1;
		}

		if (frame_rate_now != frame_rate || sensor->streamcap.capturemode != mode ||
		   sensor->streamcap.extendedmode != (u32)a->parm.capture.extendedmode) 
		{
			if (mode != tc358840_mode_unknown) 
			{
				sensor->streamcap.capturemode = mode;
				sensor->streamcap.timeperframe = *timeperframe;
				sensor->streamcap.extendedmode =(u32)a->parm.capture.extendedmode;
				pr_debug("%s: capture mode: %d\n", __func__,mode);
				ret = tc358840_init_mode(td, frame_rate, mode);
			} 
			else 
			{
				a->parm.capture.capturemode = sensor->streamcap.capturemode;
				*timeperframe = sensor->streamcap.timeperframe;
				a->parm.capture.extendedmode = sensor->streamcap.extendedmode;
			}
		} 
		else 
		{
			pr_debug("%s: Keep current settings\n", __func__);
		}
		break;

	/* These are all the possible cases. */
	case V4L2_BUF_TYPE_VIDEO_OUTPUT:
	case V4L2_BUF_TYPE_VIDEO_OVERLAY:
	case V4L2_BUF_TYPE_VBI_CAPTURE:
	case V4L2_BUF_TYPE_VBI_OUTPUT:
	case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
	case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
		pr_debug("   type is not V4L2_BUF_TYPE_VIDEO_CAPTURE but %d\n",a->type);
		ret = -EINVAL;
		break;
	default:
		pr_debug("   type is unknown - %d\n", a->type);
		ret = -EINVAL;
		break;
	}

	det_work_enable(td, 1);
	mutex_unlock(&td->access_lock);
	return ret;
}
/* Get or set the value of a control*/
/*!
 * ioctl_g_ctrl - V4L2 sensor interface handler for VIDIOC_G_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_G_CTRL ioctl structure
 *
 * If the requested control is supported, returns the control's current
 * value from the video_control[] array.  Otherwise, returns -EINVAL
 * if the control is not supported.
 */
static int ioctl_g_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;
	int ret = 0;

	pr_debug("%s\n", __func__);
	switch (vc->id) 
	{
	case V4L2_CID_BRIGHTNESS:
		vc->value = sensor->brightness;
		break;
	case V4L2_CID_HUE:
		vc->value = sensor->hue;
		break;
	case V4L2_CID_CONTRAST:
		vc->value = sensor->contrast;
		break;
	case V4L2_CID_SATURATION:
		vc->value = sensor->saturation;
		break;
	case V4L2_CID_RED_BALANCE:
		vc->value = sensor->red;
		break;
	case V4L2_CID_BLUE_BALANCE:
		vc->value = sensor->blue;
		break;
	case V4L2_CID_EXPOSURE:
		vc->value = sensor->ae_mode;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/*!
 * ioctl_s_ctrl - V4L2 sensor interface handler for VIDIOC_S_CTRL ioctl
 * @s: pointer to standard V4L2 device structure
 * @vc: standard V4L2 VIDIOC_S_CTRL ioctl structure
 *
 * If the requested control is supported, sets the control's current
 * value in HW (and updates the video_control[] array).  Otherwise,
 * returns -EINVAL if the control is not supported.
 */
static int ioctl_s_ctrl(struct v4l2_int_device *s, struct v4l2_control *vc)
{
	int retval = 0;

	pr_debug("In tc358840:ioctl_s_ctrl %d\n",
		 vc->id);

	switch (vc->id) 
	{
	case V4L2_CID_BRIGHTNESS:
		break;
	case V4L2_CID_CONTRAST:
		break;
	case V4L2_CID_SATURATION:
		break;
	case V4L2_CID_HUE:
		break;
	case V4L2_CID_AUTO_WHITE_BALANCE:
		break;
	case V4L2_CID_DO_WHITE_BALANCE:
		break;
	case V4L2_CID_RED_BALANCE:
		break;
	case V4L2_CID_BLUE_BALANCE:
		break;
	case V4L2_CID_GAMMA:
		break;
	case V4L2_CID_EXPOSURE:
		break;
	case V4L2_CID_AUTOGAIN:
		break;
	case V4L2_CID_GAIN:
		break;
	case V4L2_CID_HFLIP:
		break;
	case V4L2_CID_VFLIP:
		break;
	default:
		retval = -EPERM;
		break;
	}

	return retval;
}
/*Enumerate frame sizes*/
/*!
 * ioctl_enum_framesizes - V4L2 sensor interface handler for
 *			   VIDIOC_ENUM_FRAMESIZES ioctl
 * @s: pointer to standard V4L2 device structure
 * @fsize: standard V4L2 VIDIOC_ENUM_FRAMESIZES ioctl structure
 *
 * Return 0 if successful, otherwise -EINVAL.
 */
static int ioctl_enum_framesizes(struct v4l2_int_device *s,
				 struct v4l2_frmsizeenum *fsize)
{
	struct tc_data *td = s->priv;
	enum tc358840_mode query_mode= fsize->index;
	enum tc358840_mode mode = td->mode;
	if (IS_COLORBAR(mode)) 
	{
		if(query_mode > MAX_COLORBAR) {return -EINVAL;}
                else { mode = query_mode;}
	} 
	else 
	{
		if (query_mode){return -EINVAL;}
	}
	pr_debug("%s, mode: %d\n", __func__, mode);
	fsize->pixel_format = get_pixelformat(0, mode);
	fsize->discrete.width =  tc358840_mode_info_data[mode].width;
	fsize->discrete.height = tc358840_mode_info_data[mode].height;
	pr_debug("%s %d:%d format: %x\n", __func__, fsize->discrete.width, fsize->discrete.height, fsize->pixel_format);
	return 0;
}
/*Identify the chips on a TV card*/
/*!
 * ioctl_g_chip_ident - V4L2 sensor interface handler for
 *			VIDIOC_DBG_G_CHIP_IDENT ioctl
 * @s: pointer to standard V4L2 device structure
 * @id: pointer to int
 *
 * Return 0.
 */
static int ioctl_g_chip_ident(struct v4l2_int_device *s, int *id)
{
	((struct v4l2_dbg_chip_ident *)id)->match.type =V4L2_CHIP_MATCH_I2C_DRIVER;
	strcpy(((struct v4l2_dbg_chip_ident *)id)->match.name,"tc358840_mipi");
	return 0;
}

/*!
 * ioctl_init - V4L2 sensor interface handler for VIDIOC_INT_INIT
 * @s: pointer to standard V4L2 device structure
 */
static int ioctl_init(struct v4l2_int_device *s)
{
	pr_debug("%s\n", __func__);
	return 0;
}
/*Enumerate image formats*/
/*!
 * ioctl_enum_fmt_cap - V4L2 sensor interface handler for VIDIOC_ENUM_FMT
 * @s: pointer to standard V4L2 device structure
 * @fmt: pointer to standard V4L2 fmt description structure
 *
 * Return 0.
 */
static int ioctl_enum_fmt_cap(struct v4l2_int_device *s,
			      struct v4l2_fmtdesc *fmt)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;
	int index = fmt->index;

	if(!index){index = sensor->streamcap.capturemode;}
	pr_debug("%s, INDEX: %d\n", __func__, index);
	if(index > tc358840_mode_1080p){return -EINVAL;}
	fmt->pixelformat = get_pixelformat(0, index);
	pr_debug("%s: format: %x\n", __func__, fmt->pixelformat);
	return 0;
}

static int ioctl_try_fmt_cap(struct v4l2_int_device *s,
			     struct v4l2_format *f)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;
	u32 tgt_fps;	/* target frames per secound */
	enum tc358840_frame_rate frame_rate;
	int ret = 0;
	struct v4l2_pix_format *pix = &f->fmt.pix;
	int mode;
	pr_debug("%s\n", __func__);
	mutex_lock(&td->access_lock);
	tgt_fps = sensor->streamcap.timeperframe.denominator / sensor->streamcap.timeperframe.numerator;
	frame_rate = tc_fps_to_index(tgt_fps);
	if (frame_rate < 0) 
	{
		pr_debug("%s: %d fps (%d,%d) is not supported\n", __func__,
			 tgt_fps, sensor->streamcap.timeperframe.denominator,
			 sensor->streamcap.timeperframe.numerator);
		ret = -EINVAL;
		goto out;
	}
	mode = sensor->streamcap.capturemode;
	sensor->pix.pixelformat = get_pixelformat(frame_rate, mode);
	sensor->pix.width = pix->width = tc358840_mode_info_data[mode].width;
	sensor->pix.height = pix->height = tc358840_mode_info_data[mode].height;
	pr_debug("%s: %dx%d\n", __func__, sensor->pix.width, sensor->pix.height);
	pix->pixelformat = sensor->pix.pixelformat;
	pix->field = V4L2_FIELD_NONE;
	pix->bytesperline = pix->width * 4;
	pix->sizeimage = pix->bytesperline * pix->height;
	pix->priv = 0;
	switch (pix->pixelformat) 
	{
	case V4L2_PIX_FMT_UYVY:
	default:
		pix->colorspace = V4L2_COLORSPACE_SRGB;
		break;
	}

/*	{
		pr_debug("SYS_STATUS: 0x%x\n", tc358743_read_reg_val(sensor, 0x8520));
		pr_debug("VI_STATUS0: 0x%x\n", tc358743_read_reg_val(sensor, 0x8521));
		pr_debug("VI_STATUS1: 0x%x\n", tc358743_read_reg_val(sensor, 0x8522));
		pr_debug("VI_STATUS2: 0x%x\n", tc358743_read_reg_val(sensor, 0x8525));
		pr_debug("VI_STATUS3: 0x%x\n", tc358743_read_reg_val(sensor, 0x8528));
		pr_debug("%s %d:%d format: %x\n", __func__, pix->width, pix->height, pix->pixelformat);
	}*/
out:
	mutex_unlock(&td->access_lock);
	return ret;
}

/*!
 * ioctl_g_fmt_cap - V4L2 sensor interface handler for ioctl_g_fmt_cap
 * @s: pointer to standard V4L2 device structure
 * @f: pointer to standard V4L2 v4l2_format structure
 *
 * Returns the sensor's current pixel format in the v4l2_format
 * parameter.
 */
static int ioctl_g_fmt_cap(struct v4l2_int_device *s, struct v4l2_format *f)
{
	struct tc_data *td = s->priv;
	struct sensor_data *sensor = &td->sensor;
	int mode = sensor->streamcap.capturemode;
	sensor->pix.pixelformat = get_pixelformat(0, mode);
	sensor->pix.width = tc358840_mode_info_data[mode].width;
	sensor->pix.height = tc358840_mode_info_data[mode].height;

	switch (f->type) 
	{
	case V4L2_BUF_TYPE_VIDEO_CAPTURE:
		f->fmt.pix = sensor->pix;
		pr_debug("%s: %dx%d\n", __func__, sensor->pix.width, sensor->pix.height);
		break;

	case V4L2_BUF_TYPE_SENSOR:
		pr_debug("%s: left=%d, top=%d, %dx%d\n", __func__,
			sensor->spix.left, sensor->spix.top,
			sensor->spix.swidth, sensor->spix.sheight);
		f->fmt.spix = sensor->spix;
		break;

	case V4L2_BUF_TYPE_PRIVATE:
		pr_debug("%s: private\n", __func__);
		break;

	default:
		f->fmt.pix = sensor->pix;
		pr_debug("%s: type=%d, %dx%d\n", __func__, f->type, sensor->pix.width, sensor->pix.height);
		break;
	}
	return 0;
}

/*!
 * ioctl_dev_init - V4L2 sensor interface handler for vidioc_int_dev_init_num
 * @s: pointer to standard V4L2 device structure
 *
 * Initialise the device when slave attaches to the master.
 */
static int ioctl_dev_init(struct v4l2_int_device *s)
{
	struct tc_data *td = s->priv;

	if (td->det_changed) 
	{
		mutex_lock(&td->access_lock);
		td->det_changed = 0;
		pr_debug("%s\n", __func__);
		tc358840_minit(td);
		mutex_unlock(&td->access_lock);
	}
	pr_debug("%s\n", __func__);
	return 0;
}

/*!
 * ioctl_dev_exit - V4L2 sensor interface handler for vidioc_int_dev_exit_num
 * @s: pointer to standard V4L2 device structure
 *
 * Delinitialise the device when slave detaches to the master.
 */
static int ioctl_dev_exit(struct v4l2_int_device *s)
{
	void *mipi_csi2_info;
	mipi_csi2_info = mipi_csi2_get_info();
	/* disable mipi csi2 */
	if (mipi_csi2_info)
	{
		if(mipi_csi2_get_status(mipi_csi2_info)){mipi_csi2_disable(mipi_csi2_info);}
	}
	return 0;
}

/*!
 * This structure defines all the ioctls for this module and links them to the
 * enumeration.
 */
static struct v4l2_int_ioctl_desc tc358840_ioctl_desc[] = 
{
	{vidioc_int_dev_init_num, 	(v4l2_int_ioctl_func*) 	ioctl_dev_init		},
	{vidioc_int_dev_exit_num, 				ioctl_dev_exit		},
	{vidioc_int_s_power_num, 	(v4l2_int_ioctl_func*) 	ioctl_s_power		},
	{vidioc_int_g_ifparm_num, 	(v4l2_int_ioctl_func*) 	ioctl_g_ifparm		},
	{vidioc_int_init_num, 		(v4l2_int_ioctl_func*) 	ioctl_init		},
	{vidioc_int_enum_fmt_cap_num,	(v4l2_int_ioctl_func *) ioctl_enum_fmt_cap	},
	{vidioc_int_try_fmt_cap_num,	(v4l2_int_ioctl_func *)	ioctl_try_fmt_cap	},
	{vidioc_int_g_fmt_cap_num, 	(v4l2_int_ioctl_func *) ioctl_g_fmt_cap		},
	{vidioc_int_g_parm_num, 	(v4l2_int_ioctl_func *) ioctl_g_parm		},
	{vidioc_int_s_parm_num, 	(v4l2_int_ioctl_func *) ioctl_s_parm		},
	{vidioc_int_g_ctrl_num, 	(v4l2_int_ioctl_func *) ioctl_g_ctrl		},
	{vidioc_int_s_ctrl_num, 	(v4l2_int_ioctl_func *) ioctl_s_ctrl		},
	{vidioc_int_enum_framesizes_num,(v4l2_int_ioctl_func *) ioctl_enum_framesizes	},
	{vidioc_int_g_chip_ident_num,	(v4l2_int_ioctl_func *) ioctl_g_chip_ident	},
};

static struct v4l2_int_slave tc358840_slave = 
{
	.ioctls = tc358840_ioctl_desc,
	.num_ioctls = ARRAY_SIZE(tc358840_ioctl_desc),
};

static struct v4l2_int_device tc358840_int_device = 
{
	.module = THIS_MODULE,
	.name = "tc358840",
	.type = v4l2_int_type_slave,
	.u = 	{
		.slave = &tc358840_slave,
		},
};


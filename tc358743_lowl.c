
struct _reg_size
{
	u16 startaddr, endaddr;
	int size;
};
/*rewrite*/
static const struct _reg_size tc358840_read_reg_size[] =
{
	{0x0000, 0x005a, 2},
	{0x0100, 0x0110, 4},
	{0x0140, 0x0150, 4},
	{0x0204, 0x0238, 4},
	{0x040c, 0x0418, 4},
	{0x044c, 0x0454, 4},
	{0x0500, 0x0518, 4},
	{0x0600, 0x06cc, 4},
	{0x7000, 0x7100, 2},
	{0x8500, 0x8bff, 1},
	{0x8c00, 0x8fff, 4},
	{0x9000, 0x90ff, 1},
	{0x9100, 0x92ff, 1},
	{0, 0, 0},
};

/*Independante to the chip low level functions*/
int get_reg_size(u16 reg, int len)
{
	const struct _reg_size *p = tc358840_read_reg_size;
	int size;
	while (p->size) 
	{
		if ((p->startaddr <= reg) && (reg <= p->endaddr)) 
		{
			size = p->size;
			if(len && (size != len)) {return 0;}
				//pr_err("%s:reg len error:reg=%x %d instead of %d\n",__func__, reg, len, size);
			if(reg % size) {return 0;}
			//pr_err("%s:cannot read from the middle of a register, reg(%x) size(%d)\n",__func__, reg, size);
			return size;
		}
		p++;
	}
	//pr_err("%s:reg=%x size is not defined\n",__func__, reg);
	return 0;
}
static s32 tc358840_read_reg(struct sensor_data *sensor, u16 reg, void *rxbuf)
{
	struct i2c_client *client = sensor->i2c_client;
	struct i2c_msg msgs[2];
	u8 txbuf[2];
	int ret,size = get_reg_size(reg, 0);
	if (!size){return -EINVAL;}
	txbuf[0] = reg >> 8;
	txbuf[1] = reg & 0xff;
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = txbuf;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = size;
	msgs[1].buf = rxbuf;
	ret = i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0) { return ret;}
		//pr_err("%s:reg=%x ret=%d\n", __func__, reg, ret);
		//pr_debug("%s:reg=%x,val=%x\n", __func__, reg, ((char *)rxbuf)[0]);
	return 0;
}
static s32 tc358840_read_reg_val(struct sensor_data *sensor, u16 reg)
{
	u32 val = 0;
	tc358840_read_reg(sensor, reg, &val);
	return val;
}
static s32 tc358840_read_reg_val16(struct sensor_data *sensor, u16 reg)
{
	u32 val1 = 0;
	u32 val2 = 0;
	tc358840_read_reg(sensor, reg, &val1);
	tc358840_read_reg(sensor, reg+1, &val2);
	return val1 | (val2 << 8);

}
static s32 tc358840_write_reg(struct sensor_data *sensor, u16 reg, u32 val, int len)
{
	int ret;
	int i = 0;
	u32 data = val;
	u8 au8Buf[6] = {0};
	int size = get_reg_size(reg, len);
	if(!size){return -EINVAL;}
	au8Buf[i++] = reg >> 8;
	au8Buf[i++] = reg & 0xff;
	while (size-- > 0) 
	{
		au8Buf[i++] = (u8)data;
		data >>= 8;
	}

	ret = i2c_master_send(sensor->i2c_client, au8Buf, i);
	if(ret < 0) {return ret;}
		//pr_err("%s:write reg error(%d):reg=%x,val=%x\n",__func__, ret, reg, val);
	if((reg < 0x7000) || (reg >= 0x7100)) 
		{pr_debug("%s:reg=%x,val=%x 8543=%02x\n", __func__, reg, val, tc358840_read_reg_val(sensor, 0x8543));}
	return 0;
}


void mipi_csi2_swreset(struct mipi_csi2_info *info);
#include "../../../../mxc/mipi/mxc_mipi_csi2.h"

int mipi_reset(void *mipi_csi2_info,
		enum tc358840_frame_rate frame_rate,
		enum tc358840_mode mode)
{
	int lanes = tc358840_mode_info_data[mode].lanes;
	if(!lanes){lanes = 4;}
	if (mipi_csi2_get_status(mipi_csi2_info)) 
	{
		mipi_csi2_disable(mipi_csi2_info);
		msleep(1);
	}
	mipi_csi2_enable(mipi_csi2_info);
	if (!mipi_csi2_get_status(mipi_csi2_info)) {return -1;}
		//pr_err("Can not enable mipi csi2 driver!\n");
	lanes = mipi_csi2_set_lanes(mipi_csi2_info, lanes);
	//pr_debug("Now Using %d lanes\n", lanes);
	mipi_csi2_reset(mipi_csi2_info);
	mipi_csi2_set_datatype(mipi_csi2_info, tc358840_mode_info_data[mode].flags);
	return 0;
}

int mipi_wait(void *mipi_csi2_info)
{
	unsigned i = 0;
	unsigned j;
	u32 mipi_reg;
	u32 mipi_reg_test[10];

	/* wait for mipi sensor ready */
	for (;;) 
	{
		mipi_reg = mipi_csi2_dphy_status(mipi_csi2_info);
		mipi_reg_test[i++] = mipi_reg;
		if(mipi_reg != 0x200){break;}
		if(i >= 10) {return -1;}
			//pr_err("mipi csi2 can not receive sensor clk!\n");
		msleep(10);
	}

	for (j = 0; j < i; j++)  { pr_debug("%d  mipi csi2 dphy status %x\n", j, mipi_reg_test[j]); }
	i = 0;
	/* wait for mipi stable */
	for (;;) 
	{
		mipi_reg = mipi_csi2_get_error1(mipi_csi2_info);
		mipi_reg_test[i++] = mipi_reg;
		if(!mipi_reg) { break; }
		if(i >= 10) { return -1; }
			//pr_err("mipi csi2 can not reveive data correctly!\n"):;
		msleep(10);
	}
	for (j = 0; j < i; j++) { pr_debug("%d  mipi csi2 err1 %x\n", j, mipi_reg_test[j]); }
	return 0;
}





/*Dependante to the chip low level functions*/

static int tc_get_fps(struct sensor_data *sensor)
{
	u32 frame_interval;
	int fps = 0;
	frame_interval = (tc358840_read_reg_val(sensor, FV_CNT_HI) & 0x3) << 8;
	frame_interval += tc358840_read_reg_val(sensor, FV_CNT_LO);
	if(frame_interval > 0){fps = DIV_ROUND_CLOSEST(10000, frame_interval);}
		//pr_debug("%s: frame_interval = %d*100us, fps = %d\n",__func__, frame_interval, fps);
	return fps;
}

static void tc358840_software_reset(struct sensor_data *sensor)
{
	int freq = sensor->mclk / 10000;
	//int freq = 40000;
	//tc358840_write_reg(sensor, 0x7080, 0, 2);
	tc358840_write_reg(sensor, 0x0002, 0x0f00, 2);
	msleep(100);
	tc358840_write_reg(sensor, 0x0002, 0x0000, 2);
	msleep(1000);
	tc358840_write_reg(sensor, 0x0004, 0x0004, 2);	/* autoinc */
	pr_debug("%s:freq=%d\n", __func__, freq);
	tc358840_write_reg(sensor, 0x8540, freq, 1);
	tc358840_write_reg(sensor, 0x8541, freq >> 8, 1);
}

static s32 power_control(struct tc_data *td, int on)
{
	struct sensor_data *sensor = &td->sensor;
	int i;
	int ret = 0;

	pr_debug("%s: %d\n", __func__, on);
	if (sensor->on == on) 
	{
		return ret;
	}
	if (on) 
	{
		for (i = 0; i < REGULATOR_CNT; i++) 
		{
			if (td->regulator[i]) 
			{
				ret = regulator_enable(td->regulator[i]);
				if (ret) {on = 0;break;}/* power all off */
						//pr_err("%s:regulator_enable failed(%d)\n",__func__, ret);
			}
		}
	}
	tc_standby(td, on ? 0 : 1);
	sensor->on = on;
	if (!on) 
	{
		for (i = REGULATOR_CNT - 1; i >= 0; i--) 
		{
			if(td->regulator[i]){regulator_disable(td->regulator[i]);}
		}
	}
	return ret;
}
static int tc358840_toggle_hpd(struct sensor_data *sensor, int active)
{
	int ret = 0;
	ret += tc358840_write_reg(sensor, 0x8544, (active) ? 0x00:0x10, 1);
	mdelay(500);
	ret += tc358840_write_reg(sensor, 0x8544, (active) ? 0x10:0x00, 1);
	return ret;
}

static int get_format_index(enum tc358840_frame_rate frame_rate, enum tc358840_mode mode)
{
	int ifmt;
	u32 flags = tc358840_mode_info_data[mode].flags;

	for (ifmt = 0; ifmt < ARRAY_SIZE(tc358840_formats); ifmt++) 
	{
		if(flags == tc358840_formats[ifmt].flags){return ifmt;}
	}
	return -1;
}

static int get_pixelformat(enum tc358840_frame_rate frame_rate, enum tc358840_mode mode)
{
	int ifmt = get_format_index(frame_rate, mode);

	if(ifmt < 0) {return 0;}
		//pr_debug("%s: unsupported format, %d, %d\n", __func__, frame_rate, mode);
	return tc358840_formats[ifmt].pixelformat;
}
static int tc_fps_to_index(int fps)
{
	int ret;
	switch (fps) 
	{
	case 49 ... 51:
		return tc358840_50_fps;
	case 59 ... 61:
		return  tc358840_60_fps;
	default:
		return -1;
	}
}
static void tc358840_enable_edid(struct sensor_data *sensor)
{
	pr_debug("Activate EDID\n");// EDID
	tc358840_write_reg(sensor, 0x85c7, 0x01, 1);// EDID MODE REGISTER: nternal EDID-RAM & DDC2B mode
	tc358840_write_reg(sensor, 0x85ca, 0x00, 1);
	tc358840_write_reg(sensor, 0x85cb, 0x01, 1);// 0x85cb:0x85ca - EDID Length = 0x01:00 (Size = 0x100 = 256)
	tc358840_write_reg(sensor, 0x8543, 0x36, 1);// DDC CONTROL: DDC_ACK output terminal H active, DDC5V_active detect delay 200ms
	tc358840_write_reg(sensor, 0x854a, 0x01, 1);// mark init done
}
static int tc358840_write_edid(struct sensor_data *sensor, const u8 *edid, int len)
{
	int i = 0, off = 0,size = 0,checksum = 0;
	u8 au8Buf[16+2] = {0};
	u16 reg= 0x8C00;
	size = ARRAY_SIZE(au8Buf) - 2;
	pr_debug("Write EDID: %d (%d)\n", len, size);
	while (len > 0) 
	{
		i = 0;
		au8Buf[i++] = (reg >> 8) & 0xff;
		au8Buf[i++] = reg & 0xff;
		if(size > len){size = len;}
		while (i < size + 2) 
		{
			u8 byte = edid[off++];
			if ((off & 0x7f) == 0) 
			{
				checksum &= 0xff;
				if (checksum != byte) 
				{
					//pr_info("%schecksum=%x, byte=%x\n", __func__, checksum, byte);
					byte = checksum;
					checksum = 0;
				}
			} 
			else  {checksum -= byte;}
			au8Buf[i++] = byte;
		}

		if(i2c_master_send(sensor->i2c_client, au8Buf, i) < 0) {return -1;}
			//pr_err("%s:write reg error:reg=%x,val=%x\n",__func__, reg, off);
		len -= (u8)size;
		reg += (u16)size;
	}
	tc358840_enable_edid(sensor);
	return 0;
}

/*Configuration function*/
int set_frame_rate_mode(struct tc_data *td,
		enum tc358840_frame_rate frame_rate, enum tc358840_mode mode)
{
	struct sensor_data *sensor = &td->sensor;
	const struct reg_value *pModeSetting = NULL;
	s32 i = 0,iModeSettingArySize = 0;
	register u32 RepeateLines = 0, Delay_ms = 0;
	register int RepeateTimes = 0;
	register u16 RegAddr = 0;
	register u32 Mask = 0, Val = 0;
	u8  Length;
	int retval = 0;

	pModeSetting =tc358840_mode_info_data[mode].init_data_ptr;
	iModeSettingArySize =tc358840_mode_info_data[mode].init_data_size;

	sensor->pix.pixelformat = get_pixelformat(frame_rate, mode);
	sensor->pix.width =tc358840_mode_info_data[mode].width;
	sensor->pix.height =tc358840_mode_info_data[mode].height;
	for (i = 0; i < iModeSettingArySize; ++i) 
	{
		pModeSetting = tc358840_mode_info_data[mode].init_data_ptr + i;
		Delay_ms = pModeSetting->u32Delay_ms & (0xffff);
		RegAddr = pModeSetting->u16RegAddr;
		Val = pModeSetting->u32Val;
		Mask = pModeSetting->u32Mask;
		Length = pModeSetting->u8Length;
		if (Mask) 
		{
			u32 RegVal = 0;

			retval = tc358840_read_reg(sensor, RegAddr, &RegVal);
			if(retval < 0) {return retval;}
				//pr_err("%s: read failed, reg=0x%x\n", __func__, RegAddr);
				
			RegVal &= ~(u8)Mask;
			Val =(Val & Mask)|RegVal;
		}

		retval = tc358840_write_reg(sensor, RegAddr, Val, Length);
		if(retval < 0) { return retval;}
			//pr_err("%s: write failed, reg=0x%x\n", __func__, RegAddr);
		if(Delay_ms) { msleep(Delay_ms) ;}
		if (0 != ((pModeSetting->u32Delay_ms>>16) & (0xff))) 
		{
			if (!RepeateTimes) 
			{
				RepeateTimes = (pModeSetting->u32Delay_ms>>16) & (0xff);
				RepeateLines = (pModeSetting->u32Delay_ms>>24) & (0xff);
			}
			if(--RepeateTimes > 0) {i -= RepeateLines;}
		}
	}
	/*tc358840_enable_edid(sensor);
	if (!td->edid_initialized) 
	{
		retval = tc358840_write_edid(sensor, cHDMIEDID, ARRAY_SIZE(cHDMIEDID));
		(retval) ? pr_err("%s: Fail to write EDID(%d) to tc358840!\n", __func__, retval): td->edid_initialized = 1;
	}*/
	td->edid_initialized = 1;
	return retval;
}



static int tc358840_init_mode(struct tc_data *td,
		enum tc358840_frame_rate frame_rate,
		enum tc358840_mode mode)
{
	struct sensor_data *sensor = &td->sensor;
	int retval = 0;
	void *mipi_csi2_info;
	//pr_debug("%s rate: %d mode: %d\n", __func__, frame_rate, mode);
	if(mode > tc358840_mode_1080p || mode < 0 ) {mode = tc358840_mode_unknown;}
		//pr_debug("%s Wrong tc358840 mode detected! %d. Set mode 0\n", __func__, mode);
	/* initial mipi dphy */
	tc358840_toggle_hpd(sensor, 0);
	tc358840_software_reset(sensor);
	mipi_csi2_info = mipi_csi2_get_info();
		//pr_debug("%s rate: %d mode: %d, info %p\n", __func__, frame_rate, mode, mipi_csi2_info);
	if(!mipi_csi2_info) {return -1;}
		//pr_err("Fail to get mipi_csi2_info!\n");
	retval = mipi_reset(mipi_csi2_info, frame_rate, tc358840_mode_unknown); //Here unexpected thing
	if(retval) { return retval; }
	retval = set_frame_rate_mode(td, frame_rate, tc358840_mode_unknown);
	if (retval) { return retval; }
	retval = mipi_wait(mipi_csi2_info);
	if (mode != tc358840_mode_unknown) 
	{
		tc358840_software_reset(sensor);
		retval = mipi_reset(mipi_csi2_info, frame_rate, mode);
		if(retval){return retval;}
		retval = set_frame_rate_mode(td, frame_rate, mode);
		if(retval){return retval;}
		retval = mipi_wait(mipi_csi2_info);
	}
	if(td->hpd_active){tc358840_toggle_hpd(sensor, td->hpd_active);}
	return retval;
}

static int tc358840_init_mode_new(struct tc_data *td,
		enum tc358840_frame_rate frame_rate,
		enum tc358840_mode mode)
{
	struct sensor_data *sensor = &td->sensor;
	int retval = 0;
	void *mipi_csi2_info;
	if(mode > tc358840_mode_1080p || mode < 0 ) {mode = tc358840_mode_unknown;}
	/* initial mipi dphy */
	tc358840_toggle_hpd(sensor, 0);
	mipi_csi2_info = mipi_csi2_get_info();
	if(!mipi_csi2_info) {return -1;}
	tc358840_software_reset(sensor);
	retval = mipi_reset(mipi_csi2_info, frame_rate, mode);
	if(retval){return retval;}
	retval = set_frame_rate_mode(td, frame_rate, mode);
	if(retval){return retval;}
	retval = mipi_wait(mipi_csi2_info);
	if(td->hpd_active){tc358840_toggle_hpd(sensor, td->hpd_active);}
	return retval;
}

static int tc358840_minit(struct tc_data *td)
{
	struct sensor_data *sensor = &td->sensor;
	int ret;
	enum tc358840_frame_rate frame_rate = tc358840_60_fps;
	u32 tgt_fps = sensor->streamcap.timeperframe.denominator / sensor->streamcap.timeperframe.numerator;
	frame_rate = tc_fps_to_index(tgt_fps);
	if(frame_rate < 0) {return -1;}
		//pr_err("%s: unsupported fps: %d\n", __func__, tgt_fps);
		//pr_debug("%s: capture mode: %d fps: %d\n", __func__,sensor->streamcap.capturemode, tgt_fps);
	ret = tc358840_init_mode(td, frame_rate, sensor->streamcap.capturemode);
	if(ret){pr_err("%s: Fail to init tc358840!\n", __func__);}
	return ret;
}

static int tc358840_reset(struct tc_data *td)
{
	int loop = 0;
	int ret;
	det_work_enable(td, 0);
	for (;;) 
	{
		//pr_debug("%s: RESET\n", __func__);
		power_control(td, 0);
		mdelay(100);
		power_control(td, 1);
		mdelay(1000);
		ret = tc358840_minit(td);
		if(!ret){break;}
		if(loop++ >= 3) {break;}
			//pr_err("%s:failed(%d)\n", __func__, ret);
	}
	det_work_enable(td, 1);
	return ret;
}

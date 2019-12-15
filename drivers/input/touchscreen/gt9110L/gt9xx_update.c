/*
 * Goodix GT9xx touchscreen driver
 *
 * Copyright  (C)  2016 - 2017 Goodix. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the GOODiX's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Version: 2.8.0.1
 * Release Date: 2017/11/24
 */

#include <linux/kthread.h>
#include "gt9xx.h"

#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/firmware.h>
#include <linux/ctype.h>

#define GUP_REG_HW_INFO		0x4220
#define GUP_REG_PID_VID		0x8140

#define FIRMWARE_NAME_LEN_MAX		256
#define GOODIX_FIRMWARE_FILE_NAME	"goodix_firmware.bin"
#define GOODIX_CONFIG_FILE_NAME		"goodix_config.cfg"

/* Update Config */
#ifdef GTP_CHECK_LCD_ID
/*sensor id = 0*/
#define GOODIX_CFG_TG_KDX_INX "goodix_cfg_TG_KDX_INX.cfg"
#define GOODIX_CFG_TG_KDX_BOE "goodix_cfg_TG_KDX_BOE.cfg"
#define GOODIX_CFG_TG_KDX_KD "goodix_cfg_TG_KDX_KD.cfg"

/*sensor id = 1*/
#define GOODIX_CFG_TG_RIJIU_INX "goodix_cfg_TG_RIJIU_INX.cfg"
#define GOODIX_CFG_TG_RIJIU_BOE "goodix_cfg_TG_RIJIU_BOE.cfg"
#define GOODIX_CFG_TG_RIJIU_KD "goodix_cfg_TG_RIJIU_KD.cfg"

/*sensor id = 2*/
#define GOODIX_CFG_LENS_RIJIU_INX "goodix_cfg_LENS_RIJIU_INX.cfg"
#define GOODIX_CFG_LENS_RIJIU_BOE "goodix_cfg_LENS_RIJIU_BOE.cfg"
#define GOODIX_CFG_LENS_RIJIU_KD "goodix_cfg_LENS_RIJIU_KD.cfg"

/*sensor id = 3*/
#define GOODIX_CFG_LENS_KDX_INX "goodix_cfg_LENS_KDX_INX.cfg"
#define GOODIX_CFG_LENS_KDX_BOE "goodix_cfg_LENS_KDX_BOE.cfg"
#define GOODIX_CFG_LENS_KDX_KD "goodix_cfg_LENS_KDX_KD.cfg"
#else
/*sensor id = 0*/
#define GOODIX_CFG_TG_KDX "goodix_cfg_TG_KDX.cfg"
/*sensor id = 1*/
#define GOODIX_CFG_TG_RIJIU "goodix_cfg_TG_RIJIU.cfg"
/*sensor id = 2*/
#define GOODIX_CFG_LENS_RIJIU "goodix_cfg_LENS_RIJIU.cfg"
/*sensor id = 3*/
#define GOODIX_CFG_LENS_KDX "goodix_cfg_LENS_KDX.cfg"
/*sensor id = 4*/
#define GOODIX_CFG_LENS_RIJIU_125um "goodix_cfg_LENS_RIJIU_125um.cfg"
/*sensor id = 5*/
#define GOODIX_CFG_LENS_RIJIU_150um "goodix_cfg_LENS_RIJIU_150um.cfg"
#endif /*GTP_CHECK_LCD_ID*/




#define FW_HEAD_LENGTH			14
#define FW_SECTION_LENGTH		0x2000	/*  8K */
#define FW_DSP_ISP_LENGTH		0x1000	/*  4K */
#define FW_DSP_LENGTH			0x1000	/*  4K */
#define FW_BOOT_LENGTH			0x800	/*  2K */
#define FW_SS51_LENGTH		(4 * FW_SECTION_LENGTH)	/*  32K */
#define FW_BOOT_ISP_LENGTH		0x800	/*  2k */
#define FW_GLINK_LENGTH			0x3000	/*  12k */
#define FW_GWAKE_LENGTH		(4 * FW_SECTION_LENGTH) /*  32k */

#define PACK_SIZE			256
#define MAX_FRAME_CHECK_TIME		5


#define _bRW_MISCTL__SRAM_BANK		0x4048
#define _bRW_MISCTL__MEM_CD_EN		0x4049
#define _bRW_MISCTL__CACHE_EN		0x404B
#define _bRW_MISCTL__TMR0_EN		0x40B0
#define _rRW_MISCTL__SWRST_B0_		0x4180
#define _bWO_MISCTL__CPU_SWRST_PULSE	0x4184
#define _rRW_MISCTL__BOOTCTL_B0_	0x4190
#define _rRW_MISCTL__BOOT_OPT_B0_	0x4218
#define _rRW_MISCTL__BOOT_CTL_		0x5094

#pragma pack(1)
struct st_fw_head {
	u8	hw_info[4];			 /* hardware info */
	u8	pid[8];				 /* product id */
	u16 vid;				 /* version id */
};
#pragma pack()

struct st_update_msg {
	u8 fw_damaged;
	u8 fw_flag;
	const u8 *fw_data;
	struct file *cfg_file;
	struct st_fw_head ic_fw_msg;
	u32 fw_total_len;
	u32 fw_burned_len;
	const struct firmware *fw;
} update_msg;

struct st_update_msg update_msg;

u16 show_len;
u16 total_len;

static u8 gup_burn_fw_gwake_section(struct i2c_client *client,
				    u8 *fw_section, u16 start_addr,
				    u32 len, u8 bank_cmd);
static int gup_check_config_source(struct i2c_client *client,
	u8 **file_name);

static s32 gup_init_panel(struct goodix_ts_data *ts)
{
	s32 ret = 0;
	u8 opr_buf[16];
	u8 sensor_id = 0;
	u8 drv_cfg_version;
	u8 flash_cfg_version;
	struct goodix_config_data *cfg = &ts->pdata->config;

	if (cfg->length < GTP_CONFIG_MIN_LENGTH) {
		TP_LOGE("No valid config with sensor_ID(%d)\n", sensor_id);

		return -EPERM;
	}

	ret = gtp_i2c_read_dbl_check(ts->client, GTP_REG_CONFIG_DATA,
				     &opr_buf[0], 1);
	if (ret == SUCCESS) {
		TP_LOGD(
		"CFG_GROUP%d Config Version: %d, IC Config Version: %d\n",
		sensor_id, cfg->data[GTP_ADDR_LENGTH], opr_buf[0]);

		flash_cfg_version = opr_buf[0];
		drv_cfg_version = cfg->data[GTP_ADDR_LENGTH];

		if (flash_cfg_version < 90 &&
		    flash_cfg_version > drv_cfg_version)
			cfg->data[GTP_ADDR_LENGTH] = 0x00;
	} else {
		TP_LOGE("Failed to get ic config version!No config sent!\n");
		return -EPERM;
	}

	ret = gtp_send_cfg(ts->client);
	if (ret < 0)
		TP_LOGE("Send config error.\n");
	else
		usleep_range(10000, 11000);

	/* restore config vrsion */
	cfg->data[GTP_ADDR_LENGTH] = drv_cfg_version;

	return 0;
}


static u8 gup_get_ic_msg(struct i2c_client *client, u16 addr, u8 *msg, s32 len)
{
	s32 i = 0;

	msg[0] = (addr >> 8) & 0xff;
	msg[1] = addr & 0xff;

	for (i = 0; i < 5; i++) {
		if (gtp_i2c_read(client, msg, GTP_ADDR_LENGTH + len) > 0)
			break;
	}

	if (i >= 5) {
		TP_LOGE("Read data from 0x%02x%02x failed!\n", msg[0], msg[1]);
		return FAIL;
	}

	return SUCCESS;
}

static u8 gup_set_ic_msg(struct i2c_client *client, u16 addr, u8 val)
{
	s32 i = 0;
	u8 msg[3];

	msg[0] = (addr >> 8) & 0xff;
	msg[1] = addr & 0xff;
	msg[2] = val;

	for (i = 0; i < 5; i++) {
		if (gtp_i2c_write(client, msg, GTP_ADDR_LENGTH + 1) > 0)
			break;
	}

	if (i >= 5) {
		TP_LOGE("Set data to 0x%02x%02x failed!\n", msg[0], msg[1]);
		return FAIL;
	}

	return SUCCESS;
}

static u8 gup_get_ic_fw_msg(struct i2c_client *client)
{
	s32 ret = -1;
	u8	retry = 0;
	u8	buf[16];
	u8	i;

	/*  step1:get hardware info */
	ret = gtp_i2c_read_dbl_check(client, GUP_REG_HW_INFO,
				     &buf[GTP_ADDR_LENGTH], 4);
	if (ret == FAIL) {
		TP_LOGE("[get_ic_fw_msg]get hw_info failed,exit\n");
		return FAIL;
	}

	/*  buf[2~5]: 00 06 90 00
	 *  hw_info: 00 90 06 00
	 */
	for (i = 0; i < 4; i++)
		update_msg.ic_fw_msg.hw_info[i] = buf[GTP_ADDR_LENGTH + 3 - i];
		TP_LOGD("IC Hardware info:%02x%02x%02x%02x\n",
			update_msg.ic_fw_msg.hw_info[0],
			update_msg.ic_fw_msg.hw_info[1],
			update_msg.ic_fw_msg.hw_info[2],
			update_msg.ic_fw_msg.hw_info[3]);
	/*  step2:get firmware message */
	for (retry = 0; retry < 2; retry++) {
		ret = gup_get_ic_msg(client, GUP_REG_FW_MSG, buf, 1);
		if (ret == FAIL) {
			TP_LOGE("Read firmware message fail.\n");
			return ret;
		}

		update_msg.fw_damaged = buf[GTP_ADDR_LENGTH];
		if ((update_msg.fw_damaged != 0xBE) && (!retry)) {
			TP_LOGI("The check sum in ic is error.\n");
			TP_LOGI("The IC will be updated by force.\n");
			continue;
		}
		break;
	}
	TP_LOGD("IC force update flag:0x%x\n", update_msg.fw_damaged);

	/*  step3:get pid & vid */
	ret = gtp_i2c_read_dbl_check(client, GUP_REG_PID_VID,
				     &buf[GTP_ADDR_LENGTH], 6);
	if (ret == FAIL) {
		TP_LOGE("[get_ic_fw_msg]get pid & vid failed,exit\n");
		return FAIL;
	}

	memset(update_msg.ic_fw_msg.pid, 0, sizeof(update_msg.ic_fw_msg.pid));
	memcpy(update_msg.ic_fw_msg.pid, &buf[GTP_ADDR_LENGTH], 4);
	TP_LOGD("IC Product id:%s\n", update_msg.ic_fw_msg.pid);

	/* GT9XX PID MAPPING */
	/*|-----FLASH-----RAM-----|
	 *|------918------918-----|
	 *|------968------968-----|
	 *|------913------913-----|
	 *|------913P-----913P----|
	 *|------927------927-----|
	 *|------927P-----927P----|
	 *|------9110-----9110----|
	 *|------9110P----9111----|
	 */
	if (update_msg.ic_fw_msg.pid[0] != 0) {
		if (!memcmp(update_msg.ic_fw_msg.pid, "9111", 4)) {
			TP_LOGD("IC Mapping Product id:%s\n",
					update_msg.ic_fw_msg.pid);
			memcpy(update_msg.ic_fw_msg.pid, "9110P", 5);
		}
	}

	update_msg.ic_fw_msg.vid = buf[GTP_ADDR_LENGTH + 4] +
				   (buf[GTP_ADDR_LENGTH + 5] << 8);
	TP_LOGD("IC version id:%04x\n", update_msg.ic_fw_msg.vid);

	return SUCCESS;
}

s32 gup_enter_update_mode(struct i2c_client *client)
{
	s32 ret = -1;
	s32 retry = 0;
	u8 rd_buf[3];

	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	/* step1:RST output low last at least 2ms */
	if (!gpio_is_valid(ts->pdata->rst_gpio)) {
		TP_LOGE("update failed, no rst pin\n");
		return FAIL;
	}
	gpio_direction_output(ts->pdata->rst_gpio, 0);
	usleep_range(2000, 3000);

	/* step2:select I2C slave addr,INT:0--0xBA;1--0x28. */
	gtp_int_output(ts, client->addr == 0x14);
	usleep_range(2000, 3000);

	/* step3:RST output high reset guitar */
	gpio_direction_output(ts->pdata->rst_gpio, 1);

	/* 20121211 modify start */
	usleep_range(5000, 6000);
	while (retry++ < 200) {
		/* step4:Hold ss51 & dsp */
		ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x0C);
		if (ret <= 0) {
			TP_LOGD("Hold ss51 & dsp I2C error,retry:%d\n", retry);
			continue;
		}

		/* step5:Confirm hold */
		ret = gup_get_ic_msg(client, _rRW_MISCTL__SWRST_B0_, rd_buf, 1);
		if (ret <= 0) {
			TP_LOGD("Hold ss51 & dsp I2C error,retry:%d\n", retry);
			continue;
		}
		if (rd_buf[GTP_ADDR_LENGTH] == 0x0C) {
			TP_LOGD("Hold ss51 & dsp confirm SUCCESS\n");
			break;
		}
		TP_LOGD("Hold ss51 & dsp confirm 0x4180 failed,value:%d\n",
			rd_buf[GTP_ADDR_LENGTH]);
	}
	if (retry >= 200) {
		TP_LOGE("Enter update Hold ss51 failed.\n");
		return FAIL;
	}

	/* step6:DSP_CK and DSP_ALU_CK PowerOn */
	ret = gup_set_ic_msg(client, 0x4010, 0x00);

	/* 20121211 modify end */
	return ret;
}

void gup_leave_update_mode(struct i2c_client *client)
{
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->pdata->int_sync && gpio_is_valid(ts->pdata->irq_gpio))
		gpio_direction_input(ts->pdata->irq_gpio);

	TP_LOGD("[leave_update_mode]reset chip.\n");
	gtp_reset_guitar(i2c_connect_client, 20);
}

static u8 gup_enter_update_judge(struct i2c_client *client,
				 struct st_fw_head *fw_head)
{
	u16 u16_tmp;
	s32 i = 0;
	u32 fw_len = 0;
	s32 pid_cmp_len = 0;

	u16_tmp = fw_head->vid;
	fw_head->vid = (u16)(u16_tmp>>8) + (u16)(u16_tmp<<8);

	TP_LOGI("FILE HARDWARE INFO:%*ph\n", 4,
		 &fw_head->hw_info[0]);
	TP_LOGI("FILE PID:%s\n", fw_head->pid);
	TP_LOGI("FILE VID:%04x\n", fw_head->vid);

	TP_LOGI("IC HARDWARE INFO:%*ph\n", 4,
		 &update_msg.ic_fw_msg.hw_info[0]);
	TP_LOGI("IC PID:%s\n", update_msg.ic_fw_msg.pid);
	TP_LOGI("IC VID:%04x\n", update_msg.ic_fw_msg.vid);

	if (!memcmp(fw_head->pid, "9158", 4) &&
	    !memcmp(update_msg.ic_fw_msg.pid, "915S", 4)) {
		TP_LOGI("Update GT915S to GT9158 directly!\n");
		return SUCCESS;
	}
	/* First two conditions */
	if (!memcmp(fw_head->hw_info, update_msg.ic_fw_msg.hw_info,
	sizeof(update_msg.ic_fw_msg.hw_info))) {
		fw_len = 42 * 1024;
	} else {
		fw_len = fw_head->hw_info[3];
		fw_len += (((u32)fw_head->hw_info[2]) << 8);
		fw_len += (((u32)fw_head->hw_info[1]) << 16);
		fw_len += (((u32)fw_head->hw_info[0]) << 24);
	}
	if (update_msg.fw_total_len != fw_len) {
		TP_LOGE("Inconsistent firmware size, Update aborted!\n");
		TP_LOGE("Default size: %d(%dK), actual size: %d(%dK)\n",
			fw_len, fw_len/1024, update_msg.fw_total_len,
			update_msg.fw_total_len/1024);
		return FAIL;
	}
	TP_LOGI("Firmware length:%d(%dK)\n",
		 update_msg.fw_total_len,
		 update_msg.fw_total_len/1024);

	if (update_msg.fw_damaged != 0xBE) {
		TP_LOGI("FW chksum error,need enter update.\n");
		return SUCCESS;
	}

	/*  20130523 start */
	if (strlen(update_msg.ic_fw_msg.pid) < 3) {
		TP_LOGI("Illegal IC pid, need enter update\n");
		return SUCCESS;
	}

	/* check pid legality */
	for (i = 0; i < 3; i++) {
		if (!isdigit(update_msg.ic_fw_msg.pid[i])) {
			TP_LOGI("Illegal IC pid, need enter update\n");
			return SUCCESS;
		}
	}
	/*  20130523 end */

	pid_cmp_len = strlen(fw_head->pid);
	if (pid_cmp_len < strlen(update_msg.ic_fw_msg.pid))
		pid_cmp_len = strlen(update_msg.ic_fw_msg.pid);

	if ((!memcmp(fw_head->pid, update_msg.ic_fw_msg.pid, pid_cmp_len)) ||
			(!memcmp(update_msg.ic_fw_msg.pid, "91XX", 4)) ||
			(!memcmp(fw_head->pid, "91XX", 4))) {
		if (!memcmp(fw_head->pid, "91XX", 4))
			TP_LOGD("Force none same pid update mode.\n");
		else
			TP_LOGD("Get the same pid.\n");

		/* The third condition */
		/* upgrade by bigger FW */
		if (fw_head->vid > update_msg.ic_fw_msg.vid) {
			TP_LOGI("FILE VID > IC VID, Need enter update.\n");
			return SUCCESS;
		}
		TP_LOGI("File VID == Ic VID, update aborted!\n");
	} else {
		TP_LOGE("File PID != Ic PID, update aborted!\n");
	}

	return FAIL;
}

int gup_update_config(struct i2c_client *client)
{
	s32 ret = 0;
	s32 i = 0;
	s32 file_cfg_len = 0;
	u8 *file_config;
	u8 *config_file_name;
	const struct firmware *fw_cfg;

	/*struct goodix_ts_data *ts = i2c_get_clientdata(client);*/
	ret = gup_check_config_source(client, &config_file_name);
	if (ret < 0) {
		TP_LOGE("Check source failed\n");
		return -EFAULT;
	}
	TP_LOGI("Config file name %s\n", config_file_name);

	ret = request_firmware(&fw_cfg, config_file_name,
			       &client->dev);
	if (ret < 0) {
		TP_LOGE("Cannot get config file - %s (%d)\n",
			GOODIX_CONFIG_FILE_NAME, ret);
		return -EFAULT;
	}
	if (!fw_cfg || !fw_cfg->data || fw_cfg->size > PAGE_SIZE) {
		TP_LOGE("config file illegal\n");
		ret = -EFAULT;
		goto cfg_fw_err;
	}

	TP_LOGD("config firmware file len:%zu\n", fw_cfg->size);

	file_config = kzalloc(GTP_CONFIG_MAX_LENGTH + GTP_ADDR_LENGTH,
			      GFP_KERNEL);
	if (!file_config) {
		ret = -ENOMEM;
		goto cfg_fw_err;
	}
	file_config[0] = GTP_REG_CONFIG_DATA >> 8;
	file_config[1] = GTP_REG_CONFIG_DATA & 0xff;
	file_cfg_len = gtp_ascii_to_array(fw_cfg->data, fw_cfg->size,
					  &file_config[GTP_ADDR_LENGTH]);
	if (file_cfg_len < 0) {
		TP_LOGE("failed covert ascii to hex\n");
		ret = -EFAULT;
		goto update_cfg_file_failed;
	}

	GTP_DEBUG_ARRAY(file_config + GTP_ADDR_LENGTH, file_cfg_len);

	i = 0;
	while (i++ < 5) {
		ret = gtp_i2c_write(client, file_config, file_cfg_len + 2);
		if (ret > 0) {
			TP_LOGI("Send config SUCCESS.\n");
			msleep(500);
			break;
		}
		TP_LOGE("Send config i2c error.\n");
	}

update_cfg_file_failed:
	kfree(file_config);
cfg_fw_err:
	release_firmware(fw_cfg);
	return ret;
}

static u8 gup_check_firmware_name(struct i2c_client *client,
		u8 **path_p)
{
	u8 len;
	u8 *fname;

	struct goodix_ts_data *ts = i2c_get_clientdata(client);
	struct goodix_fw_info *fw_info = &ts->fw_info;

	TP_LOGI("Sensor Id = %d\n", fw_info->sensor_id);
#ifdef GTP_CHECK_LCD_ID
	TP_LOGI("lcd_id = %d\n", ts->lcd_id);
#endif

	if (!(*path_p)) {
		/* FW file may be same with all TP source */
		*path_p = GOODIX_FIRMWARE_FILE_NAME;
	}

	len = strnlen(*path_p, FIRMWARE_NAME_LEN_MAX);
	if (len >= FIRMWARE_NAME_LEN_MAX) {
		TP_LOGE("firmware name too long!\n");
		return -EINVAL;
	}

	fname = strrchr(*path_p, '/');
	if (fname) {
		fname = fname + 1;
		*path_p = fname;
	}

	return 0;
}

static int gup_check_config_source(struct i2c_client *client,
	u8 **file_name)
{
	u8 len;
	u8 *fname;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);
	struct goodix_fw_info *fw_info = &ts->fw_info;

	TP_LOGI("Sensor Id = %d\n", fw_info->sensor_id);
#ifdef GTP_CHECK_LCD_ID
	TP_LOGI("lcd_id = %d\n", ts->lcd_id);
#endif

#ifdef GTP_CHECK_LCD_ID
	if (ts->lcd_id == GTP_LCD_INX &&
		fw_info->sensor_id == GTP_TP_TG_KDX) {
		TP_LOGI("source is TG_KDX+INX\n");
		*file_name = GOODIX_CFG_TG_KDX_INX;
	} else if (ts->lcd_id == GTP_LCD_INX &&
		fw_info->sensor_id == GTP_TP_TG_RIJIU) {
		TP_LOGI("source is TG_RIJIU+INX\n");
		*file_name = GOODIX_CFG_TG_RIJIU_INX;
	} else if (ts->lcd_id == GTP_LCD_BOE &&
		fw_info->sensor_id == GTP_TP_TG_KDX) {
		TP_LOGI("source is TG_KDX+BOE\n");
		*file_name = GOODIX_CFG_TG_KDX_BOE;
	} else if (ts->lcd_id == GTP_LCD_BOE &&
		fw_info->sensor_id == GTP_TP_TG_RIJIU) {
		TP_LOGI("source is TG_RIJIU+BOE\n");
		*file_name = GOODIX_CFG_TG_RIJIU_BOE;
	} else if (ts->lcd_id == GTP_LCD_KD &&
		fw_info->sensor_id == GTP_TP_TG_KDX) {
		TP_LOGI("source is TG_KDX+KD\n");
		*file_name = GOODIX_CFG_TG_KDX_KD;
	} else if (ts->lcd_id == GTP_LCD_KD &&
		fw_info->sensor_id == GTP_TP_TG_RIJIU) {
		TP_LOGI("source is TG_RIJIU+KD\n");
		*file_name = GOODIX_CFG_TG_RIJIU_KD;
	} else if (ts->lcd_id == GTP_LCD_INX &&
		fw_info->sensor_id == GTP_TP_LENS_KDX) {
		TP_LOGI("source is LENS_KDX+INX\n");
		*file_name = GOODIX_CFG_LENS_KDX_INX;
	} else if (ts->lcd_id == GTP_LCD_INX &&
		fw_info->sensor_id == GTP_TP_LENS_RIJIU) {
		TP_LOGI("source is LENS_RIJIU+INX\n");
		*file_name = GOODIX_CFG_LENS_RIJIU_INX;
	} else if (ts->lcd_id == GTP_LCD_BOE &&
		fw_info->sensor_id == GTP_TP_LENS_KDX) {
		TP_LOGI("source is LENS_KDX+BOE\n");
		*file_name = GOODIX_CFG_LENS_KDX_BOE;
	} else if (ts->lcd_id == GTP_LCD_BOE &&
		fw_info->sensor_id == GTP_TP_LENS_RIJIU) {
		TP_LOGI("source is LENS_RIJIU+BOE\n");
		*file_name = GOODIX_CFG_LENS_RIJIU_BOE;
	} else if (ts->lcd_id == GTP_LCD_KD &&
		fw_info->sensor_id == GTP_TP_LENS_RIJIU) {
		TP_LOGI("source is LENS_RIJIU+KD\n");
		*file_name = GOODIX_CFG_LENS_RIJIU_KD;
	} else if (ts->lcd_id == GTP_LCD_KD &&
		fw_info->sensor_id == GTP_TP_LENS_KDX) {
		TP_LOGI("source is LENS_KDX+KD\n");
		*file_name = GOODIX_CFG_LENS_KDX_KD;
	} else {
		/* Invalid sensor id*/
		TP_LOGE("Invalid sensor, don't set config source\n");
		return -EINVAL;
	}
#else
	if (fw_info->sensor_id == GTP_TP_TG_KDX) {
		TP_LOGI("source is TG_KDX\n");
		*file_name = GOODIX_CFG_TG_KDX;
	} else if (fw_info->sensor_id == GTP_TP_TG_RIJIU) {
		TP_LOGI("source is TG_RIJIU\n");
		*file_name = GOODIX_CFG_TG_RIJIU;
	} else if (fw_info->sensor_id == GTP_TP_LENS_RIJIU) {
		TP_LOGI("source is LENS_RIJIU\n");
		*file_name = GOODIX_CFG_LENS_RIJIU;
	} else if (fw_info->sensor_id == GTP_TP_LENS_KDX) {
		TP_LOGI("source is LENS_KDX\n");
		*file_name = GOODIX_CFG_LENS_KDX;
	} else if (fw_info->sensor_id == GTP_TP_LENS_RIJIU_125um) {
		TP_LOGI("source is LENS_RIJIU_125um\n");
		*file_name = GOODIX_CFG_LENS_RIJIU_125um;
	} else if (fw_info->sensor_id == GTP_TP_LENS_RIJIU_150um) {
		TP_LOGI("source is LENS_RIJIU_150um\n");
		*file_name = GOODIX_CFG_LENS_RIJIU_150um;
	} else {
		/* Invalid sensor id*/
		TP_LOGE("Invalid sensor, don't set config source\n");
		return -EINVAL;
	}
#endif /*GTP_CHECK_LCD_ID*/

	len = strnlen(*file_name, FIRMWARE_NAME_LEN_MAX);
	if (len >= FIRMWARE_NAME_LEN_MAX) {
		TP_LOGE("firmware name too long!\n");
		return -EINVAL;
	}

	fname = strrchr(*file_name, '/');
	if (fname) {
		fname = fname + 1;
		*file_name = fname;
	}

	return 0;
}


static u8 gup_get_update_file(struct i2c_client *client,
				struct st_fw_head *fw_head, u8 *path)
{
	s32 ret = 0;
	s32 i = 0;
	s32 fw_checksum = 0;
	struct goodix_ts_data *ts = i2c_get_clientdata(client);

	if (ts->pdata->auto_update_cfg) {
		ret = gup_update_config(client);
		if (ret <= 0)
			TP_LOGE("Update config failed.\n");
	}

	ret = gup_check_firmware_name(client, &path);
	if (ret < 0)
		return FAIL;

	ret = request_firmware(&update_msg.fw, path, &client->dev);
	if (ret < 0) {
		TP_LOGE("Failed get firmware:%d\n", ret);
		return FAIL;
	}

	TP_LOGI("FW File: %s size=%zu\n", path, update_msg.fw->size);
	update_msg.fw_data = update_msg.fw->data;
	update_msg.fw_total_len = update_msg.fw->size;

	if (update_msg.fw_total_len <
	    FW_HEAD_LENGTH + FW_SECTION_LENGTH * 4 + FW_DSP_ISP_LENGTH +
	    FW_DSP_LENGTH + FW_BOOT_LENGTH) {
		TP_LOGE("INVALID bin file(size: %d), update aborted.\n",
			update_msg.fw_total_len);
		goto invalied_fw;
	}

	update_msg.fw_total_len -= FW_HEAD_LENGTH;

	TP_LOGD("Bin firmware actual size: %d(%dK)\n",
		update_msg.fw_total_len, update_msg.fw_total_len/1024);

	memcpy(fw_head, update_msg.fw_data, FW_HEAD_LENGTH);

	/* check firmware legality */
	fw_checksum = 0;
	for (i = 0; i < update_msg.fw_total_len; i += 2) {
		u16 temp;

		temp = (update_msg.fw_data[FW_HEAD_LENGTH + i] << 8) +
			update_msg.fw_data[FW_HEAD_LENGTH + i + 1];
		fw_checksum += temp;
	}

	TP_LOGD("firmware checksum:%x\n", fw_checksum&0xFFFF);
	if (fw_checksum & 0xFFFF) {
		TP_LOGE("Illegal firmware file.\n");
		goto invalied_fw;
	}

	return SUCCESS;

invalied_fw:
	update_msg.fw_data = NULL;
	update_msg.fw_total_len = 0;
	release_firmware(update_msg.fw);
	return FAIL;
}

static u8 gup_burn_proc(struct i2c_client *client, u8 *burn_buf,
		u16 start_addr, u16 total_length)
{
	s32 ret = 0;
	u16 burn_addr = start_addr;
	u16 frame_length = 0;
	u16 burn_length = 0;
	u8 wr_buf[PACK_SIZE + GTP_ADDR_LENGTH];
	u8 rd_buf[PACK_SIZE + GTP_ADDR_LENGTH];
	u8 retry = 0;

	TP_LOGD("Begin burn %dk data to addr 0x%x\n",
		total_length / 1024, start_addr);
	while (burn_length < total_length) {
		TP_LOGD("B/T:%04d/%04d\n", burn_length, total_length);
		frame_length = ((total_length - burn_length)
		> PACK_SIZE) ? PACK_SIZE : (total_length - burn_length);
		wr_buf[0] = (u8)(burn_addr>>8);
		rd_buf[0] = wr_buf[0];
		wr_buf[1] = (u8)burn_addr;
		rd_buf[1] = wr_buf[1];
		memcpy(&wr_buf[GTP_ADDR_LENGTH],
		&burn_buf[burn_length], frame_length);

		for (retry = 0; retry < MAX_FRAME_CHECK_TIME; retry++) {
			ret = gtp_i2c_write(client,
			wr_buf, GTP_ADDR_LENGTH + frame_length);
			if (ret <= 0) {
				TP_LOGE("Write frame data i2c error.\n");
				continue;
			}
			ret = gtp_i2c_read(client, rd_buf,
					   GTP_ADDR_LENGTH + frame_length);
			if (ret <= 0) {
				TP_LOGE("Read back frame data i2c error.\n");
				continue;
			}
			if (memcmp(&wr_buf[GTP_ADDR_LENGTH],
				&rd_buf[GTP_ADDR_LENGTH], frame_length)) {
				TP_LOGE("Check frame data fail,not equal.\n");
				TP_LOGD("write array:\n");
				GTP_DEBUG_ARRAY(&wr_buf[GTP_ADDR_LENGTH],
				frame_length);
				TP_LOGD("read array:\n");
				GTP_DEBUG_ARRAY(&rd_buf[GTP_ADDR_LENGTH],
				frame_length);
				continue;
			} else {
				/* TP_LOGD(
				 * "Check frame data success.\n");
				 */
				break;
			}
		}
		if (retry >= MAX_FRAME_CHECK_TIME) {
			TP_LOGE("Burn frame data time out,exit.\n");
			return FAIL;
		}
		burn_length += frame_length;
		burn_addr += frame_length;
	}

	return SUCCESS;
}

static u8 gup_load_section_file(u8 *buf, u32 offset, u16 length, u8 set_or_end)
{
	if (!update_msg.fw_data ||
	    update_msg.fw_total_len < offset + length) {
		TP_LOGE("cannot load section data. fw_len=%d read end=%d\n",
			update_msg.fw_total_len,
			FW_HEAD_LENGTH + offset + length);
		return FAIL;
	}

	if (set_or_end == SEEK_SET) {
		memcpy(buf, &update_msg.fw_data[FW_HEAD_LENGTH + offset],
		       length);
	} else {
		/* seek end */
		memcpy(buf, &update_msg.fw_data[update_msg.fw_total_len +
		       FW_HEAD_LENGTH - offset], length);
	}

	return SUCCESS;
}

static u8 gup_recall_check(struct i2c_client *client, u8 *chk_src,
			   u16 start_rd_addr, u16 chk_length)
{
	u8 rd_buf[PACK_SIZE + GTP_ADDR_LENGTH];
	s32 ret = 0;
	u16 recall_addr = start_rd_addr;
	u16 recall_length = 0;
	u16 frame_length = 0;

	while (recall_length < chk_length) {
		frame_length = ((chk_length - recall_length)
				> PACK_SIZE) ? PACK_SIZE :
				(chk_length - recall_length);
		ret = gup_get_ic_msg(client, recall_addr, rd_buf, frame_length);
		if (ret <= 0) {
			TP_LOGE("recall i2c error,exit\n");
			return FAIL;
		}

		if (memcmp(&rd_buf[GTP_ADDR_LENGTH],
		&chk_src[recall_length], frame_length)) {
			TP_LOGE("Recall frame data fail,not equal.\n");
			TP_LOGD("chk_src array:\n");
			GTP_DEBUG_ARRAY(&chk_src[recall_length], frame_length);
			TP_LOGD("recall array:\n");
			GTP_DEBUG_ARRAY(&rd_buf[GTP_ADDR_LENGTH], frame_length);
			return FAIL;
		}

		recall_length += frame_length;
		recall_addr += frame_length;
	}
	TP_LOGD("Recall check %dk firmware success.\n", (chk_length/1024));

	return SUCCESS;
}

static u8 gup_burn_fw_section(struct i2c_client *client, u8 *fw_section,
			      u16 start_addr, u8 bank_cmd)
{
	s32 ret = 0;
	u8	rd_buf[5];

	/* step1:hold ss51 & dsp */
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_section]hold ss51 & dsp fail.\n");
		return FAIL;
	}

	/* step2:set scramble */
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_section]set scramble fail.\n");
		return FAIL;
	}

	/* step3:select bank */
	ret = gup_set_ic_msg(client, _bRW_MISCTL__SRAM_BANK,
	(bank_cmd >> 4)&0x0F);
	if (ret <= 0) {
		TP_LOGE(
		"[burn_fw_section]select bank %d fail.\n",
		(bank_cmd >> 4)&0x0F);
		return FAIL;
	}

	/* step4:enable accessing code */
	ret = gup_set_ic_msg(client, _bRW_MISCTL__MEM_CD_EN, 0x01);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_section]enable accessing code fail.\n");
		return FAIL;
	}

	/* step5:burn 8k fw section */
	ret = gup_burn_proc(client, fw_section, start_addr, FW_SECTION_LENGTH);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_section]burn fw_section fail.\n");
		return FAIL;
	}

	/* step6:hold ss51 & release dsp */
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x04);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_section]hold ss51 & release dsp fail.\n");
		return FAIL;
	}
	/* must delay */
	usleep_range(1000, 2000);

	/* step7:send burn cmd to move data to flash from sram */
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, bank_cmd&0x0f);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_section]send burn cmd fail.\n");
		return FAIL;
	}
	TP_LOGD("[burn_fw_section]Wait for the burn is complete......\n");
	do {
		ret = gup_get_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, rd_buf, 1);
		if (ret <= 0) {
			TP_LOGE("[burn_fw_section]Get burn state fail\n");
			return FAIL;
		}
		usleep_range(10000, 11000);
	/* TP_LOGD("[burn_fw_section]Get burn state:%d.\n",
	 * rd_buf[GTP_ADDR_LENGTH]);
	 */
	} while (rd_buf[GTP_ADDR_LENGTH]);

	/* step8:select bank */
	ret = gup_set_ic_msg(client,
			     _bRW_MISCTL__SRAM_BANK, (bank_cmd >> 4) & 0x0F);
	if (ret <= 0) {
		TP_LOGE(
		"[burn_fw_section]select bank %d fail.\n",
		(bank_cmd >> 4)&0x0F);
		return FAIL;
	}

	/* step9:enable accessing code */
	ret = gup_set_ic_msg(client, _bRW_MISCTL__MEM_CD_EN, 0x01);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_section]enable accessing code fail.\n");
		return FAIL;
	}

	/* step10:recall 8k fw section */
	ret = gup_recall_check(client,
	fw_section, start_addr, FW_SECTION_LENGTH);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_section]recall check %dk firmware fail.\n",
			FW_SECTION_LENGTH / 1024);
		return FAIL;
	}

	/* step11:disable accessing code */
	ret = gup_set_ic_msg(client, _bRW_MISCTL__MEM_CD_EN, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_section]disable accessing code fail.\n");
		return FAIL;
	}

	return SUCCESS;
}

static u8 gup_burn_dsp_isp(struct i2c_client *client)
{
	s32 ret = 0;
	u8 *fw_dsp_isp = NULL;
	u8	retry = 0;

	TP_LOGI("[burn_dsp_isp]Begin burn dsp isp---->>\n");

	/* step1:alloc memory */
	TP_LOGD("[burn_dsp_isp]step1:alloc memory\n");
	while (retry++ < 5) {
		fw_dsp_isp = kzalloc(FW_DSP_ISP_LENGTH, GFP_KERNEL);
		if (fw_dsp_isp == NULL) {
			continue;
		} else {
			TP_LOGI(
			"[burn_dsp_isp]Alloc %dk byte memory success.\n",
			FW_DSP_ISP_LENGTH / 1024);
			break;
		}
	}
	if (retry > 5) {
		TP_LOGE("[burn_dsp_isp]Alloc memory fail,exit.\n");
		return FAIL;
	}

	/* step2:load dsp isp file data */
	TP_LOGD("[burn_dsp_isp]step2:load dsp isp file data\n");
	ret = gup_load_section_file(fw_dsp_isp, FW_DSP_ISP_LENGTH,
				    FW_DSP_ISP_LENGTH, SEEK_END);
	if (ret == FAIL) {
		TP_LOGE("[burn_dsp_isp]load firmware dsp_isp fail.\n");
		goto exit_burn_dsp_isp;
	}

	/* step3:disable wdt,clear cache enable */
	TP_LOGD("[burn_dsp_isp]step3:disable wdt,clear cache enable\n");
	ret = gup_set_ic_msg(client, _bRW_MISCTL__TMR0_EN, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_dsp_isp]disable wdt fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}
	ret = gup_set_ic_msg(client, _bRW_MISCTL__CACHE_EN, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_dsp_isp]clear cache enable fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step4:hold ss51 & dsp */
	TP_LOGD("[burn_dsp_isp]step4:hold ss51 & dsp\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TP_LOGE("[burn_dsp_isp]hold ss51 & dsp fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step5:set boot from sram */
	TP_LOGD("[burn_dsp_isp]step5:set boot from sram\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOTCTL_B0_, 0x02);
	if (ret <= 0) {
		TP_LOGE("[burn_dsp_isp]set boot from sram fail\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step6:software reboot */
	TP_LOGD("[burn_dsp_isp]step6:software reboot\n");
	ret = gup_set_ic_msg(client, _bWO_MISCTL__CPU_SWRST_PULSE, 0x01);
	if (ret <= 0) {
		TP_LOGE("[burn_dsp_isp]software reboot fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step7:select bank2 */
	TP_LOGD("[burn_dsp_isp]step7:select bank2\n");
	ret = gup_set_ic_msg(client, _bRW_MISCTL__SRAM_BANK, 0x02);
	if (ret <= 0) {
		TP_LOGE("[burn_dsp_isp]select bank2 fail\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step8:enable accessing code */
	TP_LOGD("[burn_dsp_isp]step8:enable accessing code\n");
	ret = gup_set_ic_msg(client, _bRW_MISCTL__MEM_CD_EN, 0x01);
	if (ret <= 0) {
		TP_LOGE("[burn_dsp_isp]enable accessing code fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}

	/* step9:burn 4k dsp_isp */
	TP_LOGD("[burn_dsp_isp]step9:burn 4k dsp_isp\n");
	ret = gup_burn_proc(client, fw_dsp_isp, 0xC000, FW_DSP_ISP_LENGTH);
	if (ret == FAIL) {
		TP_LOGE("[burn_dsp_isp]burn dsp_isp fail.\n");
		goto exit_burn_dsp_isp;
	}

	/* step10:set scramble */
	TP_LOGD("[burn_dsp_isp]step10:set scramble\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_dsp_isp]set scramble fail.\n");
		ret = FAIL;
		goto exit_burn_dsp_isp;
	}
	update_msg.fw_burned_len += FW_DSP_ISP_LENGTH;
	TP_LOGD("[burn_dsp_isp]Burned length:%d\n",
			update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_dsp_isp:
	kfree(fw_dsp_isp);

	return ret;
}

static u8 gup_burn_fw_ss51(struct i2c_client *client)
{
	u8 *fw_ss51 = NULL;
	u8	retry = 0;
	s32 ret = 0;

	TP_LOGI("[burn_fw_ss51]Begin burn ss51 firmware---->>\n");

	/* step1:alloc memory */
	TP_LOGD("[burn_fw_ss51]step1:alloc memory\n");
	while (retry++ < 5) {
		fw_ss51 = kzalloc(FW_SECTION_LENGTH, GFP_KERNEL);
		if (fw_ss51 == NULL) {
			continue;
		} else {
			TP_LOGD(
			"[burn_fw_ss51]Alloc %dk byte memory success.\n",
			(FW_SECTION_LENGTH / 1024));
			break;
		}
	}
	if (retry > 5) {
		TP_LOGE("[burn_fw_ss51]Alloc memory fail,exit.\n");
		return FAIL;
	}

	TP_LOGI("[burn_fw_ss51]Reset first 8K of ss51 to 0xFF.\n");
	TP_LOGD("[burn_fw_ss51]step2: reset bank0 0xC000~0xD000\n");
	memset(fw_ss51, 0xFF, FW_SECTION_LENGTH);

	/* step3:clear control flag */
	TP_LOGD("[burn_fw_ss51]step3:clear control flag\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_ss51]clear control flag fail.\n");
		ret = FAIL;
		goto exit_burn_fw_ss51;
	}

	/* step4:burn ss51 firmware section 1 */
	TP_LOGD("[burn_fw_ss51]step4:burn ss51 firmware section 1\n");
	ret = gup_burn_fw_section(client, fw_ss51, 0xC000, 0x01);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_ss51]burn ss51 firmware section 1 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step5:load ss51 firmware section 2 file data */
	TP_LOGD("[burn_fw_ss51]step5:load ss51 firmware section 2 file data\n");
	ret = gup_load_section_file(fw_ss51, FW_SECTION_LENGTH,
				    FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_ss51]load ss51 firmware section 2 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step6:burn ss51 firmware section 2 */
	TP_LOGD("[burn_fw_ss51]step6:burn ss51 firmware section 2\n");
	ret = gup_burn_fw_section(client, fw_ss51, 0xE000, 0x02);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_ss51]burn ss51 firmware section 2 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step7:load ss51 firmware section 3 file data */
	TP_LOGD("[burn_fw_ss51]step7:load ss51 firmware section 3 file data\n");
	ret = gup_load_section_file(fw_ss51, 2 * FW_SECTION_LENGTH,
				    FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_ss51]load ss51 firmware section 3 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step8:burn ss51 firmware section 3 */
	TP_LOGD("[burn_fw_ss51]step8:burn ss51 firmware section 3\n");
	ret = gup_burn_fw_section(client, fw_ss51, 0xC000, 0x13);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_ss51]burn ss51 firmware section 3 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step9:load ss51 firmware section 4 file data */
	TP_LOGD("[burn_fw_ss51]step9:load ss51 firmware section 4 file data\n");
	ret = gup_load_section_file(fw_ss51, 3 * FW_SECTION_LENGTH,
				    FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_ss51]load ss51 firmware section 4 fail.\n");
		goto exit_burn_fw_ss51;
	}

	/* step10:burn ss51 firmware section 4 */
	TP_LOGD("[burn_fw_ss51]step10:burn ss51 firmware section 4\n");
	ret = gup_burn_fw_section(client, fw_ss51, 0xE000, 0x14);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_ss51]burn ss51 firmware section 4 fail.\n");
		goto exit_burn_fw_ss51;
	}

	update_msg.fw_burned_len += (FW_SECTION_LENGTH*4);
	TP_LOGD("[burn_fw_ss51]Burned length:%d\n",
		update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_ss51:
	kfree(fw_ss51);
	return ret;
}

static u8 gup_burn_fw_dsp(struct i2c_client *client)
{
	s32 ret = 0;
	u8 *fw_dsp = NULL;
	u8 retry = 0;
	u8 rd_buf[5];

	TP_LOGI("[burn_fw_dsp]Begin burn dsp firmware---->>\n");
	/* step1:alloc memory */
	TP_LOGD("[burn_fw_dsp]step1:alloc memory\n");
	while (retry++ < 5) {
		fw_dsp = kzalloc(FW_DSP_LENGTH, GFP_KERNEL);
		if (fw_dsp == NULL) {
			continue;
		} else {
			TP_LOGD("[burn_fw_dsp]Alloc %dk byte memory success.\n",
				FW_SECTION_LENGTH / 1024);
			break;
		}
	}
	if (retry > 5) {
		TP_LOGE("[burn_fw_dsp]Alloc memory fail,exit.\n");
		return FAIL;
	}

	/* step2:load firmware dsp */
	TP_LOGD("[burn_fw_dsp]step2:load firmware dsp\n");
	ret = gup_load_section_file(fw_dsp, 4 * FW_SECTION_LENGTH,
				    FW_DSP_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_dsp]load firmware dsp fail.\n");
		goto exit_burn_fw_dsp;
	}

	/* step3:select bank3 */
	TP_LOGD("[burn_fw_dsp]step3:select bank3\n");
	ret = gup_set_ic_msg(client, _bRW_MISCTL__SRAM_BANK, 0x03);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_dsp]select bank3 fail.\n");
		ret = FAIL;
		goto exit_burn_fw_dsp;
	}

	/* step4:hold ss51 & dsp */
	TP_LOGD("[burn_fw_dsp]step4:hold ss51 & dsp\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_dsp]hold ss51 & dsp fail.\n");
		ret = FAIL;
		goto exit_burn_fw_dsp;
	}

	/* step5:set scramble */
	TP_LOGD("[burn_fw_dsp]step5:set scramble\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_dsp]set scramble fail.\n");
		ret = FAIL;
		goto exit_burn_fw_dsp;
	}

	/* step6:release ss51 & dsp */
	TP_LOGD("[burn_fw_dsp]step6:release ss51 & dsp\n");
	ret = gup_set_ic_msg(
	client, _rRW_MISCTL__SWRST_B0_, 0x04);/* 20121211 */
	if (ret <= 0) {
		TP_LOGE("[burn_fw_dsp]release ss51 & dsp fail.\n");
		ret = FAIL;
		goto exit_burn_fw_dsp;
	}
	/* must delay */
	usleep_range(1000, 1100);

	/* step7:burn 4k dsp firmware */
	TP_LOGD("[burn_fw_dsp]step7:burn 4k dsp firmware\n");
	ret = gup_burn_proc(client, fw_dsp, 0x9000, FW_DSP_LENGTH);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_dsp]burn fw_section fail.\n");
		goto exit_burn_fw_dsp;
	}

	/* step8:send burn cmd to move data to flash from sram */
	TP_LOGD(
		"[burn_fw_dsp]step8:send burn cmd to move data to flash from sram\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, 0x05);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_dsp]send burn cmd fail.\n");
		goto exit_burn_fw_dsp;
	}
	TP_LOGD("[burn_fw_dsp]Wait for the burn is complete......\n");
	do {
		ret = gup_get_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, rd_buf, 1);
		if (ret <= 0) {
			TP_LOGE("[burn_fw_dsp]Get burn state fail\n");
			goto exit_burn_fw_dsp;
		}
		usleep_range(10000, 11000);
		/* TP_LOGD(
		* "[burn_fw_dsp]Get burn state:%d.\n",
		*	rd_buf[GTP_ADDR_LENGTH]);
		*/
	} while (rd_buf[GTP_ADDR_LENGTH]);

	/* step9:recall check 4k dsp firmware */
	TP_LOGD("[burn_fw_dsp]step9:recall check 4k dsp firmware\n");
	ret = gup_recall_check(client, fw_dsp, 0x9000, FW_DSP_LENGTH);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_dsp]recall check 4k dsp firmware fail.\n");
		goto exit_burn_fw_dsp;
	}

	update_msg.fw_burned_len += FW_DSP_LENGTH;
	TP_LOGD("[burn_fw_dsp]Burned length:%d\n",
			update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_dsp:
	kfree(fw_dsp);

	return ret;
}

static u8 gup_burn_fw_boot(struct i2c_client *client)
{
	s32 ret = 0;
	u8 *fw_boot = NULL;
	u8	retry = 0;
	u8	rd_buf[5];

	TP_LOGI("[burn_fw_boot]Begin burn bootloader firmware---->>\n");

	/* step1:Alloc memory */
	TP_LOGD("[burn_fw_boot]step1:Alloc memory\n");
	while (retry++ < 5) {
		fw_boot = kzalloc(FW_BOOT_LENGTH, GFP_KERNEL);
		if (fw_boot == NULL) {
			continue;
		} else {
			TP_LOGD(
			"[burn_fw_boot]Alloc %dk byte memory success.\n",
			FW_BOOT_LENGTH / 1024);
			break;
		}
	}
	if (retry > 5) {
		TP_LOGE("[burn_fw_boot]Alloc memory fail,exit.\n");
		return FAIL;
	}

	/* step2:load firmware bootloader */
	TP_LOGD("[burn_fw_boot]step2:load firmware bootloader\n");
	ret = gup_load_section_file(fw_boot,
				    4 * FW_SECTION_LENGTH + FW_DSP_LENGTH,
				    FW_BOOT_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_boot]load firmware bootcode fail.\n");
		goto exit_burn_fw_boot;
	}

	/* step3:hold ss51 & dsp */
	TP_LOGD("[burn_fw_boot]step3:hold ss51 & dsp\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_boot]hold ss51 & dsp fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot;
	}

	/* step4:set scramble */
	TP_LOGD("[burn_fw_boot]step4:set scramble\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_boot]set scramble fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot;
	}

	/* step5:hold ss51 & release dsp */
	TP_LOGD("[burn_fw_boot]step5:hold ss51 & release dsp\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x04);
	/* 20121211 */
	if (ret <= 0) {
		TP_LOGE("[burn_fw_boot]release ss51 & dsp fail\n");
		ret = FAIL;
		goto exit_burn_fw_boot;
	}
	/* must delay */
	usleep_range(1000, 1100);

	/* step6:select bank3 */
	TP_LOGD("[burn_fw_boot]step6:select bank3\n");
	ret = gup_set_ic_msg(client, _bRW_MISCTL__SRAM_BANK, 0x03);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_boot]select bank3 fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot;
	}

	/* step6:burn 2k bootloader firmware */
	TP_LOGD("[burn_fw_boot]step6:burn 2k bootloader firmware\n");
	ret = gup_burn_proc(client, fw_boot, 0x9000, FW_BOOT_LENGTH);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_boot]burn fw_boot fail.\n");
		goto exit_burn_fw_boot;
	}

	/* step7:send burn cmd to move data to flash from sram */
	TP_LOGD(
		"[burn_fw_boot]step7:send burn cmd to move data to flash from sram\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, 0x06);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_boot]send burn cmd fail.\n");
		goto exit_burn_fw_boot;
	}
	TP_LOGD("[burn_fw_boot]Wait for the burn is complete......\n");
	do {
		ret = gup_get_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, rd_buf, 1);
		if (ret <= 0) {
			TP_LOGE("[burn_fw_boot]Get burn state fail\n");
			goto exit_burn_fw_boot;
		}
		usleep_range(10000, 11000);
	/* TP_LOGD("[burn_fw_boot]Get burn state:%d.\n",
	 * rd_buf[GTP_ADDR_LENGTH]);
	 */
	} while (rd_buf[GTP_ADDR_LENGTH]);

	/* step8:recall check 2k bootloader firmware */
	TP_LOGD("[burn_fw_boot]step8:recall check 2k bootloader firmware\n");
	ret = gup_recall_check(client, fw_boot, 0x9000, FW_BOOT_LENGTH);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_boot]recall check 2k bootcode firmware fail\n");
		goto exit_burn_fw_boot;
	}

	update_msg.fw_burned_len += FW_BOOT_LENGTH;
	TP_LOGD("[burn_fw_boot]Burned length:%d\n",
		update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_boot:
	kfree(fw_boot);

	return ret;
}
static u8 gup_burn_fw_boot_isp(struct i2c_client *client)
{
	s32 ret = 0;
	u8 *fw_boot_isp = NULL;
	u8	retry = 0;
	u8	rd_buf[5];

	if (update_msg.fw_burned_len >= update_msg.fw_total_len) {
		TP_LOGD("No need to upgrade the boot_isp code!\n");
		return SUCCESS;
	}
	TP_LOGI("[burn_fw_boot_isp]Begin burn boot_isp firmware---->>\n");

	/* step1:Alloc memory */
	TP_LOGD("[burn_fw_boot_isp]step1:Alloc memory\n");
	while (retry++ < 5) {
		fw_boot_isp = kzalloc(FW_BOOT_ISP_LENGTH, GFP_KERNEL);
		if (fw_boot_isp == NULL) {
			continue;
		} else {
			TP_LOGD(
			"[burn_fw_boot_isp]Alloc %dk byte memory success.\n",
			(FW_BOOT_ISP_LENGTH/1024));
			break;
		}
	}
	if (retry > 5) {
		TP_LOGE("[burn_fw_boot_isp]Alloc memory fail,exit.\n");
		return FAIL;
	}

	/* step2:load firmware bootloader */
	TP_LOGD("[burn_fw_boot_isp]step2:load firmware bootloader isp\n");
	/* ret = gup_load_section_file(fw_boot_isp,
	 * (4*FW_SECTION_LENGTH+FW_DSP_LENGTH +
	 * FW_BOOT_LENGTH+FW_DSP_ISP_LENGTH), FW_BOOT_ISP_LENGTH, SEEK_SET);
	 */
	ret = gup_load_section_file(fw_boot_isp,
				(update_msg.fw_burned_len - FW_DSP_ISP_LENGTH),
				    FW_BOOT_ISP_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_boot_isp]load firmware boot_isp fail.\n");
		goto exit_burn_fw_boot_isp;
	}

	/* step3:hold ss51 & dsp */
	TP_LOGD("[burn_fw_boot_isp]step3:hold ss51 & dsp\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_boot_isp]hold ss51 & dsp fail\n");
		ret = FAIL;
		goto exit_burn_fw_boot_isp;
	}

	/* step4:set scramble */
	TP_LOGD("[burn_fw_boot_isp]step4:set scramble\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_boot_isp]set scramble fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot_isp;
	}


	/* step5:hold ss51 & release dsp */
	TP_LOGD("[burn_fw_boot_isp]step5:hold ss51 & release dsp\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x04);
	/* 20121211 */
	if (ret <= 0) {
		TP_LOGE("[burn_fw_boot_isp]release ss51 & dsp fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot_isp;
	}
	/* must delay */
	usleep_range(1000, 2000);

	/* step6:select bank3 */
	TP_LOGD("[burn_fw_boot_isp]step6:select bank3\n");
	ret = gup_set_ic_msg(client, _bRW_MISCTL__SRAM_BANK, 0x03);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_boot_isp]select bank3 fail.\n");
		ret = FAIL;
		goto exit_burn_fw_boot_isp;
	}

	/* step7:burn 2k bootload_isp firmware */
	TP_LOGD("[burn_fw_boot_isp]step7:burn 2k bootloader firmware\n");
	ret = gup_burn_proc(client, fw_boot_isp, 0x9000, FW_BOOT_ISP_LENGTH);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_boot_isp]burn fw_section fail.\n");
		goto exit_burn_fw_boot_isp;
	}

	/* step7:send burn cmd to move data to flash from sram */
	TP_LOGD(
	"[burn_fw_boot_isp]step8:send burn cmd to move data to flash from sram\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, 0x07);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_boot_isp]send burn cmd fail.\n");
		goto exit_burn_fw_boot_isp;
	}
	TP_LOGD("[burn_fw_boot_isp]Wait for the burn is complete......\n");
	do {
		ret = gup_get_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, rd_buf, 1);
		if (ret <= 0) {
			TP_LOGE("[burn_fw_boot_isp]Get burn state fail\n");
			goto exit_burn_fw_boot_isp;
		}
		usleep_range(10000, 11000);
	/* TP_LOGD("[burn_fw_boot_isp]Get
	 * burn state:%d.", rd_buf[GTP_ADDR_LENGTH]);
	 */
	} while (rd_buf[GTP_ADDR_LENGTH]);

	/* step8:recall check 2k bootload_isp firmware */
	TP_LOGD(
	"[burn_fw_boot_isp]step9:recall check 2k bootloader firmware\n");
	ret = gup_recall_check(client, fw_boot_isp, 0x9000, FW_BOOT_ISP_LENGTH);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_boot_isp]recall check 2k bootcode_isp firmware fail.\n");
		goto exit_burn_fw_boot_isp;
	}

	update_msg.fw_burned_len += FW_BOOT_ISP_LENGTH;
	TP_LOGD("[burn_fw_boot_isp]Burned length:%d\n",
		update_msg.fw_burned_len);

	ret = SUCCESS;

exit_burn_fw_boot_isp:
	kfree(fw_boot_isp);

	return ret;
}

static u8 gup_burn_fw_link(struct i2c_client *client)
{
	u8 *fw_link = NULL;
	u8	retry = 0;
	s32 ret = 0;
	u32 offset;

	if (update_msg.fw_burned_len >= update_msg.fw_total_len) {
		TP_LOGD("No need to upgrade the link code!\n");
		return SUCCESS;
	}
	TP_LOGI("[burn_fw_link]Begin burn link firmware---->>\n");

	/* step1:Alloc memory */
	TP_LOGD("[burn_fw_link]step1:Alloc memory\n");
	while (retry++ < 5) {
		fw_link = kzalloc(FW_SECTION_LENGTH, GFP_KERNEL);
		if (fw_link == NULL) {
			continue;
		} else {
			TP_LOGD(
			"[burn_fw_link]Alloc %dk byte memory success.\n",
			(FW_SECTION_LENGTH/1024));
			break;
		}
	}
	if (retry > 5) {
		TP_LOGE("[burn_fw_link]Alloc memory fail,exit.\n");
		return FAIL;
	}

	/* step2:load firmware link section 1 */
	TP_LOGD("[burn_fw_link]step2:load firmware link section 1\n");
	offset = update_msg.fw_burned_len - FW_DSP_ISP_LENGTH;
	ret = gup_load_section_file(fw_link, offset,
				FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_link]load firmware link section 1 fail.\n");
		goto exit_burn_fw_link;
	}

	/* step3:burn link firmware section 1 */
	TP_LOGD("[burn_fw_link]step3:burn link firmware section 1\n");
	ret = gup_burn_fw_gwake_section(
	client, fw_link, 0x9000, FW_SECTION_LENGTH, 0x38);

	if (ret == FAIL) {
		TP_LOGE("[burn_fw_link]burn link firmware section 1 fail.\n");
		goto exit_burn_fw_link;
	}

	/* step4:load link firmware section 2 file data */
	TP_LOGD("[burn_fw_link]step4:load link firmware section 2 file data\n");
	offset += FW_SECTION_LENGTH;
	ret = gup_load_section_file(fw_link, offset,
				    FW_GLINK_LENGTH -
				    FW_SECTION_LENGTH, SEEK_SET);

	if (ret == FAIL) {
		TP_LOGE("[burn_fw_link]load link firmware section 2 fail.\n");
		goto exit_burn_fw_link;
	}

	/* step5:burn link firmware section 2 */
	TP_LOGD("[burn_fw_link]step4:burn link firmware section 2\n");
	ret = gup_burn_fw_gwake_section(client,
	fw_link, 0x9000, FW_GLINK_LENGTH - FW_SECTION_LENGTH, 0x39);

	if (ret == FAIL) {
		TP_LOGE("[burn_fw_link]burn link firmware section 2 fail.\n");
		goto exit_burn_fw_link;
	}

	update_msg.fw_burned_len += FW_GLINK_LENGTH;
	TP_LOGD("[burn_fw_link]Burned length:%d\n", update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_link:
	kfree(fw_link);

	return ret;
}

static u8 gup_burn_fw_gwake_section(struct i2c_client *client,
		u8 *fw_section, u16 start_addr, u32 len, u8 bank_cmd)
{
	s32 ret = 0;
	u8	rd_buf[5];

	/* step1:hold ss51 & dsp */
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x0C);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_app_section]hold ss51 & dsp fail.\n");
		return FAIL;
	}

	/* step2:set scramble */
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_OPT_B0_, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_app_section]set scramble fail.\n");
		return FAIL;
	}

	/* step3:hold ss51 & release dsp */
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x04);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_app_section]hold ss51 & release dsp fail.\n");
		return FAIL;
	}
	/* must delay */
	usleep_range(1000, 2000);

	/* step4:select bank */
	ret = gup_set_ic_msg(
	client, _bRW_MISCTL__SRAM_BANK, (bank_cmd >> 4)&0x0F);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_section]select bank %d fail.\n",
		(bank_cmd >> 4)&0x0F);
		return FAIL;
	}

	/* step5:burn fw section */
	ret = gup_burn_proc(client, fw_section, start_addr, len);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_app_section]burn fw_section fail.\n");
		return FAIL;
	}

	/* step6:send burn cmd to move data to flash from sram */
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, bank_cmd&0x0F);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_app_section]send burn cmd fail.\n");
		return FAIL;
	}
	TP_LOGD("[burn_fw_section]Wait for the burn is complete......\n");
	do {
		ret = gup_get_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, rd_buf, 1);
		if (ret <= 0) {
			TP_LOGE("[burn_fw_app_section]Get burn state fail\n");
			return FAIL;
		}
		usleep_range(10000, 11000);
	/* TP_LOGD("[burn_fw_app_section]Get burn state:%d.\n"
	 * rd_buf[GTP_ADDR_LENGTH]);
	 */
	} while (rd_buf[GTP_ADDR_LENGTH]);

	/* step7:recall fw section */
	ret = gup_recall_check(client, fw_section, start_addr, len);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_app_section]recall check %dk firmware fail.\n",
		len/1024);
		return FAIL;
	}

	return SUCCESS;
}

static u8 gup_burn_fw_gwake(struct i2c_client *client)
{
	u8 *fw_gwake = NULL;
	u8	retry = 0;
	s32 ret = 0;
	u16 start_index = 4*FW_SECTION_LENGTH +
	FW_DSP_LENGTH + FW_BOOT_LENGTH +
	FW_BOOT_ISP_LENGTH + FW_GLINK_LENGTH;/* 32 + 4 + 2 + 4 = 42K */
	/* u16 start_index; */

	if (start_index >= update_msg.fw_total_len) {
		TP_LOGD("No need to upgrade the gwake code!\n");
		return SUCCESS;
	}
	/* start_index = update_msg.fw_burned_len - FW_DSP_ISP_LENGTH; */
	TP_LOGI("[burn_fw_gwake]Begin burn gwake firmware---->>\n");

	/* step1:alloc memory */
	TP_LOGD("[burn_fw_gwake]step1:alloc memory\n");
	while (retry++ < 5) {
		fw_gwake =
	kzalloc(FW_SECTION_LENGTH, GFP_KERNEL);
		if (fw_gwake == NULL) {
			continue;
		} else {
			TP_LOGD(
			"[burn_fw_gwake]Alloc %dk byte memory success.\n",
			(FW_SECTION_LENGTH/1024));
			break;
		}
	}
	if (retry > 5) {
		TP_LOGE("[burn_fw_gwake]Alloc memory fail,exit.\n");
		return FAIL;
	}

	/* clear control flag */
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_finish]clear control flag fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step2:load app_code firmware section 1 file data */
	TP_LOGD(
		"[burn_fw_gwake]step2:load app_code firmware section 1 file data\n");
	ret = gup_load_section_file(fw_gwake, start_index,
				    FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_gwake]load app_code firmware section 1 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step3:burn app_code firmware section 1 */
	TP_LOGD("[burn_fw_gwake]step3:burn app_code firmware section 1\n");
	ret = gup_burn_fw_gwake_section(client,
	fw_gwake, 0x9000, FW_SECTION_LENGTH, 0x3A);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_gwake]burn app_code firmware section 1 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step5:load app_code firmware section 2 file data */
	TP_LOGD(
	"[burn_fw_gwake]step5:load app_code firmware section 2 file data\n");
	ret = gup_load_section_file(fw_gwake, start_index+FW_SECTION_LENGTH,
				    FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_gwake]load app_code firmware section 2 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step6:burn app_code firmware section 2 */
	TP_LOGD("[burn_fw_gwake]step6:burn app_code firmware section 2\n");
	ret = gup_burn_fw_gwake_section(client,
	fw_gwake, 0x9000, FW_SECTION_LENGTH, 0x3B);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_gwake]burn app_code firmware section 2 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step7:load app_code firmware section 3 file data */
	TP_LOGD(
		"[burn_fw_gwake]step7:load app_code firmware section 3 file data\n");
	ret = gup_load_section_file(fw_gwake,
				    start_index + 2 * FW_SECTION_LENGTH,
				    FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_gwake]load app_code firmware section 3 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step8:burn app_code firmware section 3 */
	TP_LOGD("[burn_fw_gwake]step8:burn app_code firmware section 3\n");
	ret = gup_burn_fw_gwake_section(
	client, fw_gwake, 0x9000, FW_SECTION_LENGTH, 0x3C);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_gwake]burn app_code firmware section 3 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step9:load app_code firmware section 4 file data */
	TP_LOGD(
		"[burn_fw_gwake]step9:load app_code firmware section 4 file data\n");
	ret = gup_load_section_file(fw_gwake,
				    start_index + 3 * FW_SECTION_LENGTH,
				    FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_gwake]load app_code firmware section 4 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* step10:burn app_code firmware section 4 */
	TP_LOGD("[burn_fw_gwake]step10:burn app_code firmware section 4\n");
	ret = gup_burn_fw_gwake_section(
	client, fw_gwake, 0x9000, FW_SECTION_LENGTH, 0x3D);
	if (ret == FAIL) {
		TP_LOGE(
		"[burn_fw_gwake]burn app_code firmware section 4 fail.\n");
		goto exit_burn_fw_gwake;
	}

	/* update_msg.fw_burned_len += FW_GWAKE_LENGTH; */
	TP_LOGD("[burn_fw_gwake]Burned length:%d\n", update_msg.fw_burned_len);
	ret = SUCCESS;

exit_burn_fw_gwake:
	kfree(fw_gwake);

	return ret;
}

static u8 gup_burn_fw_finish(struct i2c_client *client)
{
	u8 *fw_ss51 = NULL;
	u8	retry = 0;
	s32 ret = 0;

	TP_LOGI("[burn_fw_finish]burn first 8K of ss51 and finish update.\n");
	/* step1:alloc memory */
	TP_LOGD("[burn_fw_finish]step1:alloc memory\n");
	while (retry++ < 5) {
		fw_ss51 = kzalloc(FW_SECTION_LENGTH, GFP_KERNEL);
		if (fw_ss51 == NULL) {
			continue;
		} else {
			TP_LOGD(
			"[burn_fw_finish]Alloc %dk byte memory success.\n",
			(FW_SECTION_LENGTH/1024));
			break;
		}
	}
	if (retry > 5) {
		TP_LOGE("[burn_fw_finish]Alloc memory fail,exit.\n");
		return FAIL;
	}

	TP_LOGD("[burn_fw_finish]step2: burn ss51 first 8K.\n");
	ret = gup_load_section_file(fw_ss51, 0, FW_SECTION_LENGTH, SEEK_SET);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_finish]load ss51 firmware section 1 fail.\n");
		goto exit_burn_fw_finish;
	}

	TP_LOGD("[burn_fw_finish]step3:clear control flag\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, 0x00);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_finish]clear control flag fail.\n");
		goto exit_burn_fw_finish;
	}

	TP_LOGD("[burn_fw_finish]step4:burn ss51 firmware section 1\n");
	ret = gup_burn_fw_section(client, fw_ss51, 0xC000, 0x01);
	if (ret == FAIL) {
		TP_LOGE("[burn_fw_finish]burn ss51 firmware section 1 fail.\n");
		goto exit_burn_fw_finish;
	}

	/* step11:enable download DSP code */
	TP_LOGD("[burn_fw_finish]step5:enable download DSP code\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__BOOT_CTL_, 0x99);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_finish]enable download DSP code fail.\n");
		goto exit_burn_fw_finish;
	}

	/* step12:release ss51 & hold dsp */
	TP_LOGD("[burn_fw_finish]step6:release ss51 & hold dsp\n");
	ret = gup_set_ic_msg(client, _rRW_MISCTL__SWRST_B0_, 0x08);
	if (ret <= 0) {
		TP_LOGE("[burn_fw_finish]release ss51 & hold dsp fail.\n");
		goto exit_burn_fw_finish;
	}

	if (fw_ss51 != NULL)
		kfree(fw_ss51);
	return SUCCESS;

exit_burn_fw_finish:
	if (fw_ss51 != NULL)
		kfree(fw_ss51);

	return FAIL;
}

/* return 0 can update, else no update condition */
static int gup_update_condition_check(struct goodix_ts_data *ts)
{
	if (test_bit(SLEEP_MODE, &ts->flags)) {
		TP_LOGI("Update abort, tp in sleep mode\n");
		return -EINVAL;
	}

	return 0;
}
s32 gup_update_proc(void *dir)
{
	s32 ret = 0;
	s32 update_ret = FAIL;
	u8	retry = 0;
	struct st_fw_head fw_head;
	struct goodix_ts_data *ts = NULL;

	ts = i2c_get_clientdata(i2c_connect_client);

	TP_LOGD("[update_proc]Begin update ......\n");

	show_len = 1;
	total_len = 100;

	ret = gup_update_condition_check(ts);
	if (ret) {
		TP_LOGW("Update start failed\n");
		return FAIL;
	}

	if (test_and_set_bit(FW_UPDATE_RUNNING, &ts->flags)) {
		TP_LOGW("FW update may already running\n");
		return FAIL;
	}

	ret = gup_get_update_file(i2c_connect_client, &fw_head, (u8 *)dir);
	if (ret == FAIL) {
		TP_LOGE("Failed get valied firmware data\n");
		clear_bit(FW_UPDATE_RUNNING, &ts->flags);
		return FAIL;
	}

	gtp_work_control_enable(ts, false);
	gtp_esd_off(ts);

	ret = gup_get_ic_fw_msg(i2c_connect_client);
	if (ret == FAIL) {
		TP_LOGE("[update_proc]get ic message fail.\n");
		goto file_fail;
	}

	if (ts->force_update || dir) {
		TP_LOGD("Enter force update.\n");
	} else {
		ret = gup_enter_update_judge(i2c_connect_client, &fw_head);
		if (ret == FAIL) {
			TP_LOGI("[update_proc]Doesn't meet update condition\n");
			goto file_fail;
		}
	}

	ret = gup_enter_update_mode(ts->client);
	if (ret == FAIL) {
		TP_LOGE("[update_proc]enter update mode fail.\n");
		goto update_fail;
	}

	while (retry++ < 5) {
		show_len = 10;
		total_len = 100;
		update_msg.fw_burned_len = 0;
		ret = gup_burn_dsp_isp(i2c_connect_client);
		if (ret == FAIL) {
			TP_LOGE("[update_proc]burn dsp isp fail.\n");
			continue;
		}

		show_len = 20;
		ret = gup_burn_fw_gwake(i2c_connect_client);
		if (ret == FAIL) {
			TP_LOGE("[update_proc]burn app_code firmware fail.\n");
			continue;
		}

		show_len = 30;
		ret = gup_burn_fw_ss51(i2c_connect_client);
		if (ret == FAIL) {
			TP_LOGE("[update_proc]burn ss51 firmware fail.\n");
			continue;
		}

		show_len = 40;
		ret = gup_burn_fw_dsp(i2c_connect_client);
		if (ret == FAIL) {
			TP_LOGE("[update_proc]burn dsp firmware fail.\n");
			continue;
		}

		show_len = 50;
		ret = gup_burn_fw_boot(i2c_connect_client);
		if (ret == FAIL) {
			TP_LOGE(
			"[update_proc]burn bootloader firmware fail.\n");
			continue;
		}
		show_len = 60;

		ret = gup_burn_fw_boot_isp(i2c_connect_client);
		if (ret == FAIL) {
			TP_LOGE("[update_proc]burn boot_isp firmware fail.\n");
			continue;
		}

		show_len = 70;
		ret = gup_burn_fw_link(i2c_connect_client);
		if (ret == FAIL) {
			TP_LOGE("[update_proc]burn link firmware fail.\n");
			continue;
		}

		show_len = 80;
		ret = gup_burn_fw_finish(i2c_connect_client);
		if (ret == FAIL) {
			TP_LOGE("[update_proc]burn finish fail.\n");
			continue;
		}
		show_len = 90;
		TP_LOGI("[update_proc]UPDATE SUCCESS.\n");
		retry = 0;
		break;
	}

	if (retry >= 5) {
		TP_LOGE("[update_proc]retry timeout,UPDATE FAIL.\n");
		update_ret = FAIL;
	} else {
		update_ret = SUCCESS;
	}

update_fail:
	TP_LOGD("[update_proc]leave update mode.\n");
	gup_leave_update_mode(i2c_connect_client);

	msleep(GTP_100_DLY_MS);

	if (update_ret == SUCCESS) {
		TP_LOGI("firmware error auto update, resent config!\n");
		if (ts->pdata->auto_update_cfg) {
			/* Get Sensor ID again (if fw broken previous) */
			gtp_get_fw_info(ts->client, &ts->fw_info);
			ret = gup_update_config(i2c_connect_client);
			if (ret <= 0)
				TP_LOGE("Update config failed.\n");
		} else {
			gup_init_panel(ts);
		}
	}
	gtp_get_fw_info(ts->client, &ts->fw_info);

file_fail:

	update_msg.fw_data = NULL;
	update_msg.fw_total_len = 0;
	release_firmware(update_msg.fw);

	clear_bit(FW_UPDATE_RUNNING, &ts->flags);
	gtp_work_control_enable(ts, true);
	gtp_esd_on(ts);
	total_len = 100;
	ts->force_update = false;

	if (update_ret == SUCCESS) {
		show_len = 100;
		clear_bit(FW_ERROR, &ts->flags);
		return SUCCESS;
	}

	show_len = 200;
	return FAIL;
}

u8 gup_init_update_proc(struct goodix_ts_data *ts)
{
	struct task_struct *thread = NULL;

	TP_LOGD("Ready to run update thread.\n");

	thread = kthread_run(gup_update_proc,
			(void *)NULL, "guitar_update\n");

	ts->fw_auto_update_complete = GTP_FW_UPDATE_COMPLETE;

	if (IS_ERR(thread)) {
		TP_LOGE("Failed to create update thread.\n");
		return -EPERM;
	}

	return 0;
}
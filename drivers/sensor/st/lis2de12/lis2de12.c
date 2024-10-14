/* ST Microelectronics LIS2DE12 3-axis accelerometer sensor driver
 *
 * Copyright (c) 2024 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Datasheet:
 * https://www.st.com/resource/en/datasheet/lis2de12.pdf
 */

#define DT_DRV_COMPAT st_lis2de12

#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <string.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>

#include "lis2de12.h"

LOG_MODULE_REGISTER(LIS2DE12, CONFIG_SENSOR_LOG_LEVEL);

static const uint16_t lis2de12_odr_map[10] = { 0, 1, 10, 25, 50, 100, 200, 400, 1620, 5376};

#ifdef CONFIG_SENSOR_LOG_LEVEL_DBG
static uint8_t reg_buffer[200];

int lis2de12_print_registers(const struct device *dev)
{

	const struct lis2de12_config *cfg = dev->config;
	stmdev_ctx_t *ctx = (stmdev_ctx_t *)&cfg->ctx;

	LOG_DBG("LIS2DE12 Register Dump:");

	uint8_t reg = 0;
	uint8_t reg_value = 0;

	reg = LIS2DE12_STATUS_REG_AUX;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_STATUS_REG_AUX, "LIS2DE12_STATUS_REG_AUX",
		reg_value);
	LOG_DBG("\tTOR=%s", ((reg_value) & (1 << (7 - 2)) ? true : false)
			? "new temperature has overwritten the previous data"
				    : "no overrun has occurred");
	LOG_DBG("\tTDA=%s", ((reg_value) & (1 << (7 - 5)) ? true : false)
			? "new temperature is available"
			: "new temperature data is not yet available");

	reg = LIS2DE12_OUT_TEMP_L;
	if (lis2de12_read_reg(ctx, reg, &reg_buffer[0], 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	reg = LIS2DE12_OUT_TEMP_H;
	if (lis2de12_read_reg(ctx, reg, &reg_buffer[1], 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_OUT_TEMP_L, "LIS2DE12_OUT_TEMP_L",
		reg_buffer[0]);
	LOG_DBG("\tTemperatur=%d C", (int16_t)((reg_buffer[1] << 8) | reg_buffer[0]));

	reg = LIS2DE12_WHO_AM_I;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_WHO_AM_I, "LIS2DE12_WHO_AM_I", reg_value);
	LOG_DBG("\tShould be 0x33=%02X", reg_value);

	reg = LIS2DE12_CTRL_REG0;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_CTRL_REG0, "LIS2DE12_CTRL_REG0", reg_value);
	LOG_DBG("\tSDO_PU_DISC=%s", ((reg_value) & (1 << (7 - 0)) ? true : false)
			? "pull-up disconnected to SDO/SA0 pin"
					    : "pull-up connected to SDO/SA0 pin");
	LOG_DBG("\tDevice:%s", ((reg_value) & (1 << (7 - 1)) ? true : false) == 0 &&
				((reg_value) & (1 << (7 - 2)) ? true : false) == 0 &&
				((reg_value) & (1 << (7 - 3)) ? true : false) == 1 &&
				((reg_value) & (1 << (7 - 4)) ? true : false) == 0 &&
				((reg_value) & (1 << (7 - 5)) ? true : false) == 0 &&
				((reg_value) & (1 << (7 - 6)) ? true : false) == 0 &&
				((reg_value) & (1 << (7 - 7)) ? true : false) == 0
			? "Normal Operation"
				       : "ERROR");
	LOG_DBG("\t1=%d,2=%d,3=%d,4=%d,5=%d,6=%d,7=%d",
		((reg_value) & (1 << (7 - 1)) ? true : false),
		((reg_value) & (1 << (7 - 2)) ? true : false),
		((reg_value) & (1 << (7 - 3)) ? true : false),
		((reg_value) & (1 << (7 - 4)) ? true : false),
		((reg_value) & (1 << (7 - 5)) ? true : false),
		((reg_value) & (1 << (7 - 6)) ? true : false),
		((reg_value) & (1 << (7 - 7)) ? true : false));

	reg = LIS2DE12_TEMP_CFG_REG;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_TEMP_CFG_REG, "LIS2DE12_TEMP_CFG_REG",
		reg_value);
	LOG_DBG("\tTemperaturEnable=%s",
		((reg_value) & (1 << (7 - 0)) ? true : false) == 1 &&
				((reg_value) & (1 << (7 - 1)) ? true : false) == 0
			? "Enabled"
			: "Disabled");

	reg = LIS2DE12_CTRL_REG1;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	uint8_t odr_raw = ((reg_value) & (1 << (7 - 0)) ? true : false) * 8 +
			  ((reg_value) & (1 << (7 - 1)) ? true : false) * 4 +
			  ((reg_value) & (1 << (7 - 2)) ? true : false) * 2 +
			  ((reg_value) & (1 << (7 - 3)) ? true : false) * 1;
	uint16_t odr = lis2de12_odr_map[odr_raw];

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_CTRL_REG1, "LIS2DE12_CTRL_REG1", reg_value);
	LOG_DBG("\tODR=%d Hz, %d", odr, odr_raw);
	LOG_DBG("\tLPen=%s", ((reg_value) & (1 << (7 - 4)) ? true : false) ? "Normal" : "ERROR");
	LOG_DBG("\tZen=%s", ((reg_value) & (1 << (7 - 5)) ? true : false) ? "enabled" : "disabled");
	LOG_DBG("\tYen=%s", ((reg_value) & (1 << (7 - 6)) ? true : false) ? "enabled" : "disabled");
	LOG_DBG("\tXen=%s", ((reg_value) & (1 << (7 - 7)) ? true : false) ? "enabled" : "disabled");

	reg = LIS2DE12_CTRL_REG2;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	uint8_t hpm = ((reg_value) & (1 << (7 - 0)) ? true : false) * 2 +
		      ((reg_value) & (1 << (7 - 1)) ? true : false);

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_CTRL_REG2, "LIS2DE12_CTRL_REG2", reg_value);
	LOG_DBG("\tHighpass=%s", hpm == 0
					 ? "Normal mode (reset by reading REFERENCE (26h) register)"
		: hpm == 1 ? "Reference signal for filtering"
		: hpm == 2 ? "Normal Mode"
		: hpm == 3 ? "Autoreset on interrupt event"
					    : "none");
	LOG_DBG("\tHPCF=%d%d", ((reg_value) & (1 << (7 - 2)) ? true : false),
		((reg_value) & (1 << (7 - 3)) ? true : false));
	LOG_DBG("\tFDS=%s", ((reg_value) & (1 << (7 - 4)) ? true : false)
				    ? "data from internal filter sent to output register and FIFO"
				    : "internal filter bypassed");
	LOG_DBG("\tHPCLICK=%s",
		((reg_value) & (1 << (7 - 5)) ? true : false) ? "filter enabled" : "bypassed");
	LOG_DBG("\tHP_IA2=%s",
		((reg_value) & (1 << (7 - 6)) ? true : false) ? "filter enabled" : "bypassed");
	LOG_DBG("\tHP_IA1=%s",
		((reg_value) & (1 << (7 - 7)) ? true : false) ? "filter enabled" : "bypassed");

	reg = LIS2DE12_CTRL_REG3;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_CTRL_REG3, "LIS2DE12_CTRL_REG3", reg_value);
	LOG_DBG("\tI1_CLICK=%s",
		((reg_value) & (1 << (7 - 0)) ? true : false) ? "enabled" : "disable");
	LOG_DBG("\tI1_IA1=%s",
		((reg_value) & (1 << (7 - 1)) ? true : false) ? "enabled" : "disable");
	LOG_DBG("\tI1_IA2=%s",
		((reg_value) & (1 << (7 - 2)) ? true : false) ? "enabled" : "disable");
	LOG_DBG("\tI1_ZYXDA=%s",
		((reg_value) & (1 << (7 - 3)) ? true : false) ? "enabled" : "disable");
	LOG_DBG("\tStatic=%s", ((reg_value) & (1 << (7 - 4)) ? true : false) ? "ERROR" : "Normal");
	LOG_DBG("\tI1_WTM=%s",
		((reg_value) & (1 << (7 - 5)) ? true : false) ? "enabled" : "disable");
	LOG_DBG("\tI1_OVERRUN=%s",
		((reg_value) & (1 << (7 - 6)) ? true : false) ? "enabled" : "disable");

	reg = LIS2DE12_CTRL_REG4;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	uint8_t fs_raw = ((reg_value) & (1 << (7 - 2)) ? true : false) * 2 +
			 ((reg_value) & (1 << (7 - 3)) ? true : false);
	uint8_t fs = 0;
	switch (fs_raw) {
	case 0:
		fs = 2;
		break;
	case 1:
		fs = 4;
		break;
	case 2:
		fs = 8;
		break;
	case 3:
		fs = 16;
		break;
	}
	uint8_t st = ((reg_value) & (1 << (7 - 5)) ? true : false) * 2 +
		     ((reg_value) & (1 << (7 - 6)) ? true : false);

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_CTRL_REG4, "LIS2DE12_CTRL_REG4", reg_value);
	LOG_DBG("\tBTU=%s",
		((reg_value) & (1 << (7 - 0)) ? true : false)
			? "continuous update"
			: "output registers not updated until MSB and LSB have been read");
	LOG_DBG("\tStatic=%s", ((reg_value) & (1 << (7 - 1)) ? true : false) ? "ERROR" : "Normal");
	LOG_DBG("\tFull-Scale=%d: %02X g", fs, fs_raw);
	LOG_DBG("\tStatic=%s", ((reg_value) & (1 << (7 - 4)) ? true : false) ? "ERROR" : "Normal");
	LOG_DBG("\tSelfTest=%s", st == 0   ? "Normal"
		: st == 1 ? "Test 0"
		: st == 2 ? "Test 1"
					   : "none");
	LOG_DBG("\tSIM=%s", ((reg_value) & (1 << (7 - 4)) ? true : false) ? "3-wire interface"
							      : "4-wire interface");

	reg = LIS2DE12_CTRL_REG5;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_CTRL_REG5, "LIS2DE12_CTRL_REG5", reg_value);
	LOG_DBG("\tBoot=%s", ((reg_value) & (1 << (7 - 0)) ? true : false) ? "reboot memory content"
									   : "normal mode");
	LOG_DBG("\tFIFO_EN=%s",
		((reg_value) & (1 << (7 - 1)) ? true : false) ? "enabled" : "disabled");
	LOG_DBG("\tStatic=%s", ((reg_value) & (1 << (7 - 2)) ? true : false) ? "ERROR" : "Normal");
	LOG_DBG("\tStatic=%s", ((reg_value) & (1 << (7 - 3)) ? true : false) ? "ERROR" : "Normal");
	LOG_DBG("\tLIR_INT1=%s", ((reg_value) & (1 << (7 - 4)) ? true : false)
					 ? "interrupt request latched"
					 : "interrupt request not latched");
	LOG_DBG("\tD4D_INT1=%s", ((reg_value) & (1 << (7 - 5)) ? true : false)
			? "4D enable: 4D detection is enabled on INT1 pin when 6D "
			  "bit on INT1_CFG (30h) is set to 1"
					 : "disabled");
	LOG_DBG("\tLIR_INT2=%s", ((reg_value) & (1 << (7 - 6)) ? true : false)
					 ? "interrupt request latched"
					 : "interrupt request not latched");
	LOG_DBG("\tD4D_INT2=%s", ((reg_value) & (1 << (7 - 7)) ? true : false)
			? "4D enable: 4D detection is enabled on INT2 pin when 6D "
			  "bit on INT2_CFG (34h) is set to 1"
					 : "disabled");

	reg = LIS2DE12_CTRL_REG6;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_CTRL_REG6, "LIS2DE12_CTRL_REG6", reg_value);
	LOG_DBG("\tClick on Int2=%s",
		((reg_value) & (1 << (7 - 0)) ? true : false) ? "enabled" : "disabled");
	LOG_DBG("\tInt1 func on Int2Pin=%s",
		((reg_value) & (1 << (7 - 1)) ? true : false) ? "enabled" : "disabled");
	LOG_DBG("\tInt2 func on Int2Pin=%s",
		((reg_value) & (1 << (7 - 2)) ? true : false) ? "enabled" : "disabled");
	LOG_DBG("\tBoot on Int2Pin=%s",
		((reg_value) & (1 << (7 - 3)) ? true : false) ? "enabled" : "disabled");
	LOG_DBG("\tActivity in Int2Pin=%s",
		((reg_value) & (1 << (7 - 4)) ? true : false) ? "enabled" : "disabled");
	LOG_DBG("\tStatic=%s", ((reg_value) & (1 << (7 - 5)) ? true : false) ? "ERROR" : "normal");
	LOG_DBG("\tStatic=%s polarity=%s",
		((reg_value) & (1 << (7 - 6)) ? true : false) ? "active-low" : "active-high",
		((reg_value) & (1 << (7 - 7)) ? true : false) ? "ERROR" : "normal");

	reg = LIS2DE12_REFERENCE;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_REFERENCE, "LIS2DE12_REFERENCE", reg_value);
	LOG_DBG("\tRef=%d", reg_value);

	reg = LIS2DE12_STATUS_REG;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_STATUS_REG, "LIS2DE12_STATUS_REG", reg_value);
	LOG_DBG("\tZYXOR=%s",
		((reg_value) & (1 << (7 - 0)) ? true : false) ? "overrun" : "no overrun");
	LOG_DBG("\tZOR=%s",
		((reg_value) & (1 << (7 - 1)) ? true : false) ? "overrun" : "no overrun");
	LOG_DBG("\tYOR=%s",
		((reg_value) & (1 << (7 - 2)) ? true : false) ? "overrun" : "no overrun");
	LOG_DBG("\tXOR=%s",
		((reg_value) & (1 << (7 - 3)) ? true : false) ? "overrun" : "no overrun");
	LOG_DBG("\tZYXDA=%s",
		((reg_value) & (1 << (7 - 4)) ? true : false) ? "new data" : "old data");
	LOG_DBG("\tZDA=%s",
		((reg_value) & (1 << (7 - 5)) ? true : false) ? "new data" : "old data");
	LOG_DBG("\tYDA=%s",
		((reg_value) & (1 << (7 - 6)) ? true : false) ? "new data" : "old data");
	LOG_DBG("\tXDA=%s",
		((reg_value) & (1 << (7 - 7)) ? true : false) ? "new data" : "old data");

	reg = LIS2DE12_FIFO_READ_START;
	if (lis2de12_read_reg(ctx, reg, reg_buffer, 192) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	for (int a = 0; a < 32; a++) {
		LOG_DBG("REG:0x%02X, %s : 0x%02X : nth=%2d x=%5d,y=%5d,z=%5d",
			LIS2DE12_FIFO_READ_START, "LIS2DE12_FIFO_READ_START", 0, a,
			reg_buffer[a * 6], reg_buffer[a * 6 + 2], reg_buffer[a * 6 + 4]);
	}

	reg = LIS2DE12_OUT_X_H;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_OUT_X_H, "LIS2DE12_OUT_X_H", reg_value);
	LOG_DBG("\tX=%5d", reg_value);

	reg = LIS2DE12_OUT_Y_H;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}

	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_OUT_Y_H, "LIS2DE12_OUT_Y_H", reg_value);
	LOG_DBG("\tY=%5d", reg_value);

	reg = LIS2DE12_OUT_Z_H;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_OUT_Z_H, "LIS2DE12_OUT_Z_H", reg_value);
	LOG_DBG("\tZ=%5d", reg_value);

	reg = LIS2DE12_FIFO_CTRL_REG;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	uint8_t fm = ((reg_value) & (1 << (7 - 0)) ? true : false) * 2 +
		     ((reg_value) & (1 << (7 - 1)) ? true : false);
	uint8_t fth = ((reg_value) & (1 << (7 - 3)) ? true : false) * 16 +
		      ((reg_value) & (1 << (7 - 4)) ? true : false) * 8 +
		      ((reg_value) & (1 << (7 - 5)) ? true : false) * 4 +
		      ((reg_value) & (1 << (7 - 6)) ? true : false) * 2 +
		      ((reg_value) & (1 << (7 - 7)) ? true : false);
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_FIFO_CTRL_REG, "LIS2DE12_FIFO_CTRL_REG",
		reg_value);
	LOG_DBG("\tFifo Mode:%s", fm == 0   ? "bypass"
		: fm == 1 ? "Fifo"
		: fm == 2 ? "Stream"
		: fm == 3 ? "Stream to Fifo"
					    : "none");
	LOG_DBG("\tTriggerSelection=%s",
		((reg_value >> 2)) ? "trigger event allows triggering signal on INT2"
				   : "trigger event allows triggering signal on INT1");
	LOG_DBG("\tFTH=%d", fth);

	reg = LIS2DE12_FIFO_SRC_REG;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	uint8_t FSS = reg_value & 0x0F;
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_FIFO_SRC_REG, "LIS2DE12_FIFO_SRC_REG",
		reg_value);
	LOG_DBG("\tWTM=%s", ((reg_value >> 0)) ? "watermark level exceeded" : "normal");
	LOG_DBG("\tOver_Fifo=%s", ((reg_value >> 1)) ? "overrun" : "no overrun");
	LOG_DBG("\tFifo Empty=%s", ((reg_value >> 2)) ? "empty" : "samples in fifo");
	LOG_DBG("\tSamples in Fifo=%d", FSS);

	reg = LIS2DE12_INT1_CFG;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_INT1_CFG, "LIS2DE12_INT1_CFG", reg_value);
	reg = LIS2DE12_INT1_SRC;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_INT1_SRC, "LIS2DE12_INT1_SRC", reg_value);
	reg = LIS2DE12_INT1_THS;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_INT1_THS, "LIS2DE12_INT1_THS", reg_value);
	reg = LIS2DE12_INT1_DURATION;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_INT1_DURATION, "LIS2DE12_INT1_DURATION",
		reg_value);
	reg = LIS2DE12_INT2_CFG;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_INT2_CFG, "LIS2DE12_INT2_CFG", reg_value);
	reg = LIS2DE12_INT2_SRC;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_INT2_SRC, "LIS2DE12_INT2_SRC", reg_value);
	reg = LIS2DE12_INT2_THS;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_INT2_THS, "LIS2DE12_INT2_THS", reg_value);
	reg = LIS2DE12_INT2_DURATION;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_INT2_DURATION, "LIS2DE12_INT2_DURATION",
		reg_value);
	reg = LIS2DE12_CLICK_CFG;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_CLICK_CFG, "LIS2DE12_CLICK_CFG", reg_value);
	reg = LIS2DE12_CLICK_SRC;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_CLICK_SRC, "LIS2DE12_CLICK_SRC", reg_value);
	LOG_DBG("\tStatic=%s", ((reg_value) & (1 << (7 - 0)) ? true : false) ? "ERROR" : "Normal");
	LOG_DBG("\tInterrupt Active=%s", ((reg_value) & (1 << (7 - 1)) ? true : false)
						 ? "one or more interrupts"
						 : "no interrupt generated");
	LOG_DBG("\tDouble Click=%s",
		((reg_value) & (1 << (7 - 2)) ? true : false) ? "enabled" : "disabled");
	LOG_DBG("\tSingle Click=%s",
		((reg_value) & (1 << (7 - 3)) ? true : false) ? "enabled" : "disabled");
	LOG_DBG("\tSign=%s",
		((reg_value) & (1 << (7 - 4)) ? true : false) ? "negative" : "positive");
	LOG_DBG("\tZClick=%s",
		((reg_value) & (1 << (7 - 5)) ? true : false) ? "interrupt" : "no interrupt");
	LOG_DBG("\tYClick=%s",
		((reg_value) & (1 << (7 - 6)) ? true : false) ? "interrupt" : "no interrupt");
	LOG_DBG("\tXClick=%s",
		((reg_value) & (1 << (7 - 7)) ? true : false) ? "interrupt" : "no interrupt");

	reg = LIS2DE12_CLICK_THS;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	uint8_t click_ths = reg_value & 0x7F;
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_CLICK_THS, "LIS2DE12_CLICK_THS", reg_value);
	LOG_DBG("\tLIR_Click=%s", ((reg_value) & (1 << (7 - 0)) ? true : false)
			? "Int HIGH for time window"
					  : "Int HIGH until CLICK_SRC (39h) is read");
	LOG_DBG("\tTHS=%d", click_ths);

	reg = LIS2DE12_TIME_LIMIT;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_TIME_LIMIT, "LIS2DE12_TIME_LIMIT", reg_value);
	LOG_DBG("\tClick time limit=%d", reg_value);

	reg = LIS2DE12_TIME_LATENCY;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_TIME_LATENCY, "LIS2DE12_TIME_LATENCY",
		reg_value);
	LOG_DBG("\tClick time latency=%d", reg_value);

	reg = LIS2DE12_TIME_WINDOW;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_TIME_WINDOW, "LIS2DE12_TIME_WINDOW", reg_value);
	LOG_DBG("\tTime window=%d", reg_value);

	reg = LIS2DE12_ACT_THS;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_TIME_WINDOW, "LIS2DE12_ACT_THS", reg_value);
	LOG_DBG("\tStatic=%s", ((reg_value) & (1 << (7 - 0)) ? true : false) ? "ERROR" : "Normal");
	LOG_DBG("\tAct=%d", reg_value);

	reg = LIS2DE12_ACT_DUR;
	if (lis2de12_read_reg(ctx, reg, &reg_value, 1) < 0) {
		LOG_ERR("Failed to Register 0x%02X", reg);
		return -EIO;
	}
	double actd_lsb = (8.0 * (double)reg_value + 1.0) / odr;
	LOG_DBG("REG:0x%02X, %s : 0x%02X", LIS2DE12_ACT_DUR, "LIS2DE12_ACT_DUR", reg_value);
	LOG_DBG("\tActd=%f", actd_lsb);

	return 0;
}
#endif

static int lis2de12_freq_to_odr_val(const struct device *dev, uint16_t freq)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(lis2de12_odr_map); i++) {
		if (freq <= lis2de12_odr_map[i]) {
			return i;
		}
	}

	return -EINVAL;
}

typedef struct {
	uint16_t fs;
	uint32_t gain; /* Accel sensor sensitivity in ug/LSB */
} fs_map;

static const fs_map lis2de12_accel_fs_map[] = {
				{2, 15600},
				{4, 31200},
				{8, 62500},
				{16, 187500},
			};

static int lis2de12_accel_range_to_fs_val(int32_t range)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(lis2de12_accel_fs_map); i++) {
		if (range == lis2de12_accel_fs_map[i].fs) {
			return i;
		}
	}

	return -EINVAL;
}

static int lis2de12_accel_set_fs_raw(const struct device *dev, uint8_t fs)
{
	const struct lis2de12_config *cfg = dev->config;
	stmdev_ctx_t *ctx = (stmdev_ctx_t *)&cfg->ctx;
	struct lis2de12_data *data = dev->data;

	if (lis2de12_full_scale_set(ctx, fs) < 0) {
		return -EIO;
	}

	data->accel_fs = fs;

	return 0;
}

static int lis2de12_accel_set_odr_raw(const struct device *dev, uint8_t odr)
{
	const struct lis2de12_config *cfg = dev->config;
	stmdev_ctx_t *ctx = (stmdev_ctx_t *)&cfg->ctx;
	struct lis2de12_data *data = dev->data;

	if (lis2de12_data_rate_set(ctx, odr) < 0) {
		return -EIO;
	}

	data->accel_freq = odr;

	return 0;
}

static int lis2de12_accel_odr_set(const struct device *dev, uint16_t freq)
{
	int odr;

	odr = lis2de12_freq_to_odr_val(dev, freq);
	if (odr < 0) {
		return odr;
	}

	if (lis2de12_accel_set_odr_raw(dev, odr) < 0) {
		LOG_ERR("failed to set accelerometer sampling rate");
		return -EIO;
	}

	return 0;
}

static int lis2de12_accel_range_set(const struct device *dev, int32_t range)
{
	int fs;
	struct lis2de12_data *data = dev->data;

	fs = lis2de12_accel_range_to_fs_val(range);
	if (fs < 0) {
		return fs;
	}

	if (lis2de12_accel_set_fs_raw(dev, fs) < 0) {
		LOG_ERR("failed to set accelerometer full-scale");
		return -EIO;
	}

	data->acc_gain = lis2de12_accel_fs_map[fs].gain;
	return 0;
}

static int lis2de12_accel_config(const struct device *dev,
				 enum sensor_channel chan,
				 enum sensor_attribute attr,
				 const struct sensor_value *val)
{
	switch (attr) {
	case SENSOR_ATTR_FULL_SCALE:
		return lis2de12_accel_range_set(dev, sensor_ms2_to_g(val));
	case SENSOR_ATTR_SAMPLING_FREQUENCY:
		return lis2de12_accel_odr_set(dev, val->val1);
	default:
		LOG_WRN("Accel attribute %d not supported.", attr);
		return -ENOTSUP;
	}
}

static int lis2de12_attr_set(const struct device *dev,
			     enum sensor_channel chan,
			     enum sensor_attribute attr,
			     const struct sensor_value *val)
{
	switch (chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
		return lis2de12_accel_config(dev, chan, attr, val);
	default:
		LOG_WRN("attribute %d not supported on this channel.", chan);
		return -ENOTSUP;
	}
}

static int lis2de12_sample_fetch_accel(const struct device *dev)
{
	const struct lis2de12_config *cfg = dev->config;
	stmdev_ctx_t *ctx = (stmdev_ctx_t *)&cfg->ctx;
	struct lis2de12_data *data = dev->data;

	if (lis2de12_acceleration_raw_get(ctx, data->acc) < 0) {
		LOG_ERR("Failed to read sample");
		return -EIO;
	}

	return 0;
}

#if defined(CONFIG_LIS2DE12_ENABLE_TEMP)
static int lis2de12_sample_fetch_temp(const struct device *dev)
{
	const struct lis2de12_config *cfg = dev->config;
	stmdev_ctx_t *ctx = (stmdev_ctx_t *)&cfg->ctx;
	struct lis2de12_data *data = dev->data;

	if (lis2de12_temperature_raw_get(ctx, &data->temp_sample) < 0) {
		LOG_DBG("Failed to read sample");
		return -EIO;
	}

	return 0;
}
#endif

static int lis2de12_sample_fetch(const struct device *dev,
				 enum sensor_channel chan)
{
	switch (chan) {
	case SENSOR_CHAN_ACCEL_XYZ:
		lis2de12_sample_fetch_accel(dev);
		break;
#if defined(CONFIG_LIS2DE12_ENABLE_TEMP)
	case SENSOR_CHAN_DIE_TEMP:
		lis2de12_sample_fetch_temp(dev);
		break;
#endif
	case SENSOR_CHAN_ALL:
		lis2de12_sample_fetch_accel(dev);
#if defined(CONFIG_LIS2DE12_ENABLE_TEMP)
		lis2de12_sample_fetch_temp(dev);
#endif
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static inline void lis2de12_accel_convert(struct sensor_value *val, int raw_val,
					  uint32_t sensitivity)
{
	int64_t dval;

	/* Sensitivity is exposed in ug/LSB */
	/* Convert to m/s^2 */
	dval = (int64_t)(raw_val / 256) * sensitivity * SENSOR_G_DOUBLE;
	val->val1 = (int32_t)(dval / 1000000);
	val->val2 = (int32_t)(dval % 1000000);

}

static inline int lis2de12_accel_get_channel(enum sensor_channel chan,
					     struct sensor_value *val,
					     struct lis2de12_data *data,
					     uint32_t sensitivity)
{
	uint8_t i;

	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
		lis2de12_accel_convert(val, data->acc[0], sensitivity);
		break;
	case SENSOR_CHAN_ACCEL_Y:
		lis2de12_accel_convert(val, data->acc[1], sensitivity);
		break;
	case SENSOR_CHAN_ACCEL_Z:
		lis2de12_accel_convert(val, data->acc[2], sensitivity);
		break;
	case SENSOR_CHAN_ACCEL_XYZ:
		for (i = 0; i < 3; i++) {
			lis2de12_accel_convert(val++, data->acc[i], sensitivity);
		}
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int lis2de12_accel_channel_get(enum sensor_channel chan,
				      struct sensor_value *val,
				      struct lis2de12_data *data)
{
	return lis2de12_accel_get_channel(chan, val, data, data->acc_gain);
}

#if defined(CONFIG_LIS2DE12_ENABLE_TEMP)
static void lis2de12_temp_channel_get(struct sensor_value *val, struct lis2de12_data *data)
{
	int64_t micro_c;

	/* convert units to micro Celsius. Raw temperature samples are
	 * expressed in 256 LSB/deg_C units. And LSB output is 0 at 25 C.
	 */
	micro_c = ((int64_t)data->temp_sample * 1000000) / 256;

	val->val1 = micro_c / 1000000 + 25;
	val->val2 = micro_c % 1000000;
}
#endif

static int lis2de12_channel_get(const struct device *dev,
				enum sensor_channel chan,
				struct sensor_value *val)
{
	struct lis2de12_data *data = dev->data;

	switch (chan) {
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
	case SENSOR_CHAN_ACCEL_XYZ:
		lis2de12_accel_channel_get(chan, val, data);
		break;
#if defined(CONFIG_LIS2DE12_ENABLE_TEMP)
	case SENSOR_CHAN_DIE_TEMP:
		lis2de12_temp_channel_get(val, data);
		break;
#endif
	default:
		return -ENOTSUP;
	}

	return 0;
}

static const struct sensor_driver_api lis2de12_driver_api = {
	.attr_set = lis2de12_attr_set,
#if CONFIG_LIS2DE12_TRIGGER
	.trigger_set = lis2de12_trigger_set,
#endif
	.sample_fetch = lis2de12_sample_fetch,
	.channel_get = lis2de12_channel_get,
};

static int lis2de12_init_chip(const struct device *dev)
{
	const struct lis2de12_config *cfg = dev->config;
	stmdev_ctx_t *ctx = (stmdev_ctx_t *)&cfg->ctx;
	struct lis2de12_data *lis2de12 = dev->data;
	uint8_t chip_id;
	uint8_t odr, fs;

	if (lis2de12_device_id_get(ctx, &chip_id) < 0) {
		LOG_ERR("Failed reading chip id");
		return -EIO;
	}

	if (chip_id != LIS2DE12_ID) {
		LOG_ERR("Invalid chip id 0x%x", chip_id);
		return -EIO;
	}

	LOG_INF("chip id 0x%x", chip_id);

	if (lis2de12_block_data_update_set(ctx, 1) < 0) {
		LOG_ERR("failed to set BDU (block_data_update)");
		return -EIO;
	}
	if (lis2de12_fifo_set(ctx, 1) < 0) {
		LOG_ERR("failed to enable FIFO");
		return -EIO;
	}
	if (lis2de12_fifo_mode_set(ctx, LIS2DE12_BYPASS_MODE) < 0) {
		LOG_ERR("failed to set fifo mode");
		return -EIO;
	}
	if (lis2de12_fifo_mode_set(ctx, LIS2DE12_DYNAMIC_STREAM_MODE) < 0) {
		LOG_ERR("failed to set fifo mode");
		return -EIO;
	}
	if (lis2de12_fifo_watermark_set(ctx, 0) < 0) {
		LOG_ERR("failed to set watermark");
		return -EIO;
	}
	if (lis2de12_self_test_set(ctx, LIS2DE12_ST_DISABLE) < 0) {
		LOG_ERR("failed to set watermark");
		return -EIO;
	}
	if (lis2de12_fifo_set(ctx, 1) < 0) {
		LOG_ERR("failed to enable FIFO");
		return -EIO;
	}
	if (lis2de12_fifo_mode_set(ctx, LIS2DE12_BYPASS_MODE) < 0) {
		LOG_ERR("failed to set fifo mode");
		return -EIO;
	}
	if (lis2de12_fifo_mode_set(ctx, LIS2DE12_DYNAMIC_STREAM_MODE) < 0) {
		LOG_ERR("failed to set fifo mode");
		return -EIO;
	}
	if (lis2de12_fifo_watermark_set(ctx, 0) < 0) {
		LOG_ERR("failed to set watermark");
		return -EIO;
	}
	if (lis2de12_self_test_set(ctx, LIS2DE12_ST_DISABLE) < 0) {
		LOG_ERR("failed to set self test");
		return -EIO;
	}

	/* set FS from DT */
	fs = cfg->accel_range;
	LOG_DBG("accel range is %d", fs);
	if (lis2de12_accel_set_fs_raw(dev, fs) < 0) {
		LOG_ERR("failed to set accelerometer range %d", fs);
		return -EIO;
	}
	lis2de12->acc_gain = lis2de12_accel_fs_map[fs].gain;

	/* set odr from DT (the only way to go in high performance) */
	odr = cfg->accel_odr;
	LOG_DBG("accel odr is %d", odr);
	if (lis2de12_accel_set_odr_raw(dev, odr) < 0) {
		LOG_ERR("failed to set accelerometer odr %d", odr);
		return -EIO;
	}

#if defined(CONFIG_LIS2DE12_ENABLE_TEMP)
	lis2de12_temperature_meas_set(ctx, LIS2DE12_TEMP_ENABLE);
#endif
#ifdef CONFIG_SENSOR_LOG_LEVEL_DBG
	lis2de12_print_registers(dev);
#endif
	return 0;
}

static int lis2de12_init(const struct device *dev)
{
#ifdef CONFIG_LIS2DE12_TRIGGER
	const struct lis2de12_config *cfg = dev->config;
#endif
	struct lis2de12_data *data = dev->data;

	LOG_INF("Initialize device %s", dev->name);
	data->dev = dev;

	if (lis2de12_init_chip(dev) < 0) {
		LOG_ERR("failed to initialize chip");
		return -EIO;
	}

#ifdef CONFIG_LIS2DE12_TRIGGER
	if (cfg->trig_enabled) {
		if (lis2de12_init_interrupt(dev) < 0) {
			LOG_ERR("Failed to initialize interrupt.");
			return -EIO;
		}
	}
#endif

	return 0;
}

/*
 * Device creation macro, shared by LIS2DE12_DEFINE_SPI() and
 * LIS2DE12_DEFINE_I2C().
 */

#define LIS2DE12_DEVICE_INIT(inst)					\
	SENSOR_DEVICE_DT_INST_DEFINE(inst,				\
			    lis2de12_init,				\
			    NULL,					\
			    &lis2de12_data_##inst,			\
			    &lis2de12_config_##inst,			\
			    POST_KERNEL,				\
			    CONFIG_SENSOR_INIT_PRIORITY,		\
			    &lis2de12_driver_api);

/*
 * Instantiation macros used when a device is on a SPI bus.
 */

#ifdef CONFIG_LIS2DE12_TRIGGER
#define LIS2DE12_CFG_IRQ(inst)						\
	.trig_enabled = true,						\
	.int1_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int1_gpios, { 0 }), \
	.int2_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, int2_gpios, { 0 }), \
	.drdy_pulsed = DT_INST_PROP(inst, drdy_pulsed)
#else
#define LIS2DE12_CFG_IRQ(inst)
#endif /* CONFIG_LIS2DE12_TRIGGER */

#define LIS2DE12_SPI_OP  (SPI_WORD_SET(8) |				\
			 SPI_OP_MODE_MASTER |				\
			 SPI_MODE_CPOL |				\
			 SPI_MODE_CPHA)					\

#define LIS2DE12_CONFIG_COMMON(inst)					\
	.accel_odr = DT_INST_PROP(inst, accel_odr),			\
	.accel_range = DT_INST_PROP(inst, accel_range),			\
	IF_ENABLED(UTIL_OR(DT_INST_NODE_HAS_PROP(inst, int1_gpios),	\
			   DT_INST_NODE_HAS_PROP(inst, int2_gpios)),	\
		   (LIS2DE12_CFG_IRQ(inst)))

/*
 * Instantiation macros used when a device is on a SPI bus.
 */

#define LIS2DE12_CONFIG_SPI(inst)						\
	{									\
		STMEMSC_CTX_SPI(&lis2de12_config_##inst.stmemsc_cfg),		\
		.stmemsc_cfg = {						\
			.spi = SPI_DT_SPEC_INST_GET(inst, LIS2DE12_SPI_OP, 0),	\
		},								\
		LIS2DE12_CONFIG_COMMON(inst)					\
	}

/*
 * Instantiation macros used when a device is on an I2C bus.
 */

#define LIS2DE12_CONFIG_I2C(inst)						\
	{									\
		STMEMSC_CTX_I2C_INCR(&lis2de12_config_##inst.stmemsc_cfg),	\
		.stmemsc_cfg = {						\
			.i2c = I2C_DT_SPEC_INST_GET(inst),			\
		},								\
		LIS2DE12_CONFIG_COMMON(inst)					\
	}

/*
 * Main instantiation macro. Use of COND_CODE_1() selects the right
 * bus-specific macro at preprocessor time.
 */

#define LIS2DE12_DEFINE(inst)						\
	static struct lis2de12_data lis2de12_data_##inst;			\
	static const struct lis2de12_config lis2de12_config_##inst =	\
		COND_CODE_1(DT_INST_ON_BUS(inst, spi),			\
			(LIS2DE12_CONFIG_SPI(inst)),			\
			(LIS2DE12_CONFIG_I2C(inst)));			\
	LIS2DE12_DEVICE_INIT(inst)

DT_INST_FOREACH_STATUS_OKAY(LIS2DE12_DEFINE)

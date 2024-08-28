/*
 * Copyright (c) 2024 Orgatex GmbH
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_bq35100

#include <zephyr/kernel.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/fuel_gauge/bq35100_user.h>
#include "bq35100.h"

LOG_MODULE_REGISTER(bq35100, CONFIG_FUEL_GAUGE_LOG_LEVEL);

#define BQ35100_DEVICE_TYPE 0x100

#define BQ35100_MAC_DATA_LEN     32
#define BQ35100_MAC_OVERHEAD_LEN 4 /* 2 cmd bytes, 1 length byte, 1 checksum byte */
#define BQ35100_MAC_COMPLETE_LEN (BQ35100_MAC_DATA_LEN + BQ35100_MAC_OVERHEAD_LEN)

#define BQ35100_CNTL_DATA_LEN 2

#define BQ35100_FLASH_WRITE_DELAY 100
#define BQ35100_NEW_BATTERY_DELAY 500

typedef enum {
	SECURITY_UNKNOWN = 0x00,
	SECURITY_FULL_ACCESS = 0x01,
	SECURITY_UNSEALED = 0x02,
	SECURITY_SEALED = 0x03
} bq35100_security_t;

static bq35100_security_t g_security_mode = SECURITY_UNKNOWN;

/* Function prototypes */
static int bq35100_set_security_mode(const struct device *dev, bq35100_security_t new_security);
static int bq35100_get_status(const struct device *dev, uint16_t *status);

static uint8_t bq35100_compute_checksum(const uint8_t *data, size_t length)
{
	uint8_t checksum = 0;
	uint8_t x = 0;

	if (data) {
		for (x = 1; x <= length; x++) {
			checksum += *data;
			data++;
		}

		checksum = 0xFF - checksum;
	}

	return checksum;
}

static int bq35100_write(const struct device *dev, const uint8_t *data, size_t len)
{
	int result = 0;
	/* Pointer to the device's configuration */
	const struct bq35100_config *cfg = dev->config;
	LOG_HEXDUMP_DBG(data, len, "dev write");

	/* Write the buffer to the device over I2C */
	result = i2c_write_dt(&cfg->i2c, data, len);
	if (result) {
		LOG_ERR("Failed to write I2C-data, error: %d", result);
	}
	return result;
}

static int bq35100_read(const struct device *dev, uint8_t *write_data, size_t write_len,
			uint8_t *read_data, size_t read_len)
{
	/* Pointer to the device's configuration */
	const struct bq35100_config *cfg = dev->config;

	/* Write-Read the buffer of the device over I2C */

	int result = i2c_write_dt(&cfg->i2c, write_data, write_len);
	if (result) {
		LOG_ERR("Unable to write data for I2C-read, error: %d", result);
		return result;
	}

	result = i2c_read_dt(&cfg->i2c, read_data, read_len);
	if (result) {
		LOG_ERR("Failed to read I2C-data, error: %d", result);
	} else {
		LOG_HEXDUMP_DBG(read_data, read_len, "dev read");
	}

	return result;
}

static int bq35100_send_data(const struct device *dev, uint8_t address, uint8_t *data, size_t len)
{
	uint8_t buffer[BQ35100_CNTL_DATA_LEN + 1];

	buffer[0] = address;
	memcpy(buffer + 1, data, len);

	return bq35100_write(dev, buffer, ARRAY_SIZE(buffer));
}

static int bq35100_get_data(const struct device *dev, uint8_t address, uint8_t *data, size_t len)
{
	uint8_t write_buffer;
	write_buffer = address;

	return bq35100_read(dev, &write_buffer, 1, data, len);
}

static int bq35100_send_cntl(const struct device *dev, uint16_t cntl_address)
{
	uint8_t buffer[2];

	sys_put_le16(cntl_address, buffer);

	return bq35100_send_data(dev, BQ35100_REG_CONTROL_STATUS, buffer, ARRAY_SIZE(buffer));
}

static int bq35100_get_cntl(const struct device *dev, uint16_t cntl_address, uint16_t *data)
{
	int result = 0;
	uint8_t buffer[2];

	if (data == NULL) {
		LOG_ERR("CNTL return buffer invalid");
		return -EINVAL;
	}

	sys_put_le16(cntl_address, buffer);

	result = bq35100_send_data(dev, BQ35100_REG_CONTROL_STATUS, buffer, ARRAY_SIZE(buffer));
	if (result) {
		return result;
	}

	result = bq35100_get_data(dev, BQ35100_REG_CONTROL_STATUS, buffer, 2);
	if (result) {
		return result;
	}

	result = bq35100_get_data(dev, BQ35100_REG_MAC_DATA, buffer, ARRAY_SIZE(buffer));
	if (result) {
		return result;
	}

	*data = sys_get_le16(buffer);

	return result;
}

static int bq35100_write_extended_data(const struct device *dev, const uint16_t address,
				       const uint8_t *data, size_t len)
{
	int result = 0;
	uint16_t answer;
	char buffer[BQ35100_MAC_DATA_LEN + 3]; /* Max data len + header */

	bq35100_security_t prev_security_mode = g_security_mode;

	if (g_security_mode == SECURITY_UNKNOWN) {
		LOG_ERR("Security mode unknown");
		return false;
	}

	if (address < 0x4000 || address > 0x43FF || len < 1 || len > 32 || !data) {
		LOG_ERR("Invalid input data");
		return false;
	}

	if (g_security_mode == SECURITY_SEALED) {
		result = bq35100_set_security_mode(dev, SECURITY_UNSEALED);
		if (result) {
			LOG_ERR("Unable to set SECURITY_UNSEALED");
			return -EINVAL;
		}
	}

	LOG_DBG("Preparing to write %u byte(s) to address 0x%04X", len, address);
	LOG_HEXDUMP_DBG(data, len, "Payload");

	buffer[0] = BQ35100_REG_MAC;
	sys_put_le16(address, &buffer[1]);

	memcpy(buffer + 3, data, len);

	result = bq35100_write(dev, buffer, 3 + len);
	if (result) {
		LOG_ERR("Unable to write to ManufacturerAccessControl");
		goto END;
	}

	k_sleep(K_MSEC(BQ35100_FLASH_WRITE_DELAY));

	/* Compute the checksum and write it to BQ35100_REG_MAC_DATA_SUM (0x60)
	 * and with autoincrement write 4 + len to BQ35100_REG_MAC_DATA_LEN (0x61)
	 */
	buffer[0] = BQ35100_REG_MAC_DATA_SUM;
	buffer[1] = bq35100_compute_checksum(buffer + 1, len + 2);
	buffer[2] = len + 4;

	result = bq35100_write(dev, buffer, 3);
	if (result) {
		LOG_ERR("Unable to write to BQ35100_REG_MAC_DATA_SUM");
		goto END;
	}

	k_sleep(K_MSEC(BQ35100_FLASH_WRITE_DELAY));

	result = bq35100_get_status(dev, &answer);
	if (result) {
		goto END;
	}

	if (BIT(15) & answer) {
		LOG_ERR("Write failed");
		goto END;
	}

	LOG_DBG("Write successful");

END:

	if (prev_security_mode != g_security_mode) {
		result = bq35100_set_security_mode(dev, prev_security_mode);
	}

	return result;
}

static int bq35100_read_extended_data(const struct device *dev, const uint16_t address,
				      uint8_t *data, size_t len)
{
	size_t length_read;
	uint8_t buffer[BQ35100_MAC_COMPLETE_LEN];
	uint8_t write_buffer;
	int result = 0;

	bq35100_security_t prev_security_mode = g_security_mode;

	if (g_security_mode == SECURITY_UNKNOWN) {
		LOG_ERR("Security mode unknown");
		return -EINVAL;
	}

	if (address < 0x4000 || address > 0x43FF || !data) {
		LOG_ERR("Invalid input data");
		return -EINVAL;
	}

	if (g_security_mode == SECURITY_SEALED) {
		result = bq35100_set_security_mode(dev, SECURITY_UNSEALED);
		if (result) {
			LOG_ERR("Unable to set SECURITY_UNSEALED");
			return -EINVAL;
		}
	}

	LOG_DBG("Preparing to read %u byte(s) from address 0x%04X", len, address);

	sys_put_le16(address, buffer);
	result = bq35100_send_data(dev, BQ35100_REG_MAC, buffer, 2);
	if (result) {
		LOG_ERR("Unable to write address to ManufacturerAccessControl");
		result = -EINVAL;
		goto END;
	}

	write_buffer = BQ35100_REG_MAC;

	result = bq35100_read(dev, &write_buffer, 1, buffer, ARRAY_SIZE(buffer));

	/* Check that the address matches */
	if (buffer[0] != (char)address || buffer[1] != (char)(address >> 8)) {
		LOG_ERR("Address didn't match (expected 0x%04X, received 0x%02X%02X)", address,
			buffer[1], buffer[0]);
		result = -EINVAL;
		goto END;
	}

	/* Check that the checksum matches (-2 on BQ35100_REG_MAC_DATA_LEN as it includes
	 * BQ35100_REG_MAC_DATA_SUM and itself)
	 */
	if (buffer[34] != bq35100_compute_checksum(buffer, buffer[35] - 2)) {
		LOG_ERR("Checksum didn't match (0x%02X expected)", buffer[34]);
		result = -EINVAL;
		goto END;
	}

	/* All is good */
	length_read =
		buffer[35] - 4; /* -4 rather than -2 to remove the two bytes of address as well */

	if (length_read > len) {
		length_read = len;
	}

	memcpy(data, buffer + 2, length_read);

	LOG_HEXDUMP_DBG(buffer, length_read, "data read");

END:

	if (prev_security_mode != g_security_mode) { /* in case we changed the mode */
		result = bq35100_set_security_mode(dev, prev_security_mode);
	}

	return result;
}

static int bq35100_get_status(const struct device *dev, uint16_t *status)
{
	int ret;
	uint8_t data[2];

	if (dev == NULL || status == NULL) {
		LOG_ERR("Invalid parameters");
		return -EINVAL;
	}

	LOG_DBG("Reading device-status");

	ret = bq35100_get_data(dev, BQ35100_REG_CONTROL_STATUS, data, sizeof(data));
	if (ret != 0) {
		LOG_ERR("Failed to read device status");
		return ret;
	}

	*status = sys_le16_to_cpu(UNALIGNED_GET((uint16_t *)data));

	return 0;
}

static int bq35100_wait_for_status(const struct device *dev, uint16_t expected, uint16_t mask,
				   k_timeout_t millis)
{
	uint16_t answer;

	for (int i = 0; i < CONFIG_BQ35100_MAX_RETRIES; i++) {
		if (!bq35100_get_status(dev, &answer)) {
			return false;
		}

		if ((answer & mask) == expected) {
			LOG_DBG("Status match");
			return true;

		} else {
			LOG_WRN("Status not yet in requested state read: %04X expected: %04X",
				answer, expected);
			k_sleep(millis);
		}
	}

	LOG_ERR("Status not in requested state, read: %04X expected: %04X", answer, expected);

	return -EINVAL;
}

static int bq35100_get_security_mode(const struct device *dev, bq35100_security_t *current_security)
{
	int result;
	uint16_t buffer;
	uint8_t extracted_security;

	LOG_DBG("Reading security-mode");

	result = bq35100_get_status(dev, &buffer);
	result = bq35100_get_status(dev, &buffer);
	if (result) {
		return SECURITY_UNKNOWN;
	}

	extracted_security = FIELD_GET(BIT_MASK(2) << 13, buffer);

	switch (extracted_security) {
	case SECURITY_FULL_ACCESS:
		LOG_DBG("Device is in FULL ACCESS mode");
		break;

	case SECURITY_UNSEALED:
		LOG_DBG("Device is in UNSEALED mode");
		break;

	case SECURITY_SEALED:
		LOG_DBG("Device is in SEALED mode");
		break;

	default:
		LOG_ERR("Invalid device mode");
		return SECURITY_UNKNOWN;
	}

	*current_security = (bq35100_security_t)extracted_security;

	return 0;
}

static int bq35100_set_security_mode(const struct device *dev, bq35100_security_t new_security)
{
	int result = -EINVAL;
	char buffer[4];

	if (new_security == g_security_mode) {
		return 0; /* We are already in this mode */
	}

	if (new_security == SECURITY_UNKNOWN) {
		LOG_ERR("Invalid access mode");
		return -EINVAL;
	}

	// For reasons that aren't clear, the BQ35100 sometimes refuses
	// to change security mode if a previous security mode change
	// happend only a few seconds ago, hence the retry here
	for (int i = 0; (i < CONFIG_BQ35100_MAX_RETRIES) && result != 0; i++) {
		buffer[0] = BQ35100_REG_MAC;

		switch (new_security) {
		case SECURITY_SEALED: {
			LOG_DBG("Setting security to SEALED");
			result = bq35100_send_cntl(dev, BQ35100_MAC_CMD_SEALED);
			if (result) {
				LOG_ERR("Unable to set SECURITY_SEALED");
				return result;
			}
			break;
		}

		case SECURITY_FULL_ACCESS: {
			/* Unseal first if in Sealed mode */
			if (g_security_mode == SECURITY_SEALED) {
				result = bq35100_set_security_mode(dev, SECURITY_UNSEALED);
				if (result) {
					LOG_ERR("Unable to set SECURITY_UNSEALED");
					return result;
				}
			}

			result = bq35100_read_extended_data(dev, BQ35100_FLASH_FULL_ACCESS_CODES,
							    buffer, ARRAY_SIZE(buffer));
			if (result) {
				LOG_ERR("Could not get full access codes");
				return result;
			}

			uint32_t full_access_codes = (buffer[0] << 24) + (buffer[1] << 16) +
						     (buffer[2] << 8) + buffer[3];

			LOG_DBG("Setting security to FULL ACCESS");

			/* Send the full access code with endianness conversion in TWO writes */
			buffer[2] = (full_access_codes >> 24) & 0xFF;
			buffer[1] = (full_access_codes >> 16) & 0xFF;
			result = bq35100_write(dev, buffer, 3);
			if (result) {
				LOG_ERR("Unable to send first part of full access key");
				return result;
			}

			buffer[2] = (full_access_codes >> 8) & 0xFF;
			buffer[1] = full_access_codes & 0xFF;
			result = bq35100_write(dev, buffer, 3);
			if (result) {
				LOG_ERR("Unable to send first part of full access key");
				return result;
			}
			break;
		}

		case SECURITY_UNSEALED: {
			/* Seal first if in Full Access mode */
			if (g_security_mode == SECURITY_FULL_ACCESS) {
				result = bq35100_set_security_mode(dev, SECURITY_SEALED);
				if (result) {
					LOG_ERR("Unable to set SECURITY_SEALED");
					return result;
				}
			}

			LOG_DBG("Setting security to UNSEALED");

			buffer[0] = BQ35100_REG_CONTROL_STATUS;
			/* Send the unsealed code with endianness conversion in TWO writes */
			buffer[2] = (BQ35100_DEFAULT_SEAL_CODES >> 24) & 0xFF;
			buffer[1] = (BQ35100_DEFAULT_SEAL_CODES >> 16) & 0xFF;
			result = bq35100_write(dev, buffer, 3);
			if (result) {
				LOG_ERR("Unable to send first part of unsealed key");
				return result;
			}

			k_sleep(K_MSEC(BQ35100_FLASH_WRITE_DELAY));

			/* buffer[0] = BQ35100_REG_MAC; */
			buffer[2] = (BQ35100_DEFAULT_SEAL_CODES >> 8) & 0xFF;
			buffer[1] = BQ35100_DEFAULT_SEAL_CODES & 0xFF;
			result = bq35100_write(dev, buffer, 3);
			if (result) {
				LOG_ERR("Unable to send first part of unsealed key");
				return result;
			}
			break;
		}

		case SECURITY_UNKNOWN:
		default: {
			LOG_ERR("Unkown security mode");

			break;
		}
		}

		result = bq35100_get_security_mode(dev, &g_security_mode);
		if (result) {
			LOG_ERR("Unable to verifiy security mode");
			return result;
		}

		if (g_security_mode == new_security) {
			LOG_DBG("Security mode set");
		} else {
			LOG_ERR("Security mode set failed (wanted 0x%02X, got 0x%02X)",
				new_security, g_security_mode);
			return -EIO;
		}
	}
	return result;
}

static int bq35100_get_device_type(const struct device *dev, uint16_t *type)
{
	if (type == NULL) {
		LOG_ERR("Inavlid device-type buffer");
		return -EINVAL;
	}

	LOG_DBG("Reading device-type");

	int result = bq35100_get_cntl(dev, BQ35100_MAC_CMD_DEVICETYPE, type);
	if (result) {
		LOG_ERR("Unable to get control status");
		return result;
	}

	return 0;
}

static int bq35100_set_design_capacity(const struct device *dev, const uint16_t new_design_capacity)
{
	uint8_t buffer[2];

	sys_put_be16(new_design_capacity, buffer);

	return bq35100_write_extended_data(dev, BQ35100_FLASH_CMD_SET_NEW_CAPACITY, buffer,
					   ARRAY_SIZE(buffer));
}

static int bq35100_start_gauge(const struct device *dev)
{
	int result = bq35100_send_cntl(dev, BQ35100_MAC_CMD_GAUGE_START);

	if (result) {
		LOG_ERR("Error enabling gauge: %d", result);
		return result;
	}

	result = bq35100_wait_for_status(dev, BIT(0), BIT(0), K_MSEC(500));

	if (result) {
		LOG_ERR("Error enabling gauge: %d", result);
		return result;
	}

	return 0;
}

static int bq35100_stop_gauge(const struct device *dev)
{
	int result = bq35100_send_cntl(dev, BQ35100_MAC_CMD_GAUGE_STOP);

	if (result) {
		LOG_ERR("Error disabling gauge: %d", result);
		return result;
	}

	result = bq35100_wait_for_status(dev, 0, BIT(0), K_MSEC(500));

	if (result) {
		LOG_ERR("Error disabling gauge: %d", result);
		return result;
	}

	return 0;
}

static int bq35100_set_new_battery(const struct device *dev, const uint16_t new_design_capacity)
{
	int result = bq35100_send_cntl(dev, BQ35100_MAC_CMD_NEW_BATTERY);

	if (result) {
		LOG_ERR("Error setting new battery: %d", result);
		return result;
	}

	k_sleep(K_MSEC(BQ35100_FLASH_WRITE_DELAY * 2));

	result = bq35100_set_design_capacity(dev, new_design_capacity);

	if (result) {
		LOG_ERR("Error setting new design-capacity: %d", result);
		return result;
	}

	k_sleep(K_MSEC(BQ35100_NEW_BATTERY_DELAY));

	return 0;
}

static int bq35100_reset(const struct device *dev)
{
	int result;

	if (g_security_mode == SECURITY_SEALED) {
		result = bq35100_set_security_mode(dev, SECURITY_UNSEALED);
		if (result) {
			LOG_ERR("Unable to set SECURITY_UNSEALED");
			return -EINVAL;
		}
	}

	result = bq35100_send_cntl(dev, BQ35100_MAC_CMD_RESET);
	if (result) {
		LOG_ERR("Unable to reset device");
		return -EINVAL;
	}

	return true;
}

static int32_t calculate_remaining_capacity(int32_t design_capacity, int32_t accumulated_capacity)
{
	// Convert design capacity to µAh
	int64_t design_capacity_uah = (int64_t)design_capacity * 1000;

	// Calculate the remaining capacity
	// Note: accumulated_capacity is already negative when discharging
	int64_t remaining_capacity = design_capacity_uah + accumulated_capacity;

	// Ensure the result is between 0 and design capacity
	if (remaining_capacity < 0) {
		remaining_capacity = 0;
	} else if (remaining_capacity > design_capacity_uah) {
		remaining_capacity = design_capacity_uah;
	}

	// The result is guaranteed to be within int32_t range now
	return (int32_t)remaining_capacity;
}

static int32_t bq35100_process_prop(const fuel_gauge_prop_t prop, const uint8_t *const buffer,
				    union fuel_gauge_prop_val *const val)
{
	int32_t result = 0;
	uint32_t temp_val;

	if (!buffer || !val) {
		return -EINVAL;
	}

	switch (prop) {
	case FUEL_GAUGE_VOLTAGE:
		/* Required unit: µV, bq35100 unit: mV */
		temp_val = (uint32_t)sys_get_le16(buffer);
		val->voltage = (int32_t)(temp_val * 1000U);
		break;
	case FUEL_GAUGE_CURRENT:
		/* Required unit: µA, bq35100 unit: mA */
		temp_val = (uint32_t)sys_get_le16(buffer);
		val->current = (int32_t)(temp_val * 1000U);
		break;
	case FUEL_GAUGE_DESIGN_CAPACITY:
		/* Required unit: mAh, bq35100 unit: mAh */
		val->design_cap = (int32_t)sys_get_le16(buffer);
		break;
	case FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE:
	case FUEL_GAUGE_REMAINING_CAPACITY: {
		/* Both cases use the same starting point */
		int32_t accumulated = (int32_t)sys_get_le32(buffer);
		int32_t design = (int32_t)sys_get_le16(&buffer[4]);
		int32_t remaining = calculate_remaining_capacity(design, accumulated);

		if (prop == FUEL_GAUGE_REMAINING_CAPACITY) {
			/* Required unit: µAh */
			val->remaining_capacity = remaining;
		} else {
			/* FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE */
			/* Required unit: %, calculated from remaining and design capacity */
			if (design <= 0) {
				val->absolute_state_of_charge = 0;
			} else {
				int64_t soc =
					(int64_t)remaining * 100LL / ((int64_t)design * 1000LL);
				val->absolute_state_of_charge = (int32_t)CLAMP(soc, 0, 100);
			}
		}
		break;
	}
	default:
		result = -ENOTSUP;
		break;
	}

	return result;
}

static uint16_t bq35100_get_register(const fuel_gauge_prop_t prop)
{
	uint16_t reg = 0U;

	switch (prop) {
	case FUEL_GAUGE_VOLTAGE:
		reg = BQ35100_REG_VOLTAGE;
		break;
	case FUEL_GAUGE_CURRENT:
		reg = BQ35100_REG_CURRENT;
		break;
	case FUEL_GAUGE_DESIGN_CAPACITY:
		reg = BQ35100_REG_DESIGN_CAPACITY;
		break;
	case FUEL_GAUGE_REMAINING_CAPACITY:
	case FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE:
		reg = BQ35100_REG_ACCUMULATED_CAPACITY;
		break;
	default:
		/* Keep reg as 0 for unsupported properties */
		break;
	}

	return reg;
}

static int bq35100_read_registers(const struct device *const dev, const fuel_gauge_prop_t prop,
				  uint8_t *const buffer)
{
	int result = 0;
	uint8_t address;

	if (!buffer) {
		return -EINVAL;
	}

	address = (uint8_t)bq35100_get_register(prop);

	switch (prop) {
	case FUEL_GAUGE_VOLTAGE:
	case FUEL_GAUGE_CURRENT:
	case FUEL_GAUGE_DESIGN_CAPACITY:
		result = bq35100_get_data(dev, address, buffer, sizeof(uint16_t));
		break;
	case FUEL_GAUGE_REMAINING_CAPACITY:
	case FUEL_GAUGE_ABSOLUTE_STATE_OF_CHARGE:
		result = bq35100_get_data(dev, BQ35100_REG_ACCUMULATED_CAPACITY, buffer,
					  sizeof(uint32_t));
		if (result == 0) {
			result = bq35100_get_data(dev, BQ35100_REG_DESIGN_CAPACITY,
						  &buffer[sizeof(uint32_t)], sizeof(uint16_t));
		}
		break;
	default:
		result = -ENOTSUP;
		break;
	}

	return result;
}

static int bq35100_get_prop(const struct device *const dev, const fuel_gauge_prop_t prop,
			    union fuel_gauge_prop_val *const val)
{
	int result = 0;
	uint8_t buffer[6]; /* Max size needed for ABSOLUTE_STATE_OF_CHARGE */

	if ((val == NULL) || (prop >= FUEL_GAUGE_PROP_MAX)) {
		return -EINVAL;
	}

	result = bq35100_read_registers(dev, prop, buffer);
	if (result == 0) {
		result = bq35100_process_prop(prop, buffer, val);
	}

	return result;
}

static int bq35100_set_prop(const struct device *dev, fuel_gauge_prop_t prop,
			    union fuel_gauge_prop_val val)
{
	int result = 0;

	switch (prop) {
	case FUEL_GAUGE_DESIGN_CAPACITY:
		LOG_DBG("Setting design capacity");
		result = bq35100_set_design_capacity(dev, val.design_cap);
		break;
	case FUEL_GAUGE_BQ35100_NEW_BATTERY:
		LOG_DBG("Setting new-battery");
		result = bq35100_set_new_battery(dev, val.design_cap);
		break;
	case FUEL_GAUGE_BQ35100_RESET:
		LOG_DBG("Resetting BQ35100");
		result = bq35100_reset(dev);
		break;
	case FUEL_GAUGE_BQ35100_START:
		LOG_DBG("Setting Gauge-Start");
		result = bq35100_start_gauge(dev);
		break;
	case FUEL_GAUGE_BQ35100_STOP:
		LOG_DBG("Setting Gauge-Stop");
		result = bq35100_stop_gauge(dev);
		break;
	default:
		result = -ENOTSUP;
	}

	return result;
}

static int bq35100_init(const struct device *dev)
{
	int result = 0;
	const struct bq35100_config *cfg;
	cfg = dev->config;
	uint16_t device_type = 0;
	uint16_t status = 0;
	uint8_t extracted_security;
	uint8_t extracted_initcomp;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	result = bq35100_get_status(dev, &status);
	if (result) {
		LOG_ERR("Reading device-status failed");
		return -ENODEV;
	}

	result = bq35100_get_device_type(dev, &device_type);
	if (result) {
		LOG_ERR("Reading device-type failed");
		return -ENODEV;
	}

	if (device_type != BQ35100_DEVICE_TYPE) {
		LOG_ERR("Devicetype missmatch! Expected: %d, Received: %d", BQ35100_DEVICE_TYPE,
			device_type);
		return -ENODEV;
	}

	extracted_security = FIELD_GET(BIT_MASK(2) << 13, status);

	switch (extracted_security) {
	case SECURITY_FULL_ACCESS:
		LOG_DBG("Device is in FULL ACCESS mode");
		break;

	case SECURITY_UNSEALED:
		LOG_DBG("Device is in UNSEALED mode");
		break;

	case SECURITY_SEALED:
		LOG_DBG("Device is in SEALED mode");
		break;

	default:
		LOG_DBG("Invalid device mode");
		return -ENODEV;
	}

	g_security_mode = extracted_security;

	extracted_initcomp = BIT(7) & status;

	if (!extracted_initcomp) {
		LOG_WRN("Device initialization not complete");
		if (bq35100_wait_for_status(dev, BIT(7), BIT(7), K_MSEC(300))) {
			LOG_ERR("Device initialization failed");
			return -ENODEV;
		}
	}

	LOG_INF("BQ35100 with device-type %04X initialized", device_type);

	return 0;
}

static const struct fuel_gauge_driver_api bq35100_driver_api = {
	.get_property = &bq35100_get_prop,
	.set_property = &bq35100_set_prop,
};

#if DT_NUM_INST_STATUS_OKAY(DT_DRV_COMPAT) == 0
#error "BQ35100 device is not defined in DTS"
#endif

#define BQ35100_INIT(index)                                                                        \
                                                                                                   \
	static const struct bq35100_config bq35100_config_##index = {                              \
		.i2c = I2C_DT_SPEC_INST_GET(index),                                                \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(index, &bq35100_init, NULL, NULL, &bq35100_config_##index,           \
			      POST_KERNEL, CONFIG_FUEL_GAUGE_INIT_PRIORITY, &bq35100_driver_api);

DT_INST_FOREACH_STATUS_OKAY(BQ35100_INIT)

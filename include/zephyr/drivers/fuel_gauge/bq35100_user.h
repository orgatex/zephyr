/*
 * Copyright (c) 2024, Orgatex GmbH
 *
 * SPDX-License-Identifier: LicenseRef-Proprietary
 */

/**
 * @file
 * @brief Extended public API for the BQ35100
 */

#ifndef ZEPHYR_DRIVERS_FUEL_GAUGE_BQ35100_PROPS_H_
#define ZEPHYR_DRIVERS_FUEL_GAUGE_BQ35100_PROPS_H_

#include <zephyr/drivers/fuel_gauge.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum fuel_gauge_prop_type_bq35100
 * @brief Enumeration of custom property types for BQ35100 fuel gauge.
 */
enum fuel_gauge_prop_type_bq35100 {
	/**
	 * @brief Initialize a new battery.
	 * @note Requires fuel_gauge_prop_val: design_cap to be set.
	 */
	FUEL_GAUGE_BQ35100_NEW_BATTERY = FUEL_GAUGE_CUSTOM_BEGIN,

	/**
	 * @brief Reset the fuel gauge.
	 * @note Does not require any fuel_gauge_prop_val.
	 */
	FUEL_GAUGE_BQ35100_RESET,

	/**
	 * @brief Start the fuel gauge.
	 * @note Does not require any fuel_gauge_prop_val.
	 */
	FUEL_GAUGE_BQ35100_START,

	/**
	 * @brief Stop the fuel gauge.
	 * @note Does not require any fuel_gauge_prop_val.
	 */
	FUEL_GAUGE_BQ35100_STOP
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_FUEL_GAUGE_BQ35100_PROPS_H_ */

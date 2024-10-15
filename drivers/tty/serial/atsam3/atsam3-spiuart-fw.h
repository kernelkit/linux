/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 *
 * Project Common Technology Linux
 * Subsystem Atmel SAM3 SPI UART bridge driver
 */

/**
 * @file
 * Firmware configuartion and parsing functions. Whole functionality
 * responsible to handle firmware binaries is provided by this file.
 */

#ifndef ATSAM3SPIUARTFW_H_
#define ATSAM3SPIUARTFW_H_

#include "atsam3-spiuart.h"

// @brief get atmel sam3 firmware paths from dts
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to atsam3_spi_entity object
// @return 0 on success, negative otherwise
int atsam3_fw_get_dts_path(struct atsam3_private_data* priv,
                struct atsam3_spi_entity* entity);

// @brief perform start of atsam3 bootloader
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @return 0 on success, negative otherwise
int atsam3_fw_btl_start(struct atsam3_private_data* priv,
                struct atsam3_spi_entity* entity);

// @brief gather information about loaded firmware at atmel and expected
// by user, if they are different perform application update
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @return 0 on success, negative otherwise
int atsam3_fw_app_update(struct atsam3_private_data* priv,
                struct atsam3_spi_entity* entity);

// @brief perform firmware start procedure at atsam3
// @param priv - pointer to atsam3_private_data object
// @param entity_id - id of atsam3 mcu to start
// @return 0 on success, negative otherwise
int atsam3_fw_app_start(struct atsam3_private_data* priv,
                struct atsam3_spi_entity* entity);

// @brief seek firmware file pointer
// @param fw - pointer to request filesystem firmware
// @param seek - shift in bytes
// @return shifted addess, null pointer otherwise
const u8* atsam3_fw_seek(const struct firmware* fw, int seek);

#endif // ATSAM3SPIUARTFW_H_

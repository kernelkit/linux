/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 *
 * Project Common Technology Linux
 * Subsystem Atmel SAM3 SPI UART bridge driver
 */

/**
 * @file
 * TTY layer function definitions shared across whole driver.
 */

#ifndef ATSAM3_SPIUART_TTY_H_
#define ATSAM3_SPIUART_TTY_H_

#include "atsam3-spiuart.h"

// @brief atsam3 read callback, called when atsam3 raise interrupt
// with SPI_CPR_IRQ_STATE_APPL_RECEIVE_DATA.
// For reading purposes flip buffer is used.
// @param priv - pointer to atsam3_private_data object
// @return count of bytes, negative otherwise
int atsam3_tty_read_clb(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity);

// @brief remove tty driver and devices from linux kernel at exit
// of driver
// @param priv - pointer to atasm3_private_data object
// @return 0 on success, negative otherwise
int atsam3_tty_remove(struct atsam3_private_data* priv);

// @brief Initialize the atsam3_tty structure.
// @param priv - pointer to atsam3_private_data object
// @return pointer to tty_driver object
struct tty_driver* atsam3_tty_drv_init(struct atsam3_private_data* priv);

#endif // ATSAM3_SPIUART_TTY_H_

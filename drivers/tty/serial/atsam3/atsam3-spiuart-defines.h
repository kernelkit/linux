/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 *
 * Project Common Technology Linux
 * Subsystem Atmel SAM3 SPI UART bridge driver
 */
 
/**
 * @file
 * Driver specific defines.
 */

#ifndef ATSAM3_SPIUART_DEFINES_H_
#define ATSAM3_SPIUART_DEFINES_H_

// ATSAM specific
#define DRIVER_NAME			"atsam3SPIUART"
#define ATSAM3_TTY_NAME 		"ATSAM3"
#define ATSAM3_SPIUART_TTY_ID 		(0u)
#define ATSAM3_SPIUART_TTY_MINORS 	(3u)
#define ATSAM3_SPIUART_TTY_FLAGS 	(0u)

#define ATSAM3_BUFFER_SIZE (320u)
#define ATSAM3_BLOCK_DATA_SIZE 		(320u)
#define ATSAM3_SIZE_PER_BLOCK_NUMBER 	(256u)

#define ATSAM3_CHIP_COUNT 		(2u)
#define ATSAM3_MIN_BAUD_RATE            (50)
#define ATSAM3_MAX_BAUD_RATE            SIO_BAUD_3000_KBD
#define ATSAM3_DEFAULT_BAUD_RATE        (9600)

#define DEVICE_NAME			"atsam3SPIUART"
#define DRIVER_AUTHOR			"Hitachi Energy"
#define DRIVER_LICENSE			"GPL"
#define DRIVER_DESCRIPTION 		"Atmel SAM3 SPI UART bridge driver"
#define DRIVER_INFO 			"1.0.rc"

#define ATSAM3_SPI_IRQ0_NAME 		"SPI0_IRQ0"
#define ATSAM3_SPI_IRQ1_NAME 		"SPI0_IRQ1"

#define ATSAM3_SPI_CFG_BPW		(8u)
#define ATSAM3_SPIUART_BUFFER_SIZE 	(SPI_CPR_MASTER_REQUEST_SIZE*2)
#define ATSAM3_BYTES_TO_BPW(X, Y) (sizeof(X)*Y)

#define ATSAM3_BLR_FW_MAJOR_VERSION_OFFSET (0xC4)
#define ATSAM3_BLR_FW_MINOR_VERSION_OFFSET (0xC5)
#define ATSAM3_BLR_FW_TYPE_NUMBER_OFFSET (0xC6)
#define ATSAM3_BLR_FW_SIZE_OFFSET (0xC8)
#define ATSAM3_BLR_FW_TYPE_OFFSET (0xCC)
#define ATSAM3_BLR_FW_VERSION_LENGTH (4)
#define ATSAM3_BLR_FW_TYPE_LENGTH (1)

#endif // ATSAM3_SPIUART_DEFINES_H_

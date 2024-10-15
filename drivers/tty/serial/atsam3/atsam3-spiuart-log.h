/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 *
 * Project Common Technology Linux
 * Subsystem Atmel SAM3 SPI UART bridge driver
 */

/**
 * @file
 * Logging macros definition.
 */

#ifndef ATSAM3_SPIUART_LOG_H_
#define ATSAM3_SPIUART_LOG_H_

#include "atsam3-spiuart-defines.h"

// ATSAM specific log
#define LENTRY() printk(KERN_INFO "[%s]:[%s:%d] entry", DRIVER_NAME, __func__, __LINE__)
#define LEXIT() printk(KERN_INFO "[%s]:[%s:%d] exit", DRIVER_NAME, __func__, __LINE__)
#define LERR(FORMAT, ...) printk(KERN_ERR "[%s]:[%s:%d] " FORMAT, DRIVER_NAME, __func__, __LINE__, ##__VA_ARGS__)
#define LINF(FORMAT, ...) printk(KERN_INFO "[%s]:[%s:%d] " FORMAT, DRIVER_NAME, __func__, __LINE__, ##__VA_ARGS__)
#ifdef DEBUG_MODE
#define LDBG(FORMAT, ...) printk(KERN_INFO "[%s]:[%s] " FORMAT, DRIVER_NAME, __func__, ##__VA_ARGS__)
#define LDENTRY() printk(KERN_INFO "[%s]:[%s] entry", DRIVER_NAME, __func__)
#define LDEXIT() printk(KERN_INFO "[%s]:[%s] exit", DRIVER_NAME, __func__)
#else
#define LDENTRY(...)
#define LDEXIT(...)
#define LDBG(...)
#endif


#endif // ATSAM3_SPIUART_LOG_H_

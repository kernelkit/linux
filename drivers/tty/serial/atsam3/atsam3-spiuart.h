/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 *
 * Project Common Technology Linux
 * Subsystem Atmel SAM3 SPI UART bridge driver
 */

/**
 * @file
 * Driver specific types definition.
 */

#ifndef ATSAM3_SPIUART_H_
#define ATSAM3_SPIUART_H_

#include <stddef.h>
#include <linux/spi/spi.h>
#include <linux/tty.h>
#include <linux/semaphore.h>
#include <linux/interrupt.h>
#include <linux/serial.h>
#include <linux/termios.h>
#include <linux/hrtimer.h>
#include "spiCprDefines.h"
#include "atsam3-spiuart-defines.h"
#include "atsam3-spiuart-log.h"

// macro definition ->

#define OPPOSITE_ENTITY_ID(ENT) (ENT->id == 0 ? 1 : 0)

#define DECLARE_STD_INFO_BUFFER(name) uint8_t buffer_##name[SPI_CPR_MASTER_REQUEST_SIZE]; \
    spiCprSlaveResponse_type* name; \
    name = (spiCprSlaveResponse_type*)&buffer_##name;

// <- macro definition

// structs definition ->

// @brief firmware information object
struct atsam3_fw_info {
    // @brief linux kernel firmware structure, assigned with request_firmware method
    const struct firmware* fw;
    // @brief file system path to currently running firmware
    const char* user_path;
    // @brief short name SLC|PDC|LCC of running firmware
    const char* fw_name;
    // @brief firmware image size
    size_t fw_size;
    // @brief firmware information
    spiCprSlaveFwInformation_type fwInfo;
};

typedef int (*_atsam3_irq_status_handler)(void* priv);
typedef int (*_atsam3_transmit_data_handler)(void* priv, unsigned char* data, int count);

// instead of using nested if trees, use state machine
struct atsam3_mcu_state {
    _atsam3_irq_status_handler _irq_stat_handler;
    _atsam3_transmit_data_handler _transmit_data_handler;
};

struct atsam3_spi_entity {
    struct gpio_desc* reset_gpio;
    struct work_struct work;
    int irq;
    int irq_gpio;
    // @brief standard status response from irq handler
    spiCprSlaveResponse_type sync_response;
    atomic_t is_open;
    atomic_t is_transmitting;
    int id;
    char fw_name[4];
    struct tty_port* port;
    struct atsam3_fw_info _fw_info;
    struct atsam3_mcu_state _boot_state;
    struct atsam3_mcu_state _app_state;
    struct atsam3_mcu_state* state;
    struct termios tty;
    unsigned int mctrl;
    unsigned int modem_line_status;
    unsigned int req_result;
    struct serial_icounter_struct counter;
};

// @brief aggregates spi connected objects
struct atsam3_spi_data {
    // @brief pointer to linux kernel spi_device generated at spi_probe method
    struct spi_device* spi_device;
    // @brief spi entities
    struct atsam3_spi_entity entity;
};

// @brief atmel sam3n private data driver context
struct atsam3_private_data {
    // @brief pin responsible for syncing time with atmel
    struct gpio_desc* fb_pin;
    // @brief 
    struct semaphore lock_flag;
    atomic_t open_counter;

    struct workqueue_struct *wq;

    // @brief pointer for storing work data structure
    // used for getting access to atsam3_private_data via
    // container_of macro
    void* work_data;
    // @brief pointr to atsam3_tty structure object
    struct tty_driver* tty_driver;

    struct atsam3_spi_data _spi_data;

    ktime_t fb_timer_period;
    struct hrtimer fb_timer;
};

// <- structs definition

// @brief starts feedback timer if there is no created
int atsam3_start_timer_fb(struct atsam3_private_data* priv);

// @brief get interrupts pins from dts
// @param priv - pointer to atsam3_private_data object
// @return 0 if ok, nonzero othwerise
int atsam3_get_irq_pins(struct atsam3_private_data* priv);

// @brief initialize application interrupt handler
// @param priv - pointer to atsam3_private_data object
int atsam3_init_irqs(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity);

// @brief deinitialize assigned interrupt handlers
// @param priv - pointer to atsam3_private_data object
void atsam3_deinit_irqs(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity);

#endif // ATSAM3_SPIUART_H_

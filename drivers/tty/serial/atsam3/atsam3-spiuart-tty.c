/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 *
 * Project Common Technology Linux
 * Subsystem Atmel SAM3 SPI UART bridge driver
 */

/**
 * @file
 * TTY layer functions which provides whole functionality for proper
 * handling user request and passing them do ATMEL SAM3 device.
 */

#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/serial.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/crc32.h>
#include <linux/hrtimer.h>
#include "atsam3-spiuart.h"
#include "atsam3-spiuart-fw.h"
#include "atsam3-spiuart-tty.h"
#include "atsam3-spiuart-spi.h"
#include <linux/tty_flip.h>

#define ATSAM3_MAP_CSIZE_TO_CONTROLLER(X) (X >> 4)
#define ATSAM3_MAP_CSTOP_TO_CONTROLLER(X) (X >> 2)
#define ATSAM3_MAP_PARITY_TO_CONTROLLER(X) (X >> 5)
#define ATSAM3_MAP_CLOCAL_TO_CONTROLLER(X) (X != 0 ? 0x01 : 0x00)

static const char ATSAM3_NAME_SLC[] = "SLC";
static const char ATSAM3_NAME_PDC[] = "PDC";
static const char ATSAM3_NAME_LCC[] = "LCC";

static int atsam3_tty_open(struct tty_struct* tty, struct file* filp);
static void atsam3_tty_close(struct tty_struct* tty, struct file* filp);
static int atsam3_tty_write_room(struct tty_struct* tty);
static int atsam3_tty_write(struct tty_struct* tty, const unsigned char* buf, int count);
static int atsam3_tty_install(struct tty_driver* driver, struct tty_struct* tty);
static int atsam3_tty_ioctl(struct tty_struct* tty, unsigned int cmd, unsigned long arg);
static int atsam3_tty_tiocmset(struct tty_struct* tty, unsigned int set, unsigned int clear);
static int atsam3_tty_tiocmget(struct tty_struct* tty);
static int atsam3_tty_tiocgicount(struct tty_struct* tty, struct serial_icounter_struct* icounter);
static void atsam3_tty_set_termios(struct tty_struct* tty, struct ktermios* otermios);

static int _atsam3_perform_init(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity);

// @brief copies user space termios to kernel ktermios object
// @param termios - pointer to termios object
// @param ktermios - pointer to ktermios object
static void _atsam3_termios_to_ktermios(struct termios* termios, struct ktermios* ktermios) {
    LDENTRY()

    if (termios && ktermios) {
        ktermios->c_cflag = termios->c_cflag;
        ktermios->c_oflag = termios->c_oflag;
        ktermios->c_lflag = termios->c_lflag;
        ktermios->c_iflag = termios->c_iflag;
        ktermios->c_line = termios->c_line;
        
        memcpy(ktermios->c_cc, termios->c_cc, sizeof(cc_t) * NCCS);
    }
}

// @brief tty shutdown system callback
static void _atsam3_tty_port_shutdown(struct tty_port* port) {
    LDENTRY();
    LDEXIT();
}

// @brief tty port atctivate system callback
static int _atsam3_tty_port_activate(struct tty_port* port, struct tty_struct* tty) {
    LDENTRY();
    LDEXIT();
    return 0;
}

// @brief tty port specific opearations
static const struct tty_port_operations atsam3_tty_port = {
    .activate = _atsam3_tty_port_activate,
    .shutdown = _atsam3_tty_port_shutdown,
};


// @brief tty operations callbacks holder
static const struct tty_operations atsam3_tty_ops = {
    .open = atsam3_tty_open,
    .close = atsam3_tty_close,
    .write_room = atsam3_tty_write_room,
    .write = atsam3_tty_write,
    .install = atsam3_tty_install,
    .ioctl = atsam3_tty_ioctl,
    .set_termios = atsam3_tty_set_termios,
    .tiocmget = atsam3_tty_tiocmget,
    .tiocmset = atsam3_tty_tiocmset,
    .get_icount = atsam3_tty_tiocgicount,
};

// @brief callback for moving atasm3n mcu to uart configuration state
// @param tty - ponter to tty_struct passed by tty kernel layer
// @param ntermios - poitner to ktermios struct passed by tty kernel layer
static void atsam3_tty_set_termios(struct tty_struct* tty, struct ktermios* ntermios) {
    struct atsam3_private_data* priv = NULL;
    struct atsam3_spi_entity* entity = (struct atsam3_spi_entity*)tty->port->client_data;

    priv = container_of(entity, struct atsam3_private_data, _spi_data.entity);

    LDENTRY();
    atsam3_spi_change_drv_state(priv, entity, SPI_CPR_STATE_CHANGE_START_UART_CONFIGURATION);
    LDEXIT();
}

// @brief returns to user space current termios configuration
// @param ttys - pointer to tty_struct
// @param ent - pointer to void casted atsam3_spi_entity pointer
// @param arg - pointer to user space termios structure
// @return 0 on succes, negative otherwise
static int _atsam3_tty_ioctl_tcgets(struct tty_struct* ttys, void* ent, unsigned long arg) {
    int result = 0;
    struct termios* tty = (struct termios*)arg;
    struct atsam3_spi_entity* entity = (struct atsam3_spi_entity*)ent;

    LDENTRY();
    if (copy_to_user((void __user*)tty, &entity->tty, sizeof(struct termios))) {
        LERR("Copy to user failed!");
        result = -EACCES;
    }
    
    LDEXIT();
    return result;
}

// @brief set baudrate on user space request
// @param ktty - pointer to ktermios object
// @param entity - pointer to atsam3_spi_entity object
// @return 0 on success, negative otherwise
static inline int _atsam3_tty_ioctl_set_baudrate(struct ktermios* ktty, struct atsam3_spi_entity* entity) {
    struct atsam3_private_data* private = container_of(entity, struct atsam3_private_data, _spi_data.entity);
    speed_t baud = tty_termios_baud_rate(ktty);
    int result = 0;

    LDENTRY();

    if (!baud) {
        LERR("Get baud rate failed!");
        return -EIO;
    }
    if (baud < ATSAM3_MIN_BAUD_RATE || baud > ATSAM3_MAX_BAUD_RATE) {
        LERR("Incorrect baud rate value");
        return -EIO;
    }
    if ((result = atsam3_spi_set_cfg_para(private, entity, SPI_CPR_CONFIG_TYPE_TRANSMIT_BAUD_RATE, sizeof(uint32_t), baud))) {
        LERR("set config param failed");
        goto exit;
    }
    entity->tty.c_cflag = ktty->c_cflag;

exit:
    LDEXIT();
    return result;
}

// @brief updates atsam3 cflag modem configuration
// @param entity - pointer to astam3_spi_entity
// @param ttys - pointer to tty_struct object
// @param user_termios - user modem configuration copied into kernel termios
// @return 0 on success, negative otherwise
static int _atsam3_tty_ioctl_tcsets_cflag(struct atsam3_spi_entity* entity,
                      struct tty_struct* ttys,
                      struct ktermios* user_termios) {
    int result = 0;
    struct termios* etty = &((struct atsam3_spi_entity*)entity)->tty;
    struct atsam3_private_data* priv = container_of(entity, struct atsam3_private_data, _spi_data.entity);
    uint16_t hw_opt = 0;

    LDENTRY();
    if (etty->c_cflag != user_termios->c_cflag) {
        hw_opt |= ATSAM3_MAP_CSIZE_TO_CONTROLLER(user_termios->c_cflag & CSIZE);
        hw_opt |= ATSAM3_MAP_CSTOP_TO_CONTROLLER(user_termios->c_cflag & CSTOPB);
        hw_opt |= ATSAM3_MAP_CLOCAL_TO_CONTROLLER((user_termios->c_cflag & CLOCAL));
        if (user_termios->c_cflag & PARENB) {
            hw_opt |= ATSAM3_MAP_PARITY_TO_CONTROLLER(user_termios->c_cflag & PARODD);
        }
        if (user_termios->c_cflag & (CBAUD | CBAUDEX)) {
            _atsam3_tty_ioctl_set_baudrate(user_termios, entity);
        }
        etty->c_cflag = user_termios->c_cflag;
        ttys->termios.c_cflag = etty->c_cflag;
        if ((result = atsam3_spi_set_cfg_para(priv,
                    entity,
                    SPI_CPR_CONFIG_TYPE_HW_OPTIONS,
                    sizeof(uint16_t),
                    hw_opt))) {
            LERR("ioctl SPI_CPR_CONFIG_TYPE_HW_OPTIONS failed");
        }
    }

exit:
    LDEXIT();
    return result;
}

// @brief updates termios lflag configuration, does not impact on atsam3
// @param entity - pointer to atsam3_spi_entity object
// @param ttys - pointer to tty_struct object
// @param user_termios - user modem configuration copied into kernel termios
// @return 0 on success, negative otherwise
static int _atsam3_tty_ioctl_tcsets_lflag(struct atsam3_spi_entity* entity,
                      struct tty_struct* ttys,
                      struct ktermios* user_termios) {
    int result = 0;
    struct termios* etty = &((struct atsam3_spi_entity*)entity)->tty;

    LDENTRY();
    if (etty->c_lflag != user_termios->c_lflag) {
        etty->c_lflag = user_termios->c_lflag;
        ttys->termios.c_lflag = etty->c_lflag;
    }
    LDEXIT();
    return result;
}

// @brief updates termios flag configuration, does not impact on atsam3
// @param entity - pointer to atsam3_spi_entity object
// @param ttys - pointer to tty_struct object
// @param user_termios - user modem configuration copied into kernel termios
// @return 0 on success, negative otherwise
static int _atsam3_tty_ioctl_tcsets_iflag(struct atsam3_spi_entity* entity,
                      struct tty_struct* ttys,
                      struct ktermios* user_termios) {
    int result = 0;
    struct termios* etty = &((struct atsam3_spi_entity*)entity)->tty;

    LDENTRY();
    if (etty->c_iflag != user_termios->c_iflag) {
        etty->c_iflag = user_termios->c_iflag;
        ttys->termios.c_iflag = etty->c_iflag;
    }
    LDEXIT();
    return result;
}

// @brief updates termios iflag configuration, does not impact on atsam3
// @param entity - pointer to atsam3_spi_entity object
// @param ttys - pointer to tty_struct object
// @param user_termios - user modem configuration copied into kernel termios
// @return 0 on success, negative otherwise
static int _atsam3_tty_ioctl_tcsets_oflag(struct atsam3_spi_entity* entity,
                      struct tty_struct* ttys,
                      struct ktermios* user_termios) {
    int result = 0;
    struct termios* etty = &((struct atsam3_spi_entity*)entity)->tty;

    LDENTRY();
    if (etty->c_oflag != user_termios->c_oflag) {
        etty->c_oflag = user_termios->c_oflag;
        ttys->termios.c_oflag = etty->c_oflag;
    }
    LDEXIT();

    return result;
}

// @brief updates termios flag configuration
// @param ttys - pointer to tty_struct object
// @param entity - void casted atsam3_spi_entity pointer
// @param arg - user space termios pointer object
// @return 0 on success, negative otherwise
static int _atsam3_tty_ioctl_tcsets(struct tty_struct* ttys, void* entity, unsigned long arg) {
    int result = 0;
    struct termios* tty = (struct termios*)arg;
    struct termios tmptty;
    struct ktermios ktty;
    DECLARE_STD_INFO_BUFFER(info);

    LDENTRY();
    if (!tty || !entity) {
        LERR("Incorrect arguments!");
        result = -EINVAL;
        goto exit;
    }
    if (copy_struct_from_user(&tmptty, sizeof(struct termios), (const void* __user)tty, sizeof(struct termios))) {
        LERR("Copy struct from user failed!");
        result = -EACCES;
        goto exit;
    }
    _atsam3_termios_to_ktermios(&tmptty, &ktty);
 
    LDBG("ktty.c_cflag[%x] c_oflag[%x] c_lflag[%x] c_iflag[%x]", ktty.c_cflag, ktty.c_oflag, ktty.c_lflag, ktty.c_iflag);

    if (_atsam3_tty_ioctl_tcsets_cflag(entity, ttys, &ktty)) {
        LERR("tcsets cflag failed");
        return -ECOMM;
    }
    if (_atsam3_tty_ioctl_tcsets_oflag(entity, ttys, &ktty)) {
        LERR("tcsets oflag failed");
        return -ECOMM;
    }
    if (_atsam3_tty_ioctl_tcsets_iflag(entity, ttys, &ktty)) {
        LERR("tcsets iflag failed");
        return -ECOMM;
    }
    if (_atsam3_tty_ioctl_tcsets_lflag(entity, ttys, &ktty)) {
        LERR("tcsets lflag failed");
        return -ECOMM;
    }

exit:
    LDEXIT();
    return result;
}

// @brief set atsam3 usart mode
// @param tty - pointer to tty_struct object
// @param entity - pointer to atsam3_spi_entity object
// @param arg - user space flag true or false
// @return 0 on success, negative otherwise
static int _atsam3_tty_ioctl_tiocsers485tx(struct tty_struct* tty, struct atsam3_spi_entity* entity, unsigned long arg) {
    int result = 0;
    bool flag = (bool)arg;
    spiCprSlcUsartMode_enum mode = SPI_CPR_SLC_USART_NORMAL_MODE;
    struct atsam3_private_data* priv = container_of(entity, struct atsam3_private_data, _spi_data.entity);

    LDENTRY();
    if (flag) {
        mode = SPI_CPR_SLC_USART_RS485_MODE;
    }

    result = atsam3_spi_set_ioctl(priv,
                entity,
                SPI_CPR_IOCTL_USART_MODE_SET,
                sizeof(uint32_t),
                (uint32_t)mode);

    if (result) {
        LERR("set ioctl USART_MODE failed");
        goto exit;
    }

exit:
    LDEXIT();
    return result;
}

// @brief set atsam3 usart mode
// @param tty - pointer to tty_struct object
// @param entity - pointer to atsam3_spi_entity object
// @param arg - user space serial_rs485 pointer
// @return 0 on success, negative otherwise
static int _atsam3_tty_ioctl_tiocsrs485(struct tty_struct* tty, struct atsam3_spi_entity* entity, unsigned long arg) {
    int result = 0;
    spiCprSlcUsartMode_enum usartMode = SPI_CPR_SLC_USART_NORMAL_MODE;
    struct atsam3_private_data* priv = container_of(entity, struct atsam3_private_data, _spi_data.entity);
    struct serial_rs485 rs485;

    LDENTRY();

    if (!arg) {
        LERR("invalid input argument");
        result = -EINVAL;
        goto exit;
    }

    if (copy_struct_from_user(&rs485, sizeof(struct serial_rs485), (const void* __user)arg, sizeof(struct serial_rs485))) {
        LERR("Copy struct from user failed!");
        goto exit;
    }

    if (rs485.flags & (SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND)) {
        usartMode = SPI_CPR_SLC_USART_RS485_MODE;
    }
    else {
        usartMode = SPI_CPR_SLC_USART_NORMAL_MODE;
    }

    result = atsam3_spi_set_ioctl(priv,
                entity,
                SPI_CPR_IOCTL_USART_MODE_SET,
                sizeof(uint32_t),
                (uint32_t)usartMode);

    if (result) {
        LERR("set ioctl USART_MODE failed");
        goto exit;
    }

exit:
    LDEXIT();
    return result;
}

// @brief copies serial statistics to user space
// @param tty - pointer to tty_struct passed by kernel
// @param icounter - pointer to user space serial_icounter_struct object
// @return 0 on success, negative otherwise
static int atsam3_tty_tiocgicount(struct tty_struct* tty, struct serial_icounter_struct* icounter) {
    int result = 0;
    struct atsam3_spi_entity* entity = (struct atsam3_spi_entity*)tty->port->client_data;

    LDENTRY();

    if (!entity) {
        LERR("Lack of entity!");
        result = -EBUSY;
        goto exit;
    }
    icounter->rx = entity->counter.rx;
    icounter->tx = entity->counter.tx;
    icounter->frame = entity->counter.frame;
    icounter->overrun = entity->counter.overrun;
    icounter->parity = entity->counter.parity;

exit:
    LDEXIT();
    return result;
}

// #brief returns modem control flag
// @param tty - pointer to kernel passed tty_struct object
// @return modem control flag
static int atsam3_tty_tiocmget(struct tty_struct* tty) {
    int result = -EINPROGRESS;
    struct atsam3_spi_entity* entity = NULL;

    LDENTRY();

    entity = (struct atsam3_spi_entity*)tty->port->client_data;
    if (!entity) {
        LERR("Lack of entity!");
        result = -EBUSY;
        goto exit;
    }

    result = entity->mctrl;

exit:
    LDEXIT();
    return result;
}

// @brief set modem line signals at atsam3
// @param tty - pointer to kernel passed tty_struct object
// @param set - bitmap of set signals
// @param clear - bitmap of clear signals
// @return 0 on success, negative otherwise
static int atsam3_tty_tiocmset(struct tty_struct* tty, unsigned int set, unsigned int clear) {
    int result = 0;
    struct atsam3_spi_entity* entity = NULL;
    struct atsam3_private_data* priv = NULL;
    int rioctl = SPI_CPR_IOCTL_NONE;
    int dioctl = SPI_CPR_IOCTL_NONE;
    int mode_line_status = 0;

    LDENTRY();

    entity = (struct atsam3_spi_entity*)tty->port->client_data;
    if (!entity) {
        LERR("Lack of entity!");
        result = -EBUSY;
        goto exit;
    }
    priv = container_of(entity, struct atsam3_private_data, _spi_data.entity);
    if (set & TIOCM_DTR) {
        dioctl = SPI_CPR_IOCTL_DTR_SET;
        entity->mctrl |= TIOCM_DTR;
    }
    if (set & TIOCM_RTS) {
        rioctl = SPI_CPR_IOCTL_RTS_SET;
        entity->mctrl |= TIOCM_RTS;
    }
    if (clear & TIOCM_DTR) {
        dioctl = SPI_CPR_IOCTL_DTR_RESET;
        entity->mctrl &= ~TIOCM_DTR;
    }
    if (clear & TIOCM_RTS) {
        rioctl = SPI_CPR_IOCTL_RTS_RESET;
        entity->mctrl &= ~TIOCM_RTS;
    }

    if ((rioctl != SPI_CPR_IOCTL_NONE) && atsam3_spi_modem_ioctl(priv, entity, rioctl, &mode_line_status)) {
        LERR("Request set modem ioctl failed");
        goto exit;
    }
    if ((dioctl != SPI_CPR_IOCTL_NONE) && atsam3_spi_modem_ioctl(priv, entity, dioctl, &mode_line_status)) {
        LERR("Request set modem ioctl failed");
        goto exit;
    }

    LDBG("mode_line_status[%x] entity->modem_line_status[%x] entity->mctrl[%x]",
        mode_line_status, entity->modem_line_status, entity->mctrl);

exit:
    LDEXIT();
    return result;
}

// @brief callback responsible for handling ioctl request from user space,
// it dispatch ioctls between driver layer and atmel sam3 layer
// @param tty - pointer to opened tty_struct
// @param cmd  - ioctl command
// @param arg - user argument
// @return 0 on success, negative otherwise
static int atsam3_tty_ioctl(struct tty_struct* tty, unsigned int cmd, unsigned long arg) {
    int result = 0;

    LDENTRY();

    if (!tty->port) {
        LERR("tty->port is NULL");
        return result;
    }
    if (!tty->port->client_data) {
        LERR("tty->port->client_data is NULL");
        return result;
    }
    switch(cmd) {
        case TCGETS:
            result = _atsam3_tty_ioctl_tcgets(tty, tty->port->client_data, arg);
            break;
        case TCSETS:
            result = _atsam3_tty_ioctl_tcsets(tty, tty->port->client_data, arg);
            break;
        // SIO_MODE_SET: rtuTtySioModeSet ->
        case TIOCSRS485: // argument struct serial_rs485
            result = _atsam3_tty_ioctl_tiocsrs485(tty, tty->port->client_data, arg);
            break;
        // SIO_RTU_SET_EXEC_TIME_SYNC_PULSE: atSam3NSpiSioProcessTimeSyncPulse ->
        case TIOCSERS485TX: // argument bool flag
            result = _atsam3_tty_ioctl_tiocsers485tx(tty, tty->port->client_data, arg);
            break;
        default:
            LERR("Unknown command [%d]", cmd);
            result = -EINVAL;
        break;
    }

    LDEXIT();
    return result;
}

// @brief callback for installing tty_port in system
// due to some problems connected with opening and reading ttySLCX/PDCX/LCCX
// paths, it is necearry to temporary overwrite opened port with atmel
// generic port
// @param driver - pointer to tty_driver object
// @param tty - pointer to tty_struct object
static int atsam3_tty_install(struct tty_driver* driver, struct tty_struct* tty) {
    LDENTRY();
    driver->ttys[tty->index] = tty;
    tty->port = driver->ports[tty->index];

    tty_standard_install(driver, tty);
    LDEXIT();
    return 0;
}

// @brief perform initialization of atsam3 device basing on node choose
// by user
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to atsam3_spi_entity object
// @return 0 on success, negative otherwise
static int _atsam3_perform_init(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity) {
    int result = 0;

    LDENTRY();

    result = atsam3_fw_btl_start(priv, entity);
    if (result) {
        LERR("Start bootloader failed");
        goto deinit_irqs_exit;
    }

    result = atsam3_fw_app_update(priv, entity);
    if (result) {
        LERR("Application update failed");
        goto deinit_irqs_exit;
    }

    LDEXIT();
    return result;

deinit_irqs_exit:
    atsam3_deinit_irqs(priv, entity);
    LDEXIT();
    return result;
}

// @brief get firmware name basing on file node name
// @param entity - pointer to atsam3_spi_entity object
// @param filp - pointer to kernel passed file object
// @return 0 on success, negative otherwise
static inline int _atsam3_tty_get_fw_name_from_filp(struct atsam3_spi_entity* entity, struct file* filp) {
    int result = -ENOENT;
    int minor = -1;
    char fw_name_buff[5] = {0};
    struct name_snapshot nm;

    take_dentry_name_snapshot(&nm, filp->f_path.dentry);
    // skip tty prefix
    if (sscanf(&nm.inline_name[3], "%3s%d", fw_name_buff, &minor)) {
        if ((0 <= minor)) {// && (minor < ATSAM3_CHIP_COUNT)) {
            // check does entity is not already opened
            if (!atomic_read_acquire(&entity->is_open)) {
                memcpy(entity->fw_name, fw_name_buff, 3);
                result = 0;
            }
            else {
                LERR("Node is already in use");
                result = -EBUSY;
            }
            atomic_inc(&entity->is_open);
        }
    }

    return result;
}

// @brief: Opens the tty subsystem, check which port was opened, start atsam3
// and if it is necessary update firmware on atsam3
// @param    tty  The tty structure to open
// @param    filp The file pointer
// @return   0 on success, -EINVAL if inode is invalid, -ENODEV if minor number is out of range
static int atsam3_tty_open(struct tty_struct* tty, struct file* filp) {
    int result = 0;
    struct atsam3_private_data* priv = NULL;
    struct atsam3_spi_entity* entity = NULL;
    entity = (struct atsam3_spi_entity*)tty->port->client_data;

    LDENTRY();
    if (!entity) {
        LERR("Cannot get entity!");
        return -EAGAIN;
    }
    priv = container_of(entity, struct atsam3_private_data, _spi_data.entity);
    
    if (_atsam3_tty_get_fw_name_from_filp(entity, filp)) {
        LERR("Get fw name from filp failed");
        return -ENOENT;
    }
    // get port based on minor value, tty->port->tty is already properly initalized
    entity->port = tty->port;
    // get path to firmware binaries
    result = atsam3_fw_get_dts_path(priv, entity);
    if (result) {
        LERR("Cannot get firmware path");
        goto exit;
    }

    // call kernel tty port open (initialize all mutexes and buffers)
    result = tty_port_open(entity->port, tty, filp);
    if (result) {
        LERR("Cannot open port");
        goto exit;
    }

    // perform init for atsam3 mcu
    result = _atsam3_perform_init(priv, entity);
    if (result) {
        result = -ENOTTY;
        LERR("Cannot perform atmel sam3 init");
        goto exit;
    }

    // start atsam3 firmware
    result = atsam3_fw_app_start(priv, entity);
    if (result) {
        result = -ENODEV;
        LERR("Cannot start firmware!");
        goto exit;
    }

exit:
    atomic_inc(&priv->open_counter);

    LDEXIT();
    return result;
}

// @brief: Closes already opened tty layer
// @param    tty      The tty structure to close
// @param    filp     The file pointer
static void atsam3_tty_close(struct tty_struct* tty, struct file* filp) {
    // set reset pin to low
    struct atsam3_spi_entity* entity = NULL;
    struct atsam3_private_data* priv = NULL;

    LDENTRY();
    entity = (struct atsam3_spi_entity*)tty->port->client_data;
    if (entity) {
        priv = container_of(entity, struct atsam3_private_data, _spi_data.entity);
        // decrement open counter and check are there any other opens tty nodes
        // if counter is 0, it is possible to close whole ATSAM3 spi connection
        if (atomic_dec_and_test(&entity->is_open)) {
            tty_port_close(entity->port, tty, filp);
            // turn off ATSAM3 mcu
            atsam3_spi_drive_reset_pin(priv, entity, 0);
            // clear entity
            entity->port = NULL;
            atsam3_deinit_irqs(priv, entity);
            atomic_set_release(&entity->is_open, 0);
            atsam3_spi_drive_reset_pin(priv, entity, 1);
            entity->state = NULL;
        }
    }
    LDEXIT();
}

// @brief return available space in bytes
// @param tty - pointer to tty_struct object
// @return SPI_CPR_MAX_DATA_BLOCK_SIZE if spi bus is free, 0 otherwise
static int atsam3_tty_write_room(struct tty_struct* tty) {
    struct atsam3_spi_entity* entity = NULL;

    entity = (struct atsam3_spi_entity*)tty->port->client_data;
    if (entity && atomic_read_acquire(&entity->is_transmitting) > 0) {
        return 0;
    }
    return SPI_CPR_MAX_DATA_BLOCK_SIZE;
}

// @brief perform data transmission to atsam3 mcu via spi bus
// @param entity - pointer to atsam3_spi_entity object
// @param buf - pointer to user space data
// @param count - count of data to tranmit
// @return count of transmitted bytes, negative otherwise
static int _atsam3_tty_transmit(struct atsam3_spi_entity* entity, const unsigned char* buf, int count) {
    int result = -ENODEV;
    int transmitted = 0;
    uint32_t bufferSize;
    struct atsam3_private_data* priv = NULL;

    LDBG("priv[%p] entity_id[%d] count[%x]", priv, entity->id, count);
    priv = container_of(entity, struct atsam3_private_data, _spi_data.entity);

    if (atomic_read_acquire(&entity->is_transmitting)) {
        return -EAGAIN;
    }
    atomic_set_release(&entity->is_transmitting, 1);
    while (count > 0) {
        bufferSize = count;
        if (bufferSize > SPI_CPR_MAX_DATA_BLOCK_SIZE) {
            bufferSize = SPI_CPR_MAX_DATA_BLOCK_SIZE;
        }
        count -= bufferSize;
        transmitted += bufferSize;
        LDBG("result[%x] bufferSize[%x]", result, bufferSize);
        if (bufferSize > 0) {
            result = atsam3_spi_send_data_buffer(priv,
                        entity,
                        SPI_CPR_REQ_TYPE_SET_TRANSMIT_SIZE,
                        (uint32_t)bufferSize,
                        (uint8_t*)buf);
            if (result) {
                LERR("Send data buffer failed");
                result = -EIO;
                break;
            }
            entity->counter.tx += 1;
        }
    }
    entity->counter.tx += transmitted;
    if (!result) {
        result = transmitted;
    }
    atomic_set_release(&entity->is_transmitting, 0);

    return result;
}

// @brief system callback for handling tty write operations
// @param tty - pointer to assigned tty structure object
// @param buf - pointer to user data
// @param count - user data size in bytes
// @return count of written bytes, negative in case of error
static int atsam3_tty_write(struct tty_struct* tty, const unsigned char* buf, int count) {
    int result = -ENODEV;

    LDENTRY();
    if (!tty->port && !tty->port->client_data) {
        LERR("tty->port is not set! terminating!");
        goto exit;
    }
    if (count > SPI_CPR_MAX_DATA_BLOCK_SIZE) {
        LERR("Unable to handle more than %d bytes", SPI_CPR_MAX_DATA_BLOCK_SIZE);
        result = -E2BIG;
        goto exit;
    }

    result = _atsam3_tty_transmit((struct atsam3_spi_entity*)tty->port->client_data, buf, count);

exit:
    LDEXIT();
    return result;
}

// @brief register tty port at linux kernel and user file system
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to atsam3_spi_entity object
// @param name - name which will occures at file system
// @param port_index - id of port
// @return 0 on success, negative otherwise
static int __atsam3_tty_port_register(struct atsam3_private_data* priv,
                   struct atsam3_spi_entity* entity,
                   const char* name,
                   unsigned int port_index) {
    #define TMP_NAME_SIZE (16)
    char * name_snap;
    int result = 0;
    unsigned int tty_index = 0;
    struct device *dev;
    struct tty_port* port;
    char tmp_name[TMP_NAME_SIZE] = {0};

    if (entity) {
        tty_index = entity->id;
    }

    LDENTRY();
    // MH: similiar solution is done in tty_register_device_attr function
    memset(tmp_name, 0, TMP_NAME_SIZE);
    snprintf(tmp_name, TMP_NAME_SIZE, "tty%s%d", name, tty_index);

    if (!priv->tty_driver || !name) {
        LERR("Wrong argument!");
        result = -EINVAL;
        goto exit;
    }

    name_snap = (char*)priv->tty_driver->name;
    priv->tty_driver->name = tmp_name;
    port = priv->tty_driver->ports[port_index];

    tty_port_init(port); // Initialize the port
    dev = tty_register_device(priv->tty_driver, port_index, NULL);
    if (IS_ERR(dev)) {
        result = PTR_ERR(dev);
        LERR("Cannot register device[%s] result[%d]", name, result);
    }
    else {
        LDBG("Registered device[%s]", name);
    }
    port->ops = &atsam3_tty_port;
    port->client_data = entity; 

exit:
    priv->tty_driver->name = name_snap;
    LDEXIT();
    return result;
}

// @brief Initializes an atsam3_tty object.
// @param priv - pointer to atsam3_private_data object
// @return 0 on success, negative otherwise.
static int _atsam3_tty_port_init(struct atsam3_private_data* priv) {
    int result = 0;
    int ii = 0;

    LDENTRY();

    if (!priv) {
        LERR("Wrong argument _tty"); // Print an error message
        result = -EINVAL; // Set the result to indicate an error
        goto exit; // Jump to the exit label
    }
    if (!priv->tty_driver) { // Check for a null tty driver
        LERR("Wrong argument _tty->tty_driver"); // Print an error message
        result = -EINVAL; // Set the result to indicate an error
        goto exit; // Jump to the exit label
    }

    priv->tty_driver->ports = (struct tty_port**)kzalloc(sizeof(struct tty_port*) * ATSAM3_SPIUART_TTY_MINORS, GFP_KERNEL);
    if (!priv->tty_driver->ports) {
        LERR("tty ports allocation fail");
        return -ENOMEM;
    }

    for (ii = 0; ii < ATSAM3_SPIUART_TTY_MINORS; ii++) {
        priv->tty_driver->ports[ii] = kzalloc(sizeof(struct tty_port), GFP_KERNEL);
        if (!priv->tty_driver->ports[ii]) {
            LERR("port allocation fail");
            for (; ii >= 0; --ii) {
                kfree(priv->tty_driver->ports[ii]);
            }
            return -ENOMEM;
        }
    }

    tty_set_operations(priv->tty_driver, &atsam3_tty_ops); // Set operations for the tty driver

    result = tty_register_driver(priv->tty_driver); // Register the tty driver
    if (result) { // Check if registration failed
        LERR("tty_register_driver fail [%d]", result); // Print an error message with the failure code
        goto deinit_exit; // Jump to the deinitialization exit label
    }

    if ((result = __atsam3_tty_port_register(priv, &priv->_spi_data.entity, ATSAM3_NAME_SLC, 0))) {
        LERR("Cannot register port");
        goto deinit_exit;
    }

    if ((result = __atsam3_tty_port_register(priv, &priv->_spi_data.entity, ATSAM3_NAME_LCC, 1))) {
        LERR("Cannot register port");
        goto deinit_exit;
    }

    if ((result = __atsam3_tty_port_register(priv, &priv->_spi_data.entity, ATSAM3_NAME_PDC, 2))) {
        LERR("Cannot register port");
        goto deinit_exit;
    }

    goto exit; // Jump to the exit label

deinit_exit:
    put_tty_driver(priv->tty_driver); // Release the tty driver
exit:
    LDEXIT();
    return result; // Return the result
}

// @brief Initialize the atsam3_tty structure.
// @param priv - pointer to atsam3_private_data object
// @return pointer to tty_driver object
struct tty_driver* atsam3_tty_drv_init(struct atsam3_private_data* priv) {
    struct tty_driver* _tty = NULL;

    LDENTRY();
    // Allocate and initialize the tty driver. ->
    priv->tty_driver = tty_alloc_driver(ATSAM3_SPIUART_TTY_MINORS, TTY_DRIVER_DYNAMIC_ALLOC);
    if (!priv->tty_driver) {
        LERR("cannot allocate tty driver");
        goto tty_driver_fail;
    }
    _tty = priv->tty_driver;
    // Set the name of the driver.
    _tty->driver_name = DRIVER_NAME;
    
    // Set the name of the device.
    _tty->name = ATSAM3_TTY_NAME;
    
    // Set the ID of the minor number.
    _tty->minor_start = 0;//ATSAM3_SPIUART_TTY_ID;
    
    // Set the type of the device (serial).
    _tty->type = TTY_DRIVER_TYPE_SERIAL;

    _tty->num = ATSAM3_SPIUART_TTY_MINORS;
    
    // Set the subtype of the device (normal serial).
    _tty->subtype = SERIAL_TYPE_NORMAL;
    
    // Set flags for the tty driver.
    // this change behavior of tty_register_driver and tty_register_device functions
    _tty->flags = TTY_DRIVER_REAL_RAW | TTY_DRIVER_DYNAMIC_DEV | TTY_DRIVER_UNNUMBERED_NODE;
    
    // Initialize terminal settings.
    _tty->init_termios = tty_std_termios;

    _tty->ops = &atsam3_tty_ops;
    
    if (_atsam3_tty_port_init(priv)) {
        // Error: unable to initialize port.
        goto tty_driver_fail;
    }
    goto exit;
    
tty_driver_fail:
    // Free memory for the atsam3_tty structure.
    kfree(_tty);
    _tty = NULL;
exit:
    LDEXIT();
    return _tty;
}

// @brief atsam3 read callback, called when atsam3 raise interrupt
// with SPI_CPR_IRQ_STATE_APPL_RECEIVE_DATA.
// For reading purposes flip buffer is used.
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to atsam3_spi_entity object
// @return count of bytes, negative otherwise
int atsam3_tty_read_clb(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity) {
    int result = 0;
    spiCprSlaveTeleTransfer_type receiveTele;
    sioChar_type sioChar;
    uint32_t timestamp;
    int ii = 0;
    uint8_t status = 0;

    LDBG("entity->id[%d]", entity->id);
    // send request for acquiring data from atsam3
    if (entity->sync_response.slc.rcvSize == 0) {
        LDBG("Nothing received");
        goto exit;
    }
    if (entity->sync_response.slc.rcvSize > SPI_CPR_MAX_DATA_BLOCK_SIZE) {
        LDBG("Invalid receive size");
        goto exit;
    }
    result = atsam3_spi_get_recv_data(priv, entity, entity->sync_response.slc.rcvSize, &receiveTele);
    if (result) {
        LERR("Get receive data failed!");
        goto exit;
    }

    if (receiveTele.noOfChar > 0) {
        timestamp = receiveTele.timestamp;
        status = (receiveTele.status & SPI_CPR_TIMESTAMP_NO_GAP_MASK);
        for (ii = 0; ii < receiveTele.noOfChar; ii++) {
            sioChar.status = status;
            sioChar.character = receiveTele.byte[ii];
            sioChar.timeStamp = timestamp;

            if ((status == SIO_RTU_STATUS_OK) || (status == SIO_RTU_STATUS_RCV_TIMEOUT)) {
                entity->counter.rx += 1;        
            }
            else if (status == SIO_RTU_STATUS_PARITY_ERR) {
                entity->counter.parity += 1;
            }
            else if (status == SIO_RTU_STATUS_FRAMING_ERR) {
                entity->counter.frame += 1;
            }
            else {
                entity->counter.overrun += 1;
            }
        }
        // push atsam3 received data to tty flip buffer
        if (tty_buffer_space_avail((struct tty_port*)entity->port) < receiveTele.noOfChar) {
            LERR("No space available in flip buffer");
            result = -ENOBUFS;
            goto exit;
        }
        if (tty_insert_flip_string((struct tty_port*)entity->port,
                    (const unsigned char*) receiveTele.byte,
                    receiveTele.noOfChar) == receiveTele.noOfChar) {
            tty_flip_buffer_push((struct tty_port*)entity->port);
        }
        else {
            LERR("TTY insert string failed!");
            result = -ENOSPC;
        }
        LDBG("data received[%x]", receiveTele.noOfChar);
    }

exit:
    LDEXIT();
    return result;
}

// @brief remove tty driver and devices from linux kernel at exit
// of driver
// @param priv - pointer to atasm3_private_data object
// @return 0 on success, negative otherwise
int atsam3_tty_remove(struct atsam3_private_data* priv) {
    int result = 0;
    int ii = 0;
    
    LDENTRY();
    if (!priv->tty_driver) {
        result = -EINVAL;
        goto exit;
    }

    for (ii = 0; ii < ATSAM3_SPIUART_TTY_MINORS; ii++) {
        LDBG("Destroy port[%d]", ii);
        tty_port_destroy(priv->tty_driver->ports[ii]);
        tty_unregister_device(priv->tty_driver, ii);
    }

    if (priv->fb_pin) {
        if (priv->fb_timer.function && hrtimer_is_hres_active(&priv->fb_timer)) {
        LDBG("terminating hrtimer");
        hrtimer_cancel(&priv->fb_timer);
        priv->fb_timer.function = NULL;
        }
    }

    tty_unregister_driver(priv->tty_driver);
    put_tty_driver(priv->tty_driver);
    priv->tty_driver = NULL;

exit:
    LDEXIT();

    return result;
}

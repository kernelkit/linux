/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 *
 * Project Common Technology Linux
 * Subsystem Atmel SAM3 SPI UART bridge driver
 */

/**
 * @file
 * Driver probe and remove functions defintion.
 */

#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/gpio.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/uaccess.h>
#include <linux/atomic.h>
#include <linux/of_irq.h>
#include <linux/firmware.h>
#include <linux/delay.h>
#include "atsam3-spiuart-log.h"
#include "atsam3-spiuart.h"
#include "atsam3-spiuart-spi.h"
#include "atsam3-spiuart-fw.h"
#include "atsam3-spiuart-tty.h"
#include "spiCprDefines.h"

static int atsam3_spiuart_spi_probe(struct spi_device* spi);
static int atsam3_spiuart_spi_remove(struct spi_device* spi);

// @brief atsam3 spiuart id table
static const struct spi_device_id atsam3spiuart_id_table [] = {
    {
        .name       = "atsam3spiuart",
        .driver_data    = 0,
    },
    { }
};

// @brief register atsam3 device table in spi subsystem
MODULE_DEVICE_TABLE(spi, atsam3spiuart_id_table);


// @brief compatible atsam3 entry for dts
static const struct of_device_id he_atsam3_spiuart_of [] = {
    { .compatible = "hitachi,atsam3-spiuart" },
    { /* placeholder */ }
};

// @brief register atsam3 of id in dts subsystem
MODULE_DEVICE_TABLE(of, he_atsam3_spiuart_of);

// @brief atsam3 spi_driver descriptor
static struct spi_driver atsam3spiuart_spi_driver = {
    .driver = {
        .name = DEVICE_NAME,
        .of_match_table = he_atsam3_spiuart_of,
        .pm = NULL,
    },

    .id_table = atsam3spiuart_id_table,
    .probe = atsam3_spiuart_spi_probe,
    .remove = atsam3_spiuart_spi_remove,
};



// @brief handles interrupts at application level, called by kernel when
// interrupt occures in system
// @param  irq The interrupt number that triggered this handler.
// @param  dev A pointer to private data structure containing device-specific information.
// @return IRQ_HANDLED
static irqreturn_t atsam3_irq_handler(int irq, void* dev) {
    struct atsam3_spi_entity* entity = (struct atsam3_spi_entity*)dev;
    struct atsam3_private_data* priv = container_of(entity, struct atsam3_private_data, _spi_data.entity);
    irqreturn_t result = IRQ_HANDLED;

    LDENTRY();
    disable_irq_nosync(irq);
    queue_work(priv->wq, &entity->work);
    LDEXIT();

    return result;
}

// @brief workqueue functions called by interrupt service routine
// @param work - pointer to work_struct object passed by kernel
static void atsam3_irq_wq(struct work_struct* work) {
    struct atsam3_spi_entity* entity = container_of(work, struct atsam3_spi_entity, work);
    struct atsam3_private_data* priv = container_of(entity, struct atsam3_private_data, _spi_data.entity);

    LDENTRY();
    if (entity->irq < 0) {
        goto exit;
    }
    if (priv && (gpio_get_value(entity->irq_gpio) == 0)) {
        // Read and check the irq flag value
        if ((atsam3_spi_get_irq_status(priv, entity) == 0)) {
            LDBG("devId[%x] reqResult[%x] irqStatus[%x] procState[%x] respType[%x]",
            entity->sync_response.devId,
            entity->sync_response.reqResult,
            entity->sync_response.irqStatus,
            entity->sync_response.procState,
            entity->sync_response.respType);
            entity->state->_irq_stat_handler(entity);
        }
        if (gpio_get_value(entity->irq_gpio) == 0) {
            queue_work(priv->wq, work);
            goto exit;
        }
    }
    enable_irq(entity->irq);

exit:
    LDBG("up lock flag");
    up(&priv->lock_flag);
    LDEXIT();
}

// @brief bootloader specific interrupt service routine, executed in atsam3_irq_wq
// function
// @param entity - pointer to entity object
static int _atsam3_irq_status_handler_btl(void* entity) {
    int result = 0;

    LDENTRY();

    LDEXIT();
    return result;
}

// @brief application specific interrupt service routine, executed in atsam3_irq_wq
// function
// @param ent - pointer to entity object
static int _atsam3_irq_status_handler_app(void* ent) {
    int result = 0;
    struct atsam3_spi_entity* entity = (struct atsam3_spi_entity*) ent;
    struct atsam3_private_data* private = container_of(entity, struct atsam3_private_data, _spi_data.entity);

    LDENTRY();
    if (entity->sync_response.irqStatus & SPI_CPR_IRQ_STATE_APPL_RECEIVE_DATA) {
        result = atsam3_tty_read_clb(private, entity);
    }
    else if (entity->sync_response.irqStatus &
        (SPI_CPR_IRQ_STATE_APPL_TRANSMIT_FINISHED | SPI_CPR_IRQ_STATE_APPL_REQUEST_RESULT)) {
        LDBG("updating modLine reqResult[%x] irqStatus[%x] procState[%x] respType[%x] SLC modLine[%x]",
        entity->sync_response.reqResult,
        entity->sync_response.irqStatus,
        entity->sync_response.procState,
        entity->sync_response.respType,
        entity->sync_response.slc.modLine);
        entity->req_result = entity->sync_response.reqResult;
        entity->modem_line_status = entity->sync_response.slc.modLine;
    }
    LDEXIT();
    return result;
}


// @brief   Initializes private data structure for ATSAM3.
// @param   spi - The SPI device.
// @return  0 on success, -ENOMEM if memory allocation fails.
static int atsam3_init_private_data(struct spi_device *spi) {
    int result = 0;
    struct atsam3_private_data* priv;
    struct device_node* dnode = spi->dev.of_node;

    LDENTRY();

    // Allocate memory for private data structure.
    priv = kzalloc(sizeof(struct atsam3_private_data), GFP_KERNEL);
    if (!priv) {
        LERR("no memory");
        result = -ENOMEM;
        goto exit;
    }

    sema_init(&priv->lock_flag, 1);

    priv->wq = alloc_workqueue("atsam3_workqueue", WQ_UNBOUND | WQ_MEM_RECLAIM, 0);
    if (!priv->wq) {
        LERR("Workqueue allocation failed");
        result = -ENOMEM;
        goto dealloc_priv;
    }

    // Sets the initial state of the IRQ flags to 0.
    atomic_set_release(&priv->open_counter, 0);

    struct atsam3_spi_entity* entity = &priv->_spi_data.entity;
    entity->reset_gpio = NULL;
    entity->irq = -1;
    if (of_property_read_u32(dnode, "serialid", &entity->id)) {
        LERR("Fail to get serialid property!");
        result = -ENOMEM;
        goto dealloc_wq;
    }

    // Sets the SPI device driver data.
    spi_set_drvdata(spi, priv);
    priv->_spi_data.spi_device = spi;

    entity->_boot_state._irq_stat_handler = &_atsam3_irq_status_handler_btl;
    entity->_boot_state._transmit_data_handler = NULL;
    entity->_app_state._irq_stat_handler = &_atsam3_irq_status_handler_app;
    entity->_app_state._transmit_data_handler = NULL;
    entity->state = NULL;
    INIT_WORK(&entity->work, atsam3_irq_wq);
    atomic_set_release(&entity->is_open, 0);
    atomic_set_release(&entity->is_transmitting, 0);
    LDEXIT();
    return result;

dealloc_wq:
    destroy_workqueue(priv->wq);

dealloc_priv:
    kfree(priv);

exit:
    LDEXIT();
    return result;
}

// @brief type definition of interrupt callback declaration
typedef irqreturn_t (*atsam3_irq_handler_clb)(int irq, void* dev);

// @brief register specific irq handler for interrupt source
// @param priv - pointer to atasm3_private_data
// @param clb - pointer to interrupt function callback
// @return 0 on success, negative otherwise
static int _atsam3_register_irq_handler(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity, atsam3_irq_handler_clb clb){
    int result = 0;

    LDENTRY();
    LDBG("Request irq[%d]", entity->irq);
    result = request_irq(entity->irq,
                clb,
                IRQF_ONESHOT,
                "atsam3_irq_handler",
                entity);

    if (result) {
        LERR("Request irq[%d] failed", entity->irq);
        goto exit;
    }
exit:
    LDEXIT();
    return result;
}

// @brief get interrupt pins for atmel sam3 device from dts file
// @param priv - pointer to atsam3_private_data object
// @return 0 on success, negative otherwise
int atsam3_get_irq_pins(struct atsam3_private_data* priv) {
    int result = -ENOENT;
    int csio = 0;
    struct device_node* dnode = priv->_spi_data.spi_device->dev.of_node;

    LDENTRY();
    if (!dnode) {
        LERR("cannot find of_node!");
        goto exit;
    }

    csio = of_get_named_gpio(dnode, "interrupt-gpios", 0);
    if (csio < 0) {
        LERR("Could not find interrupt gpio");
        goto exit;
    }
    if (gpio_is_valid(csio)) {
        priv->_spi_data.entity.irq = gpio_to_irq(csio);
        priv->_spi_data.entity.irq_gpio = csio;
    }
    result = 0;

exit:
    LDEXIT();
    return result;
}

// @brief assign application allback for interrupt pins
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to astam3_spi_entity object
int atsam3_init_irqs(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity) {
    LDENTRY();
    _atsam3_register_irq_handler(priv, entity, atsam3_irq_handler);
    LDEXIT();
    return 0;
}

// @brief deinitialize interrupt pins
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to astam3_spi_entity object
void atsam3_deinit_irqs(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity) {
    LDENTRY();
    if (entity->irq > 0) {
        free_irq(entity->irq, entity);
    }
    entity->irq = -1;
    memset(&entity->sync_response, 0, sizeof(spiCprSlaveResponse_type));

    LDEXIT();
}

// @brief deinitialize private data allocated at probe function
// @param priv - pointer to atsam3_private_data object
static void _atsam3_deinit_private_data(struct atsam3_private_data* priv) {
    LDENTRY();
    if (priv) {
        if (priv->wq) {
            destroy_workqueue(priv->wq);
            priv->wq = NULL;
        }
        kfree(priv);
        priv = NULL;
    }
    LDEXIT();
}

// @brief atsam3 feedback pin toggler, called by kernel when hrtimer
// elapse
// @param timer - pointer to atsam3 hrtimer structure
// @return HRTIMER_RESTART
static enum hrtimer_restart _atsam3_fb_timer_clb(struct hrtimer* timer) {
    // check does system is initialized
    struct atsam3_private_data* priv = NULL;
    priv = container_of(timer, struct atsam3_private_data, fb_timer);

    LDENTRY();
    if (priv && priv->fb_pin) {
        // get current value of fb pin and toggle it
        if(gpiod_get_raw_value(priv->fb_pin) != 0) {
            gpiod_set_value(priv->fb_pin, 0);
        }
        else {
            gpiod_set_value(priv->fb_pin, 1);
        }
    }
    // forward to system current value of fb timer
    hrtimer_forward_now(&priv->fb_timer,priv->fb_timer_period);
    LDEXIT();

    return HRTIMER_RESTART;
}

// @brief method responsible for starting atsam3 hrtimer
// @param priv - pointer to atsam3_private_data object
int atsam3_start_timer_fb(struct atsam3_private_data* priv) {
    int result = 0;

    LDENTRY();
    // initialize period value
    if (priv->fb_pin && !priv->fb_timer.function) {
        priv->fb_timer_period = ktime_set(60, 0);
        hrtimer_init(&priv->fb_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        // set function callback
        priv->fb_timer.function = _atsam3_fb_timer_clb;
        // start hrtimer
        hrtimer_start(&priv->fb_timer, priv->fb_timer_period, HRTIMER_MODE_REL);
    }
    LDEXIT();

    return result;
}

// @brief read gpio information from dts and initialize atsam3_private_data
// gpio fields
// @param atsam3 - pointer to atsam3_private_data
// @return 0 on success, negative error otherwise
static int _atsam3_spiuart_gpio_init(struct atsam3_private_data* atsam3) {
    int result = -ENOENT;
    int ii = 0;
    int pin = 0;
    struct gpio_desc* gdesc;
    // get pointer to spi specific data
    struct atsam3_spi_data* spi_data = &atsam3->_spi_data;
    // get handler to dts node
    struct device_node* node = spi_data->spi_device->dev.of_node;

    LDENTRY();
    if (!node) {
        LERR("Cannot find of_node!");
        goto exit;
    }

    // get specific reset pin
    pin = of_get_named_gpio(node, "reset-gpios", 0);
    if (pin < 0) {
        LERR("Could not find reset-gpio[%d]", spi_data->entity.id);
        goto exit;
    }
    // check does pin is valid
    if (gpio_is_valid(pin)) {
        // it is it get reset pin gpio descriptor pointer
        gdesc = devm_gpiod_get_index_optional(&spi_data->spi_device->dev,
                "reset", ii, GPIOD_OUT_LOW);
        if (IS_ERR(gdesc)) {
            result = PTR_ERR_OR_ZERO(gdesc);
            LERR("Cannot get reset-gpio id[%d] err[%x]", spi_data->entity.id, PTR_ERR_OR_ZERO(gdesc) * (-1));
            goto exit;
        }
        else {
            LDBG("Get reset-gpio id[%x] gdesc[%p]", spi_data->entity.id, gdesc);
                spi_data->entity.reset_gpio = gdesc;
        }
    }
    // get feedback gpio pin, there is only one driver instance which drives whole time feedback pin
    pin = of_get_named_gpio(node, "fb-gpios", 0);
    // check does feedback gpio is valid
    if (gpio_is_valid(pin)) {
        // get feedback pin gpio descriptor
        gdesc = devm_gpiod_get_index_optional(&spi_data->spi_device->dev,
                 "fb", 0, GPIOD_OUT_LOW);
        if (IS_ERR(gdesc)) {
            result = PTR_ERR_OR_ZERO(gdesc);
            LERR("Get fb gpio failed, err[%x]", PTR_ERR_OR_ZERO(gdesc) * (-1));
            goto exit;
        }
        else {
            atsam3->fb_pin = gdesc;
        }
    }
    result = 0;
exit:
    LDEXIT();
    return result;
}

// @brief atsam3_spiuart_spi_probe: This function probes for SPI interface and initializes it.
// Automatically called by kernel when appropiate spi device in dts was found
// @param spi: The struct spi_device that represents the SPI device.
// @return 0 on success, negative otherwise
static int atsam3_spiuart_spi_probe(struct spi_device* spi) {
    int result = 0;
    struct atsam3_private_data* priv;

    // Initialize private data and assign pointer to private data
    result = atsam3_init_private_data(spi);
    if (result) {
        LERR("init_private_data_error %d", result);
        goto exit;
    }
    // get assigned private data
    priv = spi_get_drvdata(spi);
    // get assingned gpios for spi from dts
    result = _atsam3_spiuart_gpio_init(priv);
    if (result) {
        LERR("Cannot initialize gpio!");
        goto deinit_private;
    }

    // Initialize tty driver
    if (!atsam3_tty_drv_init(priv)) {
        LERR("Cannot initialize tty!");
        result = -EAGAIN;
        goto deinit_private;
    }
   
    // Set up the SPI device
    spi->bits_per_word = ATSAM3_SPI_CFG_BPW;
    spi->mode = SPI_MODE_1;
    spi->max_speed_hz = 12000000;

    // setup spi controller mode, bpw and speed
    result = spi_setup(spi);
    if (result) {
        LERR("spi_setup failed(%d)", result);
        return result;
    }

    atsam3_spi_drive_reset_pin(priv, &priv->_spi_data.entity, 1);

    goto exit;

deinit_private:
    // if somehing failed, deinitialize private data
    _atsam3_deinit_private_data(priv);

exit:
    LDEXIT();
    return result;
}

// @brief callback for removing spi device from system
// called by kernel when rmmod is done
// @param spi - pointer to spi device
static int atsam3_spiuart_spi_remove(struct spi_device* spi) {
    struct atsam3_private_data* priv = NULL;

    LDENTRY();
    priv = spi_get_drvdata(spi);
    if (priv) {
        atsam3_tty_remove(priv);
        _atsam3_deinit_private_data(priv);
        atsam3_spi_remove(priv);
    }
    LDEXIT();
    return 0;
}

module_spi_driver(atsam3spiuart_spi_driver);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);
MODULE_LICENSE(DRIVER_LICENSE);
MODULE_INFO(Version, DRIVER_INFO);

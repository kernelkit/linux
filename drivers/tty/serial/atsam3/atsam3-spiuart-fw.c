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

#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/crc32.h>
#include "atsam3-spiuart-fw.h"
#include "atsam3-spiuart-spi.h"

// @brief seek firmware file pointer
// @param fw - pointer to request filesystem firmware
// @param seek - shift in bytes
// @return shifted addess, null pointer otherwise
static const u8* _atsam3_fw_seek(const struct firmware* fw, int seek) {

    if (fw && fw->data && fw->size && (fw->size > seek)) {
        const u8* _ptr;
        _ptr = fw->data + seek;
        return _ptr;
    }
    return NULL;
}


// @brief read user specific firmware image from file system
// @param atsam3 - pointer to atsam3_private_data
// @param entity - pointer to specific atsam3_spi_entity object
// @param fw - pointer to pointer for storing 
// @return 0 on success, negative otherwise
static int _atsam3_fw_get_from_fs(struct atsam3_private_data* atsam3,
              struct atsam3_spi_entity* entity,
              const struct firmware** fw,
              const char* user_path) {
    int result = -EINVAL;

    LDENTRY();

    if (request_firmware(fw, user_path, &atsam3->_spi_data.spi_device->dev)) {
        LERR("Cannot request_firmware[%s]", user_path);
    }
    else {
        if (entity) {
            entity->_fw_info.fw_size = entity->_fw_info.fw->size;
        }
        result = 0;
    }

    LDEXIT();
    return result;
}

// @brief function responsible for updating firmware flash area at atmel sam3
// device
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param area - flash area which will be updated
// @return 0 on success, negative otherwise
static int _atsam3_fw_proceed_update(struct atsam3_private_data* priv,
                  struct atsam3_spi_entity* entity,
                  spiCprFlashAreaType_enum area) {
    int result = 0;
    uint32_t writtenBytes = 0;
    uint32_t bytesToCopy = 0;
    uint32_t blockNo = 0;
    uint8_t fileBuffer[SPI_CPR_SIZE_PER_BLOCK_NUMBER];
    uint8_t* ptr;

    LDENTRY();

    // check does written bytes are lower then firmware sie
    while (writtenBytes < entity->_fw_info.fw->size) {
        // fill file buffer
        bytesToCopy = SPI_CPR_SIZE_PER_BLOCK_NUMBER;
        memset(fileBuffer, 0, SPI_CPR_SIZE_PER_BLOCK_NUMBER);
        // seek pointer to new position at file
        ptr = (uint8_t*)_atsam3_fw_seek((struct firmware*)entity->_fw_info.fw, writtenBytes);
        // if bytesToCopy overwritten file size, trim it
        if ((writtenBytes + bytesToCopy) >= entity->_fw_info.fw->size) {
            bytesToCopy = (entity->_fw_info.fw->size - writtenBytes);
        }
        // copy files to buffer
        memcpy(fileBuffer, ptr, bytesToCopy);
        // perform spi send data block
        result = atsam3_spi_send_data_block(priv,
                      entity,
                      SPI_CPR_REQ_TYPE_SET_FLASH_BLOCK_INFO,
                      blockNo,
                      bytesToCopy,
                      area,
                      (uint8_t*)fileBuffer);
        // increase written bytes counter
        writtenBytes += bytesToCopy;
        blockNo += 1;
        msleep(1);
    }

    LDEXIT();
    return result;
}

// @brief read firmware information from user space binary image
// @param priv - pointer to atsam3_private_data object
// @param fw - pointer to firmware structure represtend in linux kernel
// @param fwInfo - pointer to spiCprSlaveFwInformation_type object
// @return 0 on success, negative otherwise
static int _atsam3_fw_get_info(struct atsam3_private_data* priv,
               const struct firmware* fw,
               spiCprSlaveFwInformation_type* fwInfo) {
    int result;
    const u8 * ptr = NULL;
    result = -ENOENT;

    LDENTRY();

    // seek to image MAJOR_VERSION
    ptr = _atsam3_fw_seek(fw, ATSAM3_BLR_FW_MAJOR_VERSION_OFFSET);
    if (!ptr) {
        goto exit;
    }

    memcpy((void*)&fwInfo->version.major, (void*)ptr, ATSAM3_BLR_FW_VERSION_LENGTH);
    
    // seek to image TYPE
    ptr = _atsam3_fw_seek(fw, ATSAM3_BLR_FW_TYPE_OFFSET);
    if (!ptr) {
        goto exit;
    }

    memcpy((void*)&fwInfo->type, (void*)ptr, ATSAM3_BLR_FW_VERSION_LENGTH);

    // seek to firmware size offset
    ptr = _atsam3_fw_seek(fw, ATSAM3_BLR_FW_SIZE_OFFSET);
    if (!ptr) {
        goto exit;
    }

    fwInfo->size = *(uint32_t*)ptr;

    // seek to begin of image
    ptr = _atsam3_fw_seek(fw, 0);
    if (!ptr) {
        goto exit;
    }
    
    // calculate crc sum for overall image
    fwInfo->hash = ~crc32(0xffFFffFF, ptr, fwInfo->size);

    result = 0;
exit:
    LDEXIT();
    return result;
}

// @brief perform firmware update is it necessary
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param devFwInfo - pointer to firmware stored in device
// @return 0 on success, negative otherwise
static int _atsam3_fw_update(struct atsam3_private_data* priv,
             struct atsam3_spi_entity* entity,
             spiCprSlaveFwInformation_type* devFwInfo) {
    int result = 0;
    spiCprSlaveFwInformation_type* fileFwInfo = &entity->_fw_info.fwInfo;

    LDENTRY();

    LDBG("\ndev major[%x] dev type[%x] dev crc[%x] dev size[%d]\nmajor[%x] type[%x] crc[%x] size[%d]",
            devFwInfo->version.major, devFwInfo->type, devFwInfo->hash, devFwInfo->size,
            fileFwInfo->version.major, fileFwInfo->type, fileFwInfo->hash, fileFwInfo->size
    );

    if ((devFwInfo->type != fileFwInfo->type) ||
        (devFwInfo->version.major != fileFwInfo->version.major) ||
        (devFwInfo->version.minor != fileFwInfo->version.minor) ||
        (devFwInfo->version.build != fileFwInfo->version.build) ||
        (devFwInfo->version.type != fileFwInfo->version.type) ||
        (devFwInfo->size != fileFwInfo->size) ||
        (devFwInfo->hash != fileFwInfo->hash)) {
        LDBG("Device firmware attributes are different");
        result = _atsam3_fw_proceed_update(priv, entity, SPI_CPR_FLASH_AREA_APPLICATION);
        if (!result) {
            LDBG("Updating devFwInfo");
            memcpy(devFwInfo, fileFwInfo, sizeof(spiCprSlaveFwInformation_type));
        }
    }
    if (entity->_fw_info.fw) {
        // release firmware when it was not used
        release_firmware(entity->_fw_info.fw);
        entity->_fw_info.fw = NULL;
    }

    LDEXIT();
    return result;
}

// @brief perform start of atsam3 bootloader
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @return 0 on success, negative otherwise
int atsam3_fw_btl_start(struct atsam3_private_data* priv,
            struct atsam3_spi_entity* entity) {
    int result = -ENOENT;

    LDENTRY();

    entity->state = &entity->_boot_state;

    result = atsam3_get_irq_pins(priv);
    if (result) {
        LERR("Get irq pins failed!");
        goto exit;
    }

    result = atsam3_init_irqs(priv, entity);
    if (result) {
        LERR("Could not initialize irqs");
    }

exit:
    LDEXIT();
    return result;
}

// @brief gather information about loaded firmware at atmel and expected
// by user, if they are different perform application update
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @return 0 on success, negative otherwise
int atsam3_fw_app_update(struct atsam3_private_data* priv,
             struct atsam3_spi_entity* entity) {
    int result = 0;
    spiCprSlaveFwInformation_type devFwInfo;
    spiCprSlaveFwInformation_type fileFwInfo;
    DECLARE_STD_INFO_BUFFER(info)

    LDENTRY();

    result = atsam3_spi_get_standard_info(priv, entity, info);
    if (result) {
        LERR("Could not get standard info");
        goto exit;
    }

    result = atsam3_spi_get_irq_status(priv, entity);
    if (result) {
        LERR("Could not get irq status");
        goto exit;
    }

    result = atsam3_spi_get_fw_info(priv, entity, SPI_CPR_FLASH_AREA_APPLICATION, &devFwInfo);
    if (result) {
        LERR("Could not take loaded fw info");
        goto exit;
    }

    result = _atsam3_fw_get_from_fs(priv, entity, &entity->_fw_info.fw, entity->_fw_info.user_path);
    if (result) {
        LERR("Get application from fs failed");
        goto exit;
    }

    result = _atsam3_fw_get_info(priv, entity->_fw_info.fw, &entity->_fw_info.fwInfo);
    if (result) {
        LERR("Could not get fw info");
        goto exit;
    }

    // update
    result = _atsam3_fw_update(priv, entity, &devFwInfo);
    if (result) {
        LERR("Application update failed");
        goto exit;
    }

    result = atsam3_spi_set_dev_fw_info(priv, entity, &devFwInfo);
    if (result) {
        LERR("Could not set device firmware info!");
        goto exit;
    }

exit:
    LDEXIT();
    return result;
}

// @brief get atmel sam3 firmware paths from dts
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to atsam3_spi_entity object
// @return 0 on success, negative otherwise
int atsam3_fw_get_dts_path(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity) {
    int result = -ENOENT;
    struct device_node* node = NULL;

    LDENTRY();

    // get dts node handler
    node = priv->_spi_data.spi_device->dev.of_node;
    if (!node) {
        LERR("Cannot find of of_node");
        goto exit;
    }

    node = node->parent;

    if (!node) {
        LERR("Parent does not exit at dts!");
        goto exit;
    }

    LDBG("entity->fw_name[%s]", entity->fw_name);
    if (of_property_read_string(node, (const char*)entity->fw_name, &entity->_fw_info.user_path)) {
        LERR("Read %s path from dts failed");
        goto exit;
    }
    result = 0;

exit:
    LDEXIT();
    return result;
}

// @brief perform firmware start procedure at atsam3
// @param priv - pointer to atsam3_private_data object
// @param entity_id - id of atsam3 mcu to start
// @return 0 on success, negative otherwise
int atsam3_fw_app_start(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity) {
    int result = 0;
    DECLARE_STD_INFO_BUFFER(info);

    LDENTRY();

    LDBG("Deinit bootloader irqs...");
    // deinitialize bootloader interrupts
    msleep(1);
    atsam3_deinit_irqs(priv, entity);

    entity->state = &entity->_app_state;

    // read atsam3 interrupt pins
    result = atsam3_get_irq_pins(priv);
    if (result) {
        LERR("Get irq pins failed!");
        goto exit;
    }

    LDBG("Starting firmware...");
    // send start firmware request to atmel
    result = atsam3_spi_start_fw(priv, entity);
    if (result) {
        LERR("Could not start firmware!");
        goto exit;
    }

    LDBG("Configuring firmware...");
    // get firmware standard info
    result = atsam3_spi_get_standard_info(priv, entity, info);
    if (result) {
        LERR("Could not get standard info!");
        goto exit;
    }
    // set spi cpr slave device id
    result = atsam3_spi_set_dev_id(priv, entity, SPI_CPR_SLAVE_DEVICE_ID_1 + entity->id);
    if (result) {
        LERR("Could not set device id!");
        goto exit;
    }

    // get standard info for checking result of previous command
    result = atsam3_spi_get_standard_info(priv, entity, info);
    if (result) {
        LERR("Could not get standard info!");
        goto exit;
    }
    // if set device id failed, return error
    if (((info->procState & SPI_CPR_PROCESSING_STATE_MASK) != SPI_CPR_PROC_STATE_APPL_STARTUP) ||
        (info->devId != SPI_CPR_SLAVE_DEVICE_ID_1 + entity->id )) {
        LERR("Unexpected application state or device id!");
        LERR("info->procState[%x] != SPI_CPR_PROC_STATE_APPL_STARTUP[%x]", info->procState, SPI_CPR_PROC_STATE_APPL_STARTUP);
        LERR("devId[%x] procState[%x] respType[%x] irqStatus[%x] reqResult[%x]",
            info->devId, info->procState, info->respType, info->irqStatus, info->reqResult);
        result = -ENODEV;
        goto exit;
    }
    // assign application interrupt callback
    atsam3_init_irqs(priv, entity);
    msleep(1);
    // start timer feedback pin
    result = atsam3_start_timer_fb(priv);
    if (result) {
        result = -ETIME;
        LERR("Start feedback timer failed!");
        goto exit;
    }

    result = atsam3_spi_change_drv_state(priv, entity, SPI_CPR_STATE_CHANGE_START_UART_CONFIGURATION);
    if (result) {
        result = -EBADE;
        LERR("Change driver state request failed");
        goto exit;
    }

    msleep(1);

    // get standard info for checking result of previous command
    result = atsam3_spi_get_standard_info(priv, entity, info);
    if (result) {
        LERR("Could not get standard info!");
        goto exit;
    }

    if (info->procState != SPI_CPR_PROC_STATE_APPL_CONFIG) {
        result = -EBADE;
        LERR("Change state to CONFIG failed");
        goto exit;
    }
exit:
    LDEXIT();
    return result;
}


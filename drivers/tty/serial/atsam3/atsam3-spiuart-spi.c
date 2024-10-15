/*
 * Copyright 2024 Hitachi Energy. All rights reserved.
 *
 * Project Common Technology Linux
 * Subsystem Atmel SAM3 SPI UART bridge driver
 */

/**
 * @file
 * Functions responsible for managing spi communication.
 */

#include <linux/delay.h>
#include <linux/gpio.h>
#include "atsam3-spiuart.h"
#include "spiCprDefines.h"

static const char FW_SLC[] = "SLC";
static const char FW_LCC[] = "LCC";
static const char FW_PDC[] = "PDC";

struct atsam3_spi_buffer {
    uint8_t* buffer;
    size_t buffer_size;
};

// @brief
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to atsam3_spi_entity object
// @param response - pointer to response object from atsam3
static int _atsam3_spi_handle_response(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity, spiCprSlaveResponse_type* response) {
    return gpio_get_value(entity->irq_gpio);
}

// @brief set reset pin in specific state
// @param priv - pointer to atsam3_private_data object
// @param entity_id - id of entity to drive
// @param enable - value different than 0 drives reset pin to low, caused
// by inverted logic of reset pin
void atsam3_spi_drive_reset_pin(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity, char enable) {
    LDENTRY();
    // set current reset pin to enable/disable state
    gpiod_set_value(entity->reset_gpio, enable != 0 ? 1 : 0);
    msleep(200);
    LDEXIT();
}

// @brief send spi request to spi device via spi_sync_transfer function
// and read response immediately
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param master - pointer to master request buffer
// @param slave - pointer to slave response buffer
// @return 0 on success, negative otherwise
static int _atsam3_spi_request(struct atsam3_private_data* priv,
            struct atsam3_spi_entity* entity,
            struct atsam3_spi_buffer* master,
            struct atsam3_spi_buffer* slave) {
    int result = 0;
    struct spi_transfer transfer;

    LDENTRY();
    memset(&transfer, 0, sizeof(struct spi_transfer));
    // check does master and slave are correct
    if (master && slave &&
        (master->buffer && master->buffer_size) &&
        (slave->buffer && slave->buffer_size)) {
        //atsam3_spi_drive_cs(&priv->_spi_data, entity->id, 1);
        // fill spi_transfer object with user data
        transfer.tx_buf = master->buffer;
        transfer.rx_buf = slave->buffer;
        transfer.len = master->buffer_size;
        transfer.delay_usecs = 150;

        // start spi synchronization with kernel methods
        result = spi_sync_transfer(priv->_spi_data.spi_device, &transfer, 1);
        if (result) {
            LERR("Cannot perform spi_sync, err[%x]!", result * (-1));
        }
        //atsam3_spi_drive_cs(&priv->_spi_data, entity->id, 0);
    }
    LDEXIT();
    return result;
}

// @brief send spi request to spi device and wait on interrupt response
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param master - pointer to master request buffer
// @param slave - pointer to slave response buffer
static int _atsam3_spi_request_wait(struct atsam3_private_data* priv,
                 struct atsam3_spi_entity* entity,
                 struct atsam3_spi_buffer* master,
                 struct atsam3_spi_buffer* slave) {

    // perform spi sync
    int result = 0;

    down(&priv->lock_flag);
    result = _atsam3_spi_request(priv, entity, master, slave);

    if (down_timeout(&priv->lock_flag, msecs_to_jiffies(1000))) {
        LERR("Device is locked!");
        result = -ETIME;
    }
    return result;
}

// @brief get standard info request
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param response - pointer to response object
// @return 0 on success and fullfilled respone object, negative otherwise
int atsam3_spi_get_standard_info(struct atsam3_private_data* priv,
                 struct atsam3_spi_entity* entity,
                 spiCprSlaveResponse_type* response) {
    int result = 0;
    spiCprMasterRequest_type request;
    struct atsam3_spi_buffer slave;
    struct atsam3_spi_buffer master;

    // set request buffer data
    master.buffer = (uint8_t*)&request;
    // set request buffer size
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;
    // set request device id
    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    // set request type
    request.reqType = SPI_CPR_REQ_TYPE_GET_STANDARD_INFO;

    slave.buffer = (uint8_t*)response;
    slave.buffer_size = sizeof(spiCprSlaveResponse_type);
    
    // perform atsam3 spi request
    LDENTRY();
    result = _atsam3_spi_request(priv, entity, &master, &slave);
    if (result) {
        LERR("Cannot get standard info");
    }
    else {
        _atsam3_spi_handle_response(priv, entity, response);
    }
    LDEXIT();
    return result;
}

// @brief method responsible for getting irq status of atsam3
// device
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @return 0 on success, negative otherwise
int atsam3_spi_get_irq_status(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity) {
    int result = 0;
    spiCprSlaveResponse_type response;
    spiCprMasterRequest_type request;
    struct atsam3_spi_buffer master;
    struct atsam3_spi_buffer slave;

    LDENTRY();
    // set request buffer data
    master.buffer = (uint8_t*)&request;
    // set request buffer size
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;
    memset((void*)&request, 0, SPI_CPR_MASTER_REQUEST_SIZE);
    // set request device id
    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    // set request type
    request.reqType = SPI_CPR_REQ_TYPE_GET_IRQ_STATUS;

    memset((void*)&response, 0, SPI_CPR_SLAVE_RESPONSE_SIZE);
    slave.buffer = (uint8_t*)&response;
    slave.buffer_size = SPI_CPR_SLAVE_RESPONSE_SIZE;

    // perform atsam3 spi request
    result = _atsam3_spi_request(priv, entity, &master, &slave);
    if (result) {
        LERR("Cannot get irq status");
    }
    else {
        LDBG("DevId[%x] RespType[%x] procState[0x%02X] IrqStatus[0x%02X] ReqResult[0x%x]",
        response.devId, response.respType, response.procState, response.irqStatus, response.reqResult);
            if ( (response.respType == SPI_CPR_RESP_TYPE_NONE) || 
            (response.respType == SPI_CPR_RESP_TYPE_STANDARD_INFO) ) {
            memcpy(&entity->sync_response, &response, SPI_CPR_SLAVE_RESPONSE_SIZE);
        }
    }
    LDEXIT();
    return result;
}

// @brief acquire flashed atsam3 firmware information
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param pFwInfo - pointer to firmware information store object
// @return 0 on success, negative otherwise
int atsam3_spi_get_fw_info(struct atsam3_private_data* priv,
               struct atsam3_spi_entity* entity,
               spiCprFlashAreaType_enum area,
               spiCprSlaveFwInformation_type* pFwInfo) {
    int result = 0;
    spiCprSlaveResponse_type response;
    spiCprMasterRequest_type request;
    spiCprSlaveFwInformation_type fwInfo;
    struct atsam3_spi_buffer master;
    struct atsam3_spi_buffer slave;
    uint8_t* buffer = NULL;

    LDENTRY();
    // set request buffer data
    master.buffer = (uint8_t*)&request;
    // set request buffer size
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;
    // set request device id
    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    // set request type
    request.reqType = SPI_CPR_REQ_TYPE_GET_FIRMWARE_INFO;
    // set optional request data
    request.master.opt.data = (uint16_t)area; 

    slave.buffer = (uint8_t*)&response;
    slave.buffer_size = SPI_CPR_SLAVE_RESPONSE_SIZE;

    // perform atsam3 spi request
    result = _atsam3_spi_request(priv, entity, &master, &slave);

    // check does result is error either atsam3 is in processing state
    if (result || (response.procState & SPI_CPR_PROCESSING_BUSY_BIT)) {
        LERR("Device not ready to provide the firmware information");
        result = -EBUSY;
        goto exit;
    }
    buffer = (uint8_t*)&response;
    // now atsam3 knows the next request will be data acquire message
    // prepare buffer for it
    memset((void*)&fwInfo, 0, SPI_CPR_FW_INFORMATION_SIZE);
    buffer = (uint8_t*)&fwInfo;
    slave.buffer = (uint8_t*)&fwInfo;
    slave.buffer_size = SPI_CPR_FW_INFORMATION_SIZE;
    // perform atsam3 spi requesr
    result = _atsam3_spi_request(priv, entity, &slave, &slave);
    if (result) {
        LERR("Cannot read firmware information");
        result = -ENODATA;
        goto exit;
    }
    else {
        if (pFwInfo) {
            pFwInfo->size = fwInfo.size;
            pFwInfo->version = fwInfo.version;
            pFwInfo->type = fwInfo.type;
            pFwInfo->hash = fwInfo.hash;
        }
    }
exit:
    LDEXIT();
    return result;
}

// @brief change firmware state
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param stateChange - new state
// @return 0 on success, negative otherwise
int atsam3_spi_change_drv_state(struct atsam3_private_data* priv,
                struct atsam3_spi_entity* entity,
                spiCprStateChangeSlc_enum stateChange) {
    int result = 0;

    spiCprMasterRequest_type request;
    struct atsam3_spi_buffer master;

    spiCprSlaveResponse_type response;
    struct atsam3_spi_buffer slave;

    LDENTRY();
    memset(&request, 0, SPI_CPR_MASTER_REQUEST_SIZE);
    // set new firmware state
    request.master.opt.data = stateChange;
    // set request device id
    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    // set request type
    request.reqType = SPI_CPR_REQ_TYPE_CHANGE_STATE;
    // set request data buffer
    master.buffer = (uint8_t*)&request;
    // set request data buffer size
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;

    memset(&response, 0, SPI_CPR_SLAVE_RESPONSE_SIZE);
    slave.buffer = (uint8_t*)&response;
    slave.buffer_size = SPI_CPR_SLAVE_RESPONSE_SIZE;

    // perform atsam3 spi request
    result = _atsam3_spi_request_wait(priv, entity, &master, &slave);
    if (result) {
        LERR("Cannot send change drv state request!");
        goto exit;
    }

exit:
    LDEXIT();
    return result;
}

// @brief set atsam3 device id, used to set device id when
// firmware is started up
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param deviceId - new firmware device id
// @return 0 on success, negative otherwise
int atsam3_spi_set_dev_id(struct atsam3_private_data* priv,
              struct atsam3_spi_entity* entity,
              spiCprDeviceId_enum deviceId) {
    int result = 0;

    spiCprMasterRequest_type request;
    struct atsam3_spi_buffer master;

    spiCprSlaveResponse_type response;
    struct atsam3_spi_buffer slave;

    LDENTRY();
    memset(&request, 0, SPI_CPR_MASTER_REQUEST_SIZE);
    // set request new firmware device id
    request.master.opt.data = deviceId; 
    // set request device id
    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    // set request type
    request.reqType = SPI_CPR_REQ_TYPE_SET_DEVICE_ID;
    // set request buffer data
    master.buffer = (uint8_t*)&request;
    // set request buffer size
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;

    memset(&response, 0, SPI_CPR_SLAVE_RESPONSE_SIZE);
    slave.buffer = (uint8_t*)&response;
    slave.buffer_size = SPI_CPR_SLAVE_RESPONSE_SIZE;

    // perform atsam3 spi request
    result = _atsam3_spi_request(priv, entity, &master, &slave);
    if (result) {
        LERR("Cannot send change drv state request!");
        goto exit;
    }
    else {
        _atsam3_spi_handle_response(priv, entity, &response);
    }

    msleep(1);
    LDBG("devId[%x] procState[%x] respType[%x] irqStatus[%x] reqResult[%x]",
          response.devId, response.procState, response.respType, response.irqStatus, response.reqResult);

exit:
    LDEXIT();
    return result;
}

// @brief start firmware request, it is splitted on two parts
// CHECK_FIRMWARE and START_FIRMWARE requests
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @return 0 on success, negative otherwise
int atsam3_spi_start_fw(struct atsam3_private_data* priv, struct atsam3_spi_entity* entity) {
    int result = 0;
    struct atsam3_spi_buffer master;
    spiCprMasterRequest_type request;
    
    struct atsam3_spi_buffer slave;
    spiCprSlaveResponse_type response;

    LDENTRY();
    memset((void*)&response, 0, SPI_CPR_SLAVE_RESPONSE_SIZE);
    memset((void*)&request, 0, SPI_CPR_MASTER_REQUEST_SIZE);

    // CHECK FIRMWARE
    // set request master device id
    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    // set request type
    request.reqType = SPI_CPR_REQ_TYPE_CHECK_FIRMWARE;
    // set request optional data
    request.master.opt.data = SPI_CPR_FLASH_AREA_APPLICATION;
    // set request data buffer
    master.buffer = (uint8_t*)&request;
    // set request data buffer size
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;

    slave.buffer = (uint8_t*)&response;
    slave.buffer_size =  SPI_CPR_SLAVE_RESPONSE_SIZE;

    // perform atsam3 spi request
    result = _atsam3_spi_request(priv, entity, &master, &slave);
    if (result) {
        LERR("Check firmware request failed!");
        goto exit;
    }

    msleep(5);

    // START FIRMWARE
    memset(&request, 0, SPI_CPR_MASTER_REQUEST_SIZE);
    // set request master device id
    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    // set request type
    request.reqType = SPI_CPR_REQ_TYPE_START_FIRMWARE;
    // set request optional data
    request.master.opt.data = SPI_CPR_FLASH_AREA_APPLICATION;
    // perform atsam3 spi request
    result = _atsam3_spi_request(priv, entity, &master, &slave);
    if (result) {
        LERR("Start firmware request failed!");
        goto exit;
    }
    _atsam3_spi_handle_response(priv, entity, &response);
    msleep(200);
exit:
    LDEXIT();
    return result;
}

// @brief set firmware configuration parameter
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param paraType - type of the parameter
// @param paraSize - size of the parameter
// @param paraValue - value of new parameter
// @return 0 on success, negative otherwise
int atsam3_spi_set_cfg_para(struct atsam3_private_data* priv,
                struct atsam3_spi_entity* entity,
                spiCprConfigParaType_enum paraType,
                uint32_t paraSize,
                uint32_t paraValue) {
    int result = 0;
    struct atsam3_spi_buffer slave;
    spiCprSlaveResponse_type response;
    struct atsam3_spi_buffer master;
    spiCprMasterRequest_type request;

    LDENTRY();
    // set request type
    request.reqType = SPI_CPR_REQ_TYPE_SET_CONFIG_PARAMETER;
    // set request parameter type
    request.master.para.type = paraType;
    // set request parameter size
    request.master.para.size = paraSize;
    // set request parameter value
    request.master.para.value = paraValue;

    master.buffer = (uint8_t*)&request;
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;
    slave.buffer = (uint8_t*)&response;
    slave.buffer_size = SPI_CPR_SLAVE_RESPONSE_SIZE;

    // perform atsam3 spi request and wait for pin interrupt response
    result = _atsam3_spi_request_wait(priv, entity, &master, &slave);
    if (result) {
        LERR("Could not send config parameter request!");
    }
    _atsam3_spi_handle_response(priv, entity, &response);

    LDEXIT();
    return result;
}

// @brief set firmware expected information, if loaded firmware does not
// fit to requested by this function, firmware will not start
// copy of atSam3NSpiBlrSetDeviceFwInfo
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param pFwInfo - pointer to spiCprSlaveFwInformation_type
// @return 0 on success, negative otherwise
int atsam3_spi_set_dev_fw_info(struct atsam3_private_data* priv,
                   struct atsam3_spi_entity* entity,
                   spiCprSlaveFwInformation_type* pFwInfo) {
    int result = 0;

    LDENTRY();
    LDBG("Set firmware type[%x]", pFwInfo->type);
    // set firmware type
    result = atsam3_spi_set_cfg_para(priv, entity, SPI_CPR_CONFIG_TYPE_FIRMWARE_TYPE, sizeof(uint32_t), pFwInfo->type);
    if (result) {
        LERR("Could not set firmware type");
        goto exit;
    }
    LDBG("Set firmware size[%d]", pFwInfo->size);
    // set firmware size
    result = atsam3_spi_set_cfg_para(priv, entity, SPI_CPR_CONFIG_TYPE_FIRMWARE_SIZE, sizeof(uint32_t), pFwInfo->size);
    if (result) {
        LERR("Could not set firmware size");
        goto exit;
    }
    LDBG("Set crc hash[%x]", pFwInfo->hash);
    // set crc hash
    result = atsam3_spi_set_cfg_para(priv, entity, SPI_CPR_CONFIG_TYPE_FIRMWARE_CRC_HASH, sizeof(uint32_t), pFwInfo->hash);
    if (result) {
        LERR("Could not set crc!");
        goto exit;
    }
exit:
    LDEXIT();
    return result;
}

// @brief send data to block to atsam3 device via spi interface
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param reqType - request type of send data block
// @param blockNo - block number to send
// @param blockSize - block size to send
// @param blockType - type of the block, spiCprFlashAreaType_enum
// @param blockData - pointer to block data buffer
// @return 0 on success, negative otherwise
int atsam3_spi_send_data_block(struct atsam3_private_data* priv,
                   struct atsam3_spi_entity* entity,
                   spiCprMasterReqType_enum reqType,
                   uint32_t blockNo,
                   uint32_t blockSize,
                               uint32_t blockType,
                   uint8_t* blockData) {
    int result = 0;
    struct atsam3_spi_buffer master;
    struct atsam3_spi_buffer slave;
    spiCprSlaveResponse_type response;
    spiCprMasterRequest_type request;
    spiCprMasterBlockData_type receiveBuffer;

    LDENTRY();

    memset(&request, 0, SPI_CPR_MASTER_REQUEST_SIZE);
    // set request master device id
    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    // set user expected request type
    request.reqType = reqType;
    // set block size
    request.master.block.size = blockSize % SPI_CPR_SIZE_PER_BLOCK_NUMBER;
    // set block number
    request.master.block.no = blockNo;
    // set bock type
    request.master.block.type = blockType;

    // set request data buffer
    master.buffer = (uint8_t*)&request;
    // set request size
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;

    slave.buffer = (uint8_t*)&response;
    slave.buffer_size = SPI_CPR_SLAVE_RESPONSE_SIZE;

    // perform atsam3 spi request
    result = _atsam3_spi_request(priv, entity, &master, &slave);
    if (result) {
        LERR("Request send data block size failed");
        goto exit;
    }

    // check does atsam3 is not in processing state
    if (response.procState & SPI_CPR_PROCESSING_BUSY_BIT) {
        LERR("Device not ready to send data block(procState & SPI_CPR_PROCESSING_BUSY_BIT)");
        goto exit;
    }

    // now atsam3 is ready for processing new data block
    // set request buffer data pointer
    master.buffer = blockData;
    // set request buffer size
    master.buffer_size = blockSize;

    slave.buffer = (uint8_t*)receiveBuffer.byte;
    slave.buffer_size = SPI_CPR_MAX_DATA_BLOCK_SIZE;
 
    // perform atsam3 spi request with new data block
    result = _atsam3_spi_request(priv, entity, &master, &slave);
    if (result) {
        LERR("Transfer block data failed");
    }

exit:
    LDEXIT();
    return result;
}

// @brief send data buffer to atsam3 device via spi interface
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param reqType - request type of send data block
// @param buffSize - size of buffer
// @param buffData - pointer to data buffer
// @return 0 on success, negative otherwise
int atsam3_spi_send_data_buffer(struct atsam3_private_data* priv,
                struct atsam3_spi_entity* entity,
                spiCprMasterReqType_enum reqType,
                uint32_t buffSize,
                uint8_t* buffData) {
    int result = 0;

    LDENTRY();
    result = atsam3_spi_send_data_block(priv,
                entity,
                reqType,
                buffSize / SPI_CPR_SIZE_PER_BLOCK_NUMBER,
                buffSize,
                0,
                buffData);

    LDEXIT();
    return result;
}

// @brief set atsam3 modem ioctl status
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param ioctl - ioctl command
// @parma modemLineStatus - new modem line status
// @return 0 on success, negative otherwise
int atsam3_spi_modem_ioctl(struct atsam3_private_data* priv,
            struct atsam3_spi_entity* entity,
            spiCprIOCtl_enum ioctl,
            uint32_t* modemLineStatus) {

    int result = 0;
    spiCprSlaveResponse_type response;
    spiCprMasterRequest_type request;

    struct atsam3_spi_buffer master;
    struct atsam3_spi_buffer slave;

    LDENTRY();

    memset(&response, 0, SPI_CPR_SLAVE_RESPONSE_SIZE);
    memset(&request, 0, SPI_CPR_MASTER_REQUEST_SIZE);

    master.buffer = (uint8_t*)&request;
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;

    slave.buffer = (uint8_t*)&response;
    slave.buffer_size = SPI_CPR_SLAVE_RESPONSE_SIZE;

    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    request.reqType = SPI_CPR_REQ_TYPE_SET_IOCTL;
    request.master.opt.data = ioctl;

    result = _atsam3_spi_request_wait(priv, entity, &master, &slave);
    if (result) {
        LERR("Set modem ioctl request failed!");
        goto exit;
    }

exit:
    return result;
}

// @brief receive incoming data from atsam3 device reported by
// interrupt handler
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param rcvSize - receive buffer size, information gathered from 
// get irq status
// @param receiveTele - receive buffer
int atsam3_spi_get_recv_data(struct atsam3_private_data* priv,
            struct atsam3_spi_entity* entity,
            uint16_t rcvSize,
            spiCprSlaveTeleTransfer_type* receiveTele) {

    int result = 0;
    spiCprSlaveResponse_type response;
    spiCprMasterRequest_type request;
    struct atsam3_spi_buffer master;
    struct atsam3_spi_buffer slave;

    LDENTRY();

    memset(&response, 0, SPI_CPR_MASTER_REQUEST_SIZE);
    memset(&request, 0, SPI_CPR_MASTER_REQUEST_SIZE);
    memset(receiveTele, 0, sizeof(spiCprSlaveTeleTransfer_type));

    // set master device id
    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    // set request type
    request.reqType = SPI_CPR_REQ_TYPE_GET_RECEIVE_DATA;
    // set request block number
    request.master.block.no = rcvSize / SPI_CPR_SIZE_PER_BLOCK_NUMBER;
    // set request block size
    request.master.block.size = rcvSize % SPI_CPR_SIZE_PER_BLOCK_NUMBER;

    master.buffer = (uint8_t*)&request;
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;

    slave.buffer = (uint8_t*)&response;
    slave.buffer_size = SPI_CPR_SLAVE_RESPONSE_SIZE;

    // perform atsam3 spi request
    result = _atsam3_spi_request(priv, entity, &master, &slave);
    if (result) {
        LERR("Request get receive data failed!");
        goto exit;
    }

    // receive of telegram data is splitted on two parts
    // first is to inform about master readiness for data
    // handling
    // this is second part master receiving data
    // set receive buffer pointer
    master.buffer = (uint8_t*)receiveTele;
    // set receive buffer size
    master.buffer_size = rcvSize + SPI_CPR_TELEGRAM_HEADER_SIZE;

    // perform spi request
    result = _atsam3_spi_request(priv, entity, &master, &master);
    if (result) {
        LERR("Transfer receive data failed!");
        goto exit;
    }

exit:
    LDEXIT();
    return result;
}

// @brief method is responsible for set ioctl command at atsam3
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param ioCTL - ioctl number send to atsam3
// @param paraSize - size of parameter
// @param paraValue - parameter value
int atsam3_spi_set_ioctl(struct atsam3_private_data* priv,
             struct atsam3_spi_entity* entity,
             spiCprIOCtl_enum ioCTL,
             uint32_t paraSize,
             uint32_t paraValue) {
    int result = 0;
    spiCprMasterRequest_type request;
    struct atsam3_spi_buffer master;

    spiCprSlaveResponse_type response;
    struct atsam3_spi_buffer slave;

    LDENTRY();

    memset(&request, 0, SPI_CPR_MASTER_REQUEST_SIZE);

    request.reqType = SPI_CPR_REQ_TYPE_SET_PARAMETER_IOCTL;
    request.devId = SPI_CPR_MASTER_DEVICE_ID;
    request.master.para.type = ioCTL;
    request.master.para.size = paraSize;
    request.master.para.value = paraValue;

    master.buffer = (void*)&request;
    master.buffer_size = SPI_CPR_MASTER_REQUEST_SIZE;

    memset(&response, 0, SPI_CPR_SLAVE_RESPONSE_SIZE);

    slave.buffer = (void*)&response;
    slave.buffer_size = SPI_CPR_SLAVE_RESPONSE_SIZE;

    result = _atsam3_spi_request_wait(priv, entity, &master, &slave);
    if (result) {
        LERR("spi request wait, failed");
        goto exit;
    }
exit:
    LDEXIT();
    return result;
}

// @brief unregister spi device from kernel
void atsam3_spi_remove(struct atsam3_private_data* priv) {
    LDENTRY();
    spi_unregister_device(priv->_spi_data.spi_device);
    LDEXIT();
}

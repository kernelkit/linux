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

#ifndef ATSAM3_SPIUART_SPI_H_
#define ATSAM3_SPIUART_SPI_H_

#include "atsam3-spiuart.h"

// @brief get standard info request
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param response - pointer to response object
// @return 0 on success and fullfilled respone object, negative otherwise
int atsam3_spi_get_standard_info(struct atsam3_private_data* priv,
                    struct atsam3_spi_entity* entity,
                    spiCprSlaveResponse_type* response);

// @brief method responsible for getting irq status of atsam3
// device
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @return 0 on success, negative otherwise
int atsam3_spi_get_irq_status(struct atsam3_private_data* priv,
                    struct atsam3_spi_entity* entity);

// 
// @brief acquire flashed atsam3 firmware information
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param pFwInfo - pointer to firmware information store object
// @return 0 on success, negative otherwise
int atsam3_spi_get_fw_info(struct atsam3_private_data* priv,
                   struct atsam3_spi_entity* entity,
                   spiCprFlashAreaType_enum area,
                   spiCprSlaveFwInformation_type* pFwInfo);

// 
// @brief start firmware request, it is splitted on two parts
// CHECK_FIRMWARE and START_FIRMWARE requests
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @return 0 on success, negative otherwise
int atsam3_spi_start_fw(struct atsam3_private_data* priv,
                    struct atsam3_spi_entity* entity);

// @brief unregister spi device from kernel
// @param priv - pointer to atsam3_private_data object
void atsam3_spi_remove(struct atsam3_private_data* priv);

// @brief change firmware state
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param stateChange - new state
// @return 0 on success, negative otherwise
int atsam3_spi_change_drv_state(struct atsam3_private_data* priv,
                    struct atsam3_spi_entity* entity,
                    spiCprStateChangeSlc_enum stateChange);

// @brief set atsam3 device id, used to set device id when
// firmware is started up
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param deviceId - new firmware device id
// @return 0 on success, negative otherwise
int atsam3_spi_set_dev_id(struct atsam3_private_data* priv,
                    struct atsam3_spi_entity* entity,
                    spiCprDeviceId_enum deviceId);

// @brief set firmware expected information, if loaded firmware does not
// fit to requested by this function, firmware will not start
// copy of atSam3NSpiBlrSetDeviceFwInfo
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param pFwInfo - pointer to spiCprSlaveFwInformation_type
// @return 0 on success, negative otherwise
int atsam3_spi_set_dev_fw_info(struct atsam3_private_data* priv,
                    struct atsam3_spi_entity* entity,
                    spiCprSlaveFwInformation_type* pFwInfo);

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
                    uint32_t paraValue);

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
                   uint8_t* blockData);

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
                    uint32_t blockSize,
                    uint8_t* blockData);

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
                     spiCprSlaveTeleTransfer_type* receiveTele);

// @brief method is responsible for set ioctl command at
// atsam3
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param ioCTL - ioctl number send to atsam3
// @param paraSize - size of parameter
// @param paraValue - parameter value
int atsam3_spi_set_ioctl(struct atsam3_private_data* priv,
                    struct atsam3_spi_entity* entity,
                    spiCprIOCtl_enum ioctl,
                    uint32_t paraSize,
                    uint32_t paraValue);

// @brief set atsam3 modem ioctl status
// @param priv - pointer to atsam3_private_data object
// @param entity - pointer to specific atsam3_spi_entity object
// @param ioctl - ioctl command
// @parma modemLineStatus - new modem line status
// @return 0 on success, negative otherwise
int atsam3_spi_modem_ioctl(struct atsam3_private_data* priv,
                    struct atsam3_spi_entity* entity,
                    spiCprIOCtl_enum ioctl,
                    uint32_t* modemLineStatus);

// @brief set reset pin in specific state
// @param priv - pointer to atsam3_private_data object
// @param entity_id - id of entity to drive
// @param enable - value different than 0 drives reset pin to low, caused
// by inverted logic of reset pin
void atsam3_spi_drive_reset_pin(struct atsam3_private_data* priv,
                    struct atsam3_spi_entity* entity,
                    char enable);

// @brief hold atsam3 in reset state by setting reset pin to low
// @param spi_data - pointer to atsam3 specific spi data
void atsam3_spi_hold_in_reset_pin(struct atsam3_spi_data* spi_data);

#endif // ATSAM3_SPIUART_SPI_H_

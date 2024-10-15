/*
 * Copyright 2016-2024 Hitachi Energy. All rights reserved.
 *
 * Project Common Technology Linux
 * Subsystem Atmel SAM3 SPI UART bridge driver
 */

/**
 * @file
 * Common defines for the communication procedures used on the SPI bus
 * that connects the ATSAM3N slave device with SPI master.
 */

#ifndef SPICPRDEFINES_H
#define SPICPRDEFINES_H

/*****************************************************************************/
/* Common defines */
/*****************************************************************************/

/** SPI CPR maximum communicate buffer size. */
#define SPI_CPR_MAX_DATA_BLOCK_SIZE              320
#define SPI_CPR_SIZE_PER_BLOCK_NUMBER            256

/** SPI CPR request/response sizes */
#define SPI_CPR_MASTER_REQUEST_SIZE              sizeof(spiCprMasterRequest_type)
#define SPI_CPR_SLAVE_RESPONSE_SIZE              sizeof(spiCprSlaveResponse_type)
#define SPI_CPR_FW_INFORMATION_SIZE              sizeof(spiCprSlaveFwInformation_type)
/** SPI CPR SLC telegram header size */
#define SPI_CPR_TELEGRAM_HEADER_SIZE             8

/** SPI CPR application interrupt status (availability of data or processing results) */
#define SPI_CPR_IRQ_STATE_APPL_NONE              0x00
#define SPI_CPR_IRQ_STATE_APPL_RECEIVE_DATA      0x01
#define SPI_CPR_IRQ_STATE_APPL_TRANSMIT_FINISHED 0x02
#define SPI_CPR_IRQ_STATE_APPL_REQUEST_RESULT    0x04
#define SPI_CPR_IRQ_STATE_APPL_RESULT_MASK       0x06
/** SPI CPR BLR interrupt status (availability of data or processing results) */
#define SPI_CPR_IRQ_STATE_BLR_NONE               0x00
#define SPI_CPR_IRQ_STATE_BLR_REQUEST_RESULT     0x01
#define SPI_CPR_IRQ_STATE_BLR_WRITE_FINISHED     0x02
#define SPI_CPR_IRQ_STATE_BLR_RESULT_MASK        0x03

/** SPI CPR processing state and busy information */
#define SPI_CPR_PROCESSING_STATE_MASK            0x7F
#define SPI_CPR_PROCESSING_BUSY_BIT              0x80

/** SPI CPR SLC receive timestamp gap information */
#define SPI_CPR_TIMESTAMP_NO_GAP_MASK            0x7F
#define SPI_CPR_TIMESTAMP_NO_GAP_BIT             0x80

/** SPI CPR SLC modem line status bits */
#define SPI_CPR_MODEM_LINE_STATUS_RTS            0x01
#define SPI_CPR_MODEM_LINE_STATUS_CTS            0x02
#define SPI_CPR_MODEM_LINE_STATUS_DCD            0x04
#define SPI_CPR_MODEM_LINE_STATUS_DTR            0x08
#define SPI_CPR_MODEM_LINE_STATUS_DSR            0x10

/** SPI CPR maximum number of available device firmware types.    */
/** CAUTION: Be sure to increase this number when the enumeration */
/**          type spiCprDeviceFwType_enum is extended. */
#define SPI_CPR_MAX_NUM_OF_DEVICE_FW_TYPES       4

/* status of telegram and characters */
#define  SIO_RTU_STATUS_NONE              255
#define  SIO_RTU_STATUS_OK                  0
#define  SIO_RTU_STATUS_PARITY_ERR          1
#define  SIO_RTU_STATUS_GAP_DETECT          2
#define  SIO_RTU_STATUS_FRAMING_ERR         3
#define  SIO_RTU_STATUS_OVERRUN_ERR         4
#define  SIO_RTU_STATUS_FRAGMENTED          5
#define  SIO_RTU_STATUS_CHECKSUM_ERR        6
#define  SIO_RTU_STATUS_STOP_CHAR_ERR       7
#define  SIO_RTU_STATUS_SIZE_ERR            8
#define  SIO_RTU_STATUS_START_CHAR_ERR      9
#define  SIO_RTU_STATUS_FT_STATE_ERR       10
#define  SIO_RTU_STATUS_PDP_MOD_TYPE_ERR   11
#define  SIO_RTU_STATUS_OK_FIRST_SEGMENT   12
#define  SIO_RTU_STATUS_OK_MIDDLE_SEGMENT  13
#define  SIO_RTU_STATUS_OK_LAST_SEGMENT    14
#define  SIO_RTU_STATUS_RCV_TIMEOUT        15
#define  SIO_RTU_STATUS_END_OF_TELEGRAM    16
/* ok status for FT12 single character */
#define  SIO_RTU_STATUS_FT12_SINGLE_CHAR   64

/* baud rate defines for higher rates */
#define SIO_BAUD_460_KBD               460800
#define SIO_BAUD_750_KBD               750000
#define SIO_BAUD_921_KBD               921600
#define SIO_BAUD_1000_KBD              1000000
#define SIO_BAUD_1500_KBD              1500000
#define SIO_BAUD_1843_KBD              1843200
#define SIO_BAUD_3000_KBD              3000000
#define SIO_BAUD_3686_KBD              3686400

/*****************************************************************************/
/* Enumeration definitions */
/*****************************************************************************/

/** SPI CPR device id enumeration */
typedef enum {
    SPI_CPR_DEVICE_ID_NONE                     = 0x00,
    SPI_CPR_SLAVE_DEVICE_ID_1                  = 0x01,    /* VxBus unit number + 1 = 0 + 1 = 1 */
    SPI_CPR_SLAVE_DEVICE_ID_2                  = 0x02,    /* VxBus unit number + 1 = 1 + 1 = 2 */
    SPI_CPR_SLAVE_DEVICE_ID_3                  = 0x03,    /* VxBus unit number + 1 = 2 + 1 = 3 */
    SPI_CPR_SLAVE_DEVICE_ID_4                  = 0x04,    /* VxBus unit number + 1 = 3 + 1 = 4 */
    SPI_CPR_SLAVE_BLR_DEVICE_ID                = 0xAA,
    SPI_CPR_MASTER_DEVICE_ID                   = 0xFF
} spiCprDeviceId_enum;

/** SPI CPR application processing state enumeration */
typedef enum {
    SPI_CPR_PROC_STATE_APPL_INIT = 0,
    SPI_CPR_PROC_STATE_APPL_STARTUP,
    SPI_CPR_PROC_STATE_APPL_CONFIG,
    SPI_CPR_PROC_STATE_APPL_ACTIVE
} spiCprProcStateAppl_enum;

/** SPI CPR BLR processing state enumeration */
typedef enum {
    SPI_CPR_PROC_STATE_BLR_INIT = 0,
    SPI_CPR_PROC_STATE_BLR_BOOT,
    SPI_CPR_PROC_STATE_BLR_RUN
} spiCprProcStateBlr_enum;

/** SPI CPR SLC state change enumeration */
typedef enum {
    SPI_CPR_STATE_NO_CHANGE = 0,
    SPI_CPR_STATE_CHANGE_START_UART_CONFIGURATION,
    SPI_CPR_STATE_CHANGE_START_UART_COMMUNICATION,
    SPI_CPR_STATE_CHANGE_STOP_UART_PROCESSING
} spiCprStateChangeSlc_enum;

/** SPI CPR master request type enumeration */
typedef enum {
    SPI_CPR_REQ_TYPE_NONE = 0,
    SPI_CPR_REQ_TYPE_GET_STANDARD_INFO,
    SPI_CPR_REQ_TYPE_GET_IRQ_STATUS,
    SPI_CPR_REQ_TYPE_GET_RECEIVE_DATA,
    SPI_CPR_REQ_TYPE_GET_FIRMWARE_INFO,
    SPI_CPR_REQ_TYPE_CHANGE_STATE,
    SPI_CPR_REQ_TYPE_SET_DEVICE_ID,
    SPI_CPR_REQ_TYPE_SET_CONFIG_PARAMETER,
    SPI_CPR_REQ_TYPE_SET_TRANSMIT_SIZE,
    SPI_CPR_REQ_TYPE_SET_IOCTL,
    SPI_CPR_REQ_TYPE_SET_FLASH_BLOCK_INFO,
    SPI_CPR_REQ_TYPE_SET_TIME_SYNC,
    SPI_CPR_REQ_TYPE_CHECK_FIRMWARE,
    SPI_CPR_REQ_TYPE_START_FIRMWARE,
    SPI_CPR_REQ_TYPE_SET_PARAMETER_IOCTL
} spiCprMasterReqType_enum;

/** SPI CPR slave response type enumeration */
typedef enum {
    SPI_CPR_RESP_TYPE_NONE = 0,
    SPI_CPR_RESP_TYPE_STANDARD_INFO
} spiCprSlaveRespType_enum;

/** SPI CPR request result */
typedef enum {
    SPI_CPR_RESULT_OK = 0,
    SPI_CPR_RESULT_ERROR,
    SPI_CPR_RESULT_WRONG_PROC_STATE,
    SPI_CPR_RESULT_MODEM_HANDSHAKE_ERROR,
    SPI_CPR_RESULT_DIFF_FIRMWARE_TYPE,
    SPI_CPR_RESULT_DIFF_FIRMWARE_SIZE,
    SPI_CPR_RESULT_DIFF_FIRMWARE_HASH,
    SPI_CPR_RESULT_RESPONSE_TIMEOUT,
    SPI_CPR_RESULT_INVALID_BIT_PATTERN,
    SPI_CPR_RESULT_RCV_NOT_IN_SYNC,
    SPI_CPR_RESULT_TRANSMIT_BIT_COUNT_EVEN,
    SPI_CPR_RESULT_TOO_LESS_TRANSMIT_BYTES
} spiCprRequestResult_enum;

/** SPI CPR configuration parameter type enumeration */
typedef enum {
    SPI_CPR_CONFIG_TYPE_NONE = 0,
    SPI_CPR_CONFIG_TYPE_SIO_MODE,
    SPI_CPR_CONFIG_TYPE_HW_OPTIONS,
    SPI_CPR_CONFIG_TYPE_BAUD_RATE,
    SPI_CPR_CONFIG_TYPE_GAP_SUPV_STEPS,
    SPI_CPR_CONFIG_TYPE_CARRIER_SETUP_TIME,
    SPI_CPR_CONFIG_TYPE_CARRIER_TRAILING_TIME,
    SPI_CPR_CONFIG_TYPE_RECEIVER_TIMEOUT_TICKS,
    SPI_CPR_CONFIG_TYPE_FIRMWARE_TYPE,
    SPI_CPR_CONFIG_TYPE_FIRMWARE_SIZE,
    SPI_CPR_CONFIG_TYPE_FIRMWARE_CRC_HASH,
    SPI_CPR_CONFIG_TYPE_TRANSMIT_BAUD_RATE,
    SPI_CPR_CONFIG_TYPE_INVERT_RECEIVE,
    SPI_CPR_CONFIG_TYPE_INVERT_TRANSMIT,
    SPI_CPR_CONFIG_TYPE_CARRIER_SETUP_ZERO_BYTES,
    SPI_CPR_CONFIG_TYPE_CARRIER_TRAILING_ZERO_BYTES,
    SPI_CPR_CONFIG_TYPE_SYNCHRONIZATION_PATTERN,
    SPI_CPR_CONFIG_TYPE_SYNCHRONIZATION_PATTERN_SIZE,
    SPI_CPR_CONFIG_TYPE_SLAVE_IDLE_STATE,
    SPI_CPR_CONFIG_TYPE_COMMON_SYNC_CHARACTER,
    SPI_CPR_CONFIG_TYPE_SPECIAL_SYNC_CHARACTER,
    SPI_CPR_CONFIG_TYPE_INVERT_SIGNAL_LEVEL,
    SPI_CPR_CONFIG_TYPE_MODULATION_RATIO,
    SPI_CPR_CONFIG_TYPE_MAX_PULSE_DISTORTION
} spiCprConfigParaType_enum;

/** SPI CPR direction of SLC telegrams */
typedef enum {
    SPI_CPR_TELE_DIRECTION_NONE = 0,
    SPI_CPR_TELE_DIRECTION_RECEIVED,
    SPI_CPR_TELE_DIRECTION_SEND
} spiCprTeleDirection_enum;

/** SPI CPR I/O controls */
typedef enum {
    SPI_CPR_IOCTL_NONE = 0,
    SPI_CPR_IOCTL_RTS_SET,
    SPI_CPR_IOCTL_RTS_RESET,
    SPI_CPR_IOCTL_DTR_SET,
    SPI_CPR_IOCTL_DTR_RESET,
    SPI_CPR_IOCTL_CTS_GET,
    SPI_CPR_IOCTL_DCD_GET,
    SPI_CPR_IOCTL_DSR_GET,
    SPI_CPR_IOCTL_DTR_GET,
    SPI_CPR_IOCTL_RCV_TIMEOUT_TICKS_SET,
    SPI_CPR_IOCTL_EXPECT_SPECIAL_SYNC_CHAR,
    SPI_CPR_IOCTL_TRANSMIT_BIT_COUNT_SET,
    SPI_CPR_IOCTL_USART_MODE_SET
} spiCprIOCtl_enum;

/** SPI CPR flash area type enumeration */
typedef enum {
    SPI_CPR_FLASH_AREA_BOOT = 0,
    SPI_CPR_FLASH_AREA_APPLICATION
} spiCprFlashAreaType_enum;

/** SPI CPR device firmware type enumeration.        */
/** CAUTION: Be sure to increase this number define  */
/**          SPI_CPR_MAX_NUM_OF_DEVICE_FW_TYPES when */
/**          this enumeration is extended. */
typedef enum {
    SPI_CPR_DEVICE_FW_UNKNOWN = 0,
    SPI_CPR_DEVICE_FW_BOOT_LOADER,
    SPI_CPR_DEVICE_FW_BOOT_LOADER_AT_APPL_AREA,
    SPI_CPR_DEVICE_FW_SERIAL_LINE_CONTROLLER,
    SPI_CPR_DEVICE_FW_LINE_CODE_CONTROLLER,
    SPI_CPR_DEVICE_FW_PULSE_DURATION_CONTROLLER,
    SPI_CPR_FIRST_DEVICE_FW = SPI_CPR_DEVICE_FW_BOOT_LOADER,
    SPI_CPR_LAST_DEVICE_FW = SPI_CPR_DEVICE_FW_PULSE_DURATION_CONTROLLER
} spiCprDeviceFwType_enum;

/** SPI CPR SLC USART modes enumeration */
typedef enum {
    SPI_CPR_SLC_USART_NORMAL_MODE              = 0x00,
    SPI_CPR_SLC_USART_RS485_MODE               = 0x01,
    SPI_CPR_SLC_USART_HW_HANDSHAKING_MODE      = 0x02,
    SPI_CPR_SLC_USART_IS07816_T_0_MODE         = 0x04,
    SPI_CPR_SLC_USART_IS07816_T_1_MODE         = 0x06,
    SPI_CPR_SLC_USART_IRDA_MODE                = 0x08,
    SPI_CPR_SLC_USART_SPI_MASTER               = 0x0E,
    SPI_CPR_SLC_USART_SPI_SLAVE                = 0x0F
} spiCprSlcUsartMode_enum;

/*****************************************************************************/
/* Common type definitions */
/*****************************************************************************/

/* SPI CPR firmware version type */
typedef struct {
    uint8_t major;                /**< major release version number */
    uint8_t minor;                /**< minor release version number */
    uint8_t build;                /**< build version number */
    uint8_t type;                 /**< type number (0 == release, 0 != debug) */
} spiCprFwVersion_type;

/*****************************************************************************/
/* Master type definitions */
/*****************************************************************************/

/* SPI CPR optional request parameter type */
typedef struct {
    uint16_t data;                   /**< Optional request parameter data */
} spiCprOptReqPara_type;

/* SPI CPR configuration parameter type */
typedef struct {
    uint8_t  type;                   /**< Parameter type */
    uint8_t  size;                   /**< Parameter size */
    uint32_t value;                  /**< Parameter value */
} spiCprConfigPara_type;

/* SPI CPR data block identification type */
typedef struct {
    uint16_t no;                     /**< Data block number */
    uint8_t  size;                   /**< Data block size (1 .. 255, 0 = 256) */
    uint8_t  type;                   /**< Data block type */
} spiCprBlockIdent_type;

/* SPI CPR master request type */
typedef struct {
    uint8_t devId;                   /**< Device id to identify master (if single master 0xFF) */
    uint8_t reqType;                 /**< Request type */
    union {
        spiCprOptReqPara_type opt;   /**< Optional request parameter */
        spiCprConfigPara_type para;  /**< Configuration parameter */
        spiCprBlockIdent_type block; /**< Data block identification */
    } master;
} spiCprMasterRequest_type;

/* SPI CPR master data block content type */
typedef struct {
    uint8_t byte[SPI_CPR_MAX_DATA_BLOCK_SIZE]; /**< Data block send by master */
} spiCprMasterBlockData_type;

/*****************************************************************************/
/* Slave type definitions */
/*****************************************************************************/

/* SPI CPR slave standard response type */
typedef struct {
    uint8_t devId;                /**< Device id to identify slave (range 3 .. 6) */
    uint8_t respType;             /**< Slave response type */
    uint8_t procState;            /**< Actual processing state of the slave */
    uint8_t irqStatus;            /**< Interrupt status bits to identify IRQ reason */
    uint8_t reqResult;            /**< Request result representing an error code */
    /* Additional SLC specific parameter */
    struct {
        uint8_t  modLine;         /**< Modem line status bits */
        uint16_t rcvSize;         /**< Receive data size (1 .. 320) */
    } slc;
} spiCprSlaveResponse_type;

/* SPI CPR slave SLC telegram transfer structure */
typedef struct {
    uint16_t noOfChar;            /**< Number of characters in telegram */
    uint8_t  status;              /**< Error status of telegram */
    uint8_t  direction;           /**< Direction of telegram (received/send) */
    uint32_t timestamp;           /**< Time stamp of first character in telegram */
    uint8_t  byte[SPI_CPR_MAX_DATA_BLOCK_SIZE]; /**< Characters of telegram */
} spiCprSlaveTeleTransfer_type;

/* SPI CPR firmware information type */
typedef struct {
    uint32_t             type;    /**< Firmware type (defined as 4 Bytes to align the structure) */
    spiCprFwVersion_type version; /**< Firmware version */
    uint32_t             size;    /**< Firmware size */
    uint32_t             hash;    /**< Firmware CRC32 hash */
} spiCprSlaveFwInformation_type;

typedef struct {
    unsigned char character; /* received character */
    unsigned char status;    /* status of received character */
    unsigned int  timeStamp; /* time stamp of received character */
} sioChar_type;

#endif

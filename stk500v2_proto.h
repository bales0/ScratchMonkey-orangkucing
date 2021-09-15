//**** ATMEL AVR - A P P L I C A T I O N   N O T E  ************************
//*
//* Title:		AVR068 - STK500 Communication Protocol
//* Filename:		command.h
//* Version:		1.0
//* Last updated:	31.01.2005
//*
//* Support E-mail:	avr@atmel.com
//*
//**************************************************************************

// Modified by Hisashi Ito <info at mewpro.cc> (c) 2015
// in order to support HVprog2, an STK500 clone open hardware that you can buy or make.
// http://mewpro.cc/2016/01/20/how-to-use-hvprog2/
// 
// XPROG related definition is copied from AVR079 - STK600 Communication Protocol

// *****************[ STK message constants ]***************************

#define MESSAGE_START                       0x1B        //= ESC = 27 decimal
#define TOKEN                               0x0E

// *****************[ STK general command constants ]**************************

#define CMD_SIGN_ON                         0x01
#define CMD_SET_PARAMETER                   0x02
#define CMD_GET_PARAMETER                   0x03
#define CMD_SET_DEVICE_PARAMETERS           0x04
#define CMD_OSCCAL                          0x05
#define CMD_LOAD_ADDRESS                    0x06
#define CMD_FIRMWARE_UPGRADE                0x07
#define CMD_CHECK_TARGET_CONNECTION         0x0D
#define CMD_LOAD_RC_ID_TABLE                0x0E
#define CMD_LOAD_EC_ID_TABLE                0x0F


// *****************[ STK ISP command constants ]******************************

#define CMD_ENTER_PROGMODE_ISP              0x10
#define CMD_LEAVE_PROGMODE_ISP              0x11
#define CMD_CHIP_ERASE_ISP                  0x12
#define CMD_PROGRAM_FLASH_ISP               0x13
#define CMD_READ_FLASH_ISP                  0x14
#define CMD_PROGRAM_EEPROM_ISP              0x15
#define CMD_READ_EEPROM_ISP                 0x16
#define CMD_PROGRAM_FUSE_ISP                0x17
#define CMD_READ_FUSE_ISP                   0x18
#define CMD_PROGRAM_LOCK_ISP                0x19
#define CMD_READ_LOCK_ISP                   0x1A
#define CMD_READ_SIGNATURE_ISP              0x1B
#define CMD_READ_OSCCAL_ISP                 0x1C
#define CMD_SPI_MULTI                       0x1D

// *****************[ STK PP command constants ]*******************************

#define CMD_ENTER_PROGMODE_PP               0x20
#define CMD_LEAVE_PROGMODE_PP               0x21
#define CMD_CHIP_ERASE_PP                   0x22
#define CMD_PROGRAM_FLASH_PP                0x23
#define CMD_READ_FLASH_PP                   0x24
#define CMD_PROGRAM_EEPROM_PP               0x25
#define CMD_READ_EEPROM_PP                  0x26
#define CMD_PROGRAM_FUSE_PP                 0x27
#define CMD_READ_FUSE_PP                    0x28
#define CMD_PROGRAM_LOCK_PP                 0x29
#define CMD_READ_LOCK_PP                    0x2A
#define CMD_READ_SIGNATURE_PP               0x2B
#define CMD_READ_OSCCAL_PP                  0x2C    

#define CMD_SET_CONTROL_STACK               0x2D

// *****************[ STK HVSP command constants ]*****************************

#define CMD_ENTER_PROGMODE_HVSP             0x30
#define CMD_LEAVE_PROGMODE_HVSP             0x31
#define CMD_CHIP_ERASE_HVSP                 0x32
#define CMD_PROGRAM_FLASH_HVSP              0x33
#define CMD_READ_FLASH_HVSP                 0x34
#define CMD_PROGRAM_EEPROM_HVSP             0x35
#define CMD_READ_EEPROM_HVSP                0x36
#define CMD_PROGRAM_FUSE_HVSP               0x37
#define CMD_READ_FUSE_HVSP                  0x38
#define CMD_PROGRAM_LOCK_HVSP               0x39
#define CMD_READ_LOCK_HVSP                  0x3A
#define CMD_READ_SIGNATURE_HVSP             0x3B
#define CMD_READ_OSCCAL_HVSP                0x3C
// These two are redefined since 0x30/0x31 collide
// with the STK600 bootloader.
#define CMD_ENTER_PROGMODE_HVSP_STK600      0x3D
#define CMD_LEAVE_PROGMODE_HVSP_STK600      0x3E

// *****************[ STK XPROG command constants ]*****************************

#define CMD_XPROG                           0x50
#define CMD_XPROG_SETMODE                   0x51

// *****************[ STK status constants ]***************************

// Success
#define STATUS_CMD_OK                       0x00

// Warnings
#define STATUS_CMD_TOUT                     0x80
#define STATUS_RDY_BSY_TOUT                 0x81
#define STATUS_SET_PARAM_MISSING            0x82

// Errors
#define STATUS_CMD_FAILED                   0xC0
#define STATUS_CKSUM_ERROR                  0xC1
#define STATUS_CMD_UNKNOWN                  0xC9
#define STATUS_CMD_ILLEGAL_PARAMETER        0xCA

// *****************[ STK parameter constants ]***************************
#define PARAM_JTAG_ALLOW_FULL_PAGE_STREAM   0x50 // also set in TPI
#define PARAM_JTAG_EEPROM_PAGE_SIZE         0x52 // also set in TPI

#define PARAM_BUILD_NUMBER_LOW              0x80
#define PARAM_BUILD_NUMBER_HIGH             0x81
#define PARAM_HW_VER                        0x90
#define PARAM_SW_MAJOR                      0x91
#define PARAM_SW_MINOR                      0x92
#define PARAM_VTARGET                       0x94
#define PARAM_VADJUST                       0x95
#define PARAM_OSC_PSCALE                    0x96
#define PARAM_OSC_CMATCH                    0x97
#define PARAM_SCK_DURATION                  0x98
#define PARAM_TOPCARD_DETECT                0x9A
#define PARAM_STATUS                        0x9C
#define PARAM_DATA                          0x9D
#define PARAM_RESET_POLARITY                0x9E
#define PARAM_CONTROLLER_INIT               0x9F

#define PARAM_STATUS_TGT_CONN               0xA1
#define PARAM_DISCHARGEDELAY                0xA4
#define PARAM_SOCKETCARD_ID                 0xA5
#define PARAM_ROUTINGCARD_ID                0xA6
#define PARAM_EXPCARD_ID                    0xA7
#define PARAM_SW_MAJOR_SLAVE1               0xA8
#define PARAM_SW_MINOR_SLAVE1               0xA9
#define PARAM_SW_MAJOR_SLAVE2               0xAA
#define PARAM_SW_MINOR_SLAVE2               0xAB
#define PARAM_BOARD_ID_STATUS               0xAD
#define PARAM_RESET                         0xB4
#define PARAM_VTG_SETPOINT                  0xB7

#define PARAM2_SCK_DURATION                 0xC0
#define PARAM2_CLOCK_CONF                   0xC1
#define PARAM2_AREF0                        0xC2
#define PARAM2_AREF1                        0xC3

#define PARAM2_JTAG_FLASH_SIZE_H            0xC5 // also set in TPI
#define PARAM2_JTAG_FLASH_SIZE_L            0xC6 // also set in TPI
#define PARAM2_JTAG_FLASH_PAGE_SIZE         0xC7 // also set in TPI
#define PARAM2_RC_ID_TABLE_REV              0xC8
#define PARAM2_EC_ID_TABLE_REV              0xC9
#define PARAM2_AREF0_SETPOINT               0xCC
#define PARAM2_AREF1_SETPOINT               0xCD

// *****************[ STK answer constants ]***************************

#define ANSWER_CKSUM_ERROR                  0xB0

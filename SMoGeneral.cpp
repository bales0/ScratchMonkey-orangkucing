// -*- mode: c++; tab-width: 4; indent-tabs-mode: nil -*-
//
// ScratchMonkey 0.1        - STK500v2 compatible programmer for Arduino
//
// File: SMoGeneral.cpp     - Protocol independent global commands
//
// Copyright (c) 2013 Matthias Neeracher <microtherion@gmail.com>
// All rights reserved.
//
// See license at bottom of this file or at
// http://opensource.org/licenses/bsd-license.php
//

// Modified by Hisashi Ito <info at mewpro.cc> (c) 2015
// in order to support HVprog2, an STK500 clone open hardware that you can buy or make.
// http://mewpro.cc/2016/01/20/how-to-use-hvprog2/

#include "SMoGeneral.h"
#include "SMoCommand.h"

#include <string.h>

#if !defined(STK600_EMU)

union SMoGeneral::address_t SMoGeneral::gAddress;
const uint8_t           kBuildNumberLow     = 0x01;
const uint8_t           kBuildNumberHigh    = 0x00;
const uint8_t           kHardwareVersion    = 0x02;
const uint8_t           kSoftwareMajor      = 0x02;
const uint8_t           kSoftwareMinor      = 0x0a;
uint8_t     SMoGeneral::gVoltage            = 50;
uint8_t     SMoGeneral::gSCKDuration        = 2;    // 125kHz
static uint8_t          gControllerInit     = 0;
uint8_t     SMoGeneral::gPrescale           = 1;
uint8_t     SMoGeneral::gClockMatch         = 0;
uint8_t     SMoGeneral::gDischargeDelay     = 232;
uint8_t     SMoGeneral::gResetPolarity      = 1;    // 1: AVR (active LOW), 0: 8051 (active HIGH)
uint8_t     SMoGeneral::gControlStack[32];

#else

union SMoGeneral::address_t SMoGeneral::gAddress;
const uint8_t           kBuildNumberLow     = 0x01;
const uint8_t           kBuildNumberHigh    = 0x00;
const uint8_t           kHardwareVersion    = 0x03;
const uint8_t           kSoftwareMajor      = 0x02;
const uint8_t           kSoftwareMinor      = 0x2d;
const uint8_t           kSlave1Major        = 0x02;
const uint8_t           kSlave1Minor        = 0x03;
const uint8_t           kSlave2Major        = 0x02;
const uint8_t           kSlave2Minor        = 0x02;
const uint16_t          kRCIdTableRev       = 0xFFFF; // AS7.0.2542 + AK7.0.132 requires >=0x00f2
const uint16_t          kECIdTableRev       = 0xFFFF;
const uint16_t          kClockConf          = 0xdefc; // 16MHz
uint8_t     SMoGeneral::gVoltage            = 50;
uint16_t    SMoGeneral::gSCKDuration        = 0x3F;  // 8.0us (125kHz)
static uint8_t          gControllerInit     = 0;
uint8_t     SMoGeneral::gPrescale           = 1;
uint8_t     SMoGeneral::gClockMatch         = 0;
uint8_t     SMoGeneral::gDischargeDelay     = 232;
uint8_t     SMoGeneral::gResetPolarity      = 1;    // 1: AVR (active LOW), 0: 8051 (active HIGH)
uint8_t     SMoGeneral::gControlStack[32];

#endif

void    
SMoGeneral::SignOn()
{
#if !defined(STK600_EMU)
    memcpy(&SMoCommand::gBody[2], "\010STK500_2", 9);
    SMoCommand::SendResponse(STATUS_CMD_OK, 11);
#else
    memcpy(&SMoCommand::gBody[2], "\006STK600", 7);
    SMoCommand::SendResponse(STATUS_CMD_OK, 9);
#endif
}

void    
SMoGeneral::SetParam()
{
    uint8_t value = SMoCommand::gBody[2];
    switch (SMoCommand::gBody[1]) {
    case PARAM_VTARGET:
    case PARAM_VADJUST:
        // just ignore
        break;
    case PARAM_SCK_DURATION:
        gSCKDuration    = value;
        break;
    case PARAM_JTAG_ALLOW_FULL_PAGE_STREAM: // Always 0x2C in TPI
    case PARAM_JTAG_EEPROM_PAGE_SIZE:       // Always 0x00 in TPI
        // just ignore
        break;
    case PARAM2_SCK_DURATION: // STK600
        gSCKDuration    = (value << 8) + SMoCommand::gBody[3];
        break;
    case PARAM_VTG_SETPOINT:
    case PARAM_STATUS_TGT_CONN:
    case PARAM2_CLOCK_CONF:
    case PARAM2_AREF0:
    case PARAM2_AREF1:
    case PARAM2_AREF0_SETPOINT:
    case PARAM2_AREF1_SETPOINT:
        // just ignore
        break;
    case PARAM2_JTAG_FLASH_SIZE_H:    // Always 0x0000 in TPI
    case PARAM2_JTAG_FLASH_SIZE_L:    // tiny10: 0x0400, tiny20: 0x0800, tiny40: 0x1000 in TPI
    case PARAM2_JTAG_FLASH_PAGE_SIZE: // Always 0x0080 in TPI
        // just ignore
        break;
    case PARAM_RESET_POLARITY:
        gResetPolarity = value;    
        break;
    case PARAM_CONTROLLER_INIT:
        gControllerInit = value;
        break;
    case PARAM_OSC_PSCALE:
        gPrescale = value;
        break;
    case PARAM_OSC_CMATCH:
        gClockMatch = value;
        break;
    case PARAM_DISCHARGEDELAY:
        gDischargeDelay = value;
        break;
    default:
        SMoCommand::SendResponse(STATUS_CMD_FAILED);
        return;
    }
    SMoCommand::SendResponse();
}

void    
SMoGeneral::GetParam()
{
    uint16_t len = 3;
#define result SMoCommand::gBody[2]
    switch (SMoCommand::gBody[1]) {
    case PARAM_BUILD_NUMBER_LOW:
        result  = kBuildNumberLow;
        break;
    case PARAM_BUILD_NUMBER_HIGH:
        result  = kBuildNumberHigh;
        break;
    case PARAM_HW_VER:
        result  = kHardwareVersion;
        break;
    case PARAM_SW_MAJOR:
        result  = kSoftwareMajor;
        break;
    case PARAM_SW_MINOR:
        result  = kSoftwareMinor;
        break;
    case PARAM_STATUS_TGT_CONN:
        result = 0; // not implemented yet
        break;
    case PARAM_VTG_SETPOINT:
        result = 0; // just ignore
        break;
    case PARAM_SOCKETCARD_ID:
    case PARAM_ROUTINGCARD_ID:
    case PARAM_EXPCARD_ID:
        result = 0xFF; // no card
        break;
#ifdef STK600_EMU
    case PARAM_SW_MAJOR_SLAVE1:
        result = kSlave1Major;
        break;
    case PARAM_SW_MINOR_SLAVE1:
        result = kSlave1Minor;
        break;
    case PARAM_SW_MAJOR_SLAVE2:
        result = kSlave2Major;
        break;
    case PARAM_SW_MINOR_SLAVE2:
        result = kSlave2Minor;
        break;
    case PARAM_BOARD_ID_STATUS:
        result = 0; // ??
        break;
#define result2 SMoCommand::gBody[3]
    case PARAM2_RC_ID_TABLE_REV:
        result = kRCIdTableRev >> 8;
        result2 = kRCIdTableRev & 0xFF;
        len = 4;
        break;
    case PARAM2_EC_ID_TABLE_REV:
        result = kECIdTableRev >> 8;
        result2 = kECIdTableRev & 0xFF;
        len = 4;
        break;
    case PARAM2_SCK_DURATION:
        result = SMoGeneral::gSCKDuration >> 8;
        result2 = SMoGeneral::gSCKDuration & 0xFF;
        len = 4;
        break;
    case PARAM2_CLOCK_CONF:
        result = kClockConf >> 8;
        result2 = kClockConf & 0xFF;
        len = 4;
        break;
    case PARAM2_AREF0:
    case PARAM2_AREF1:
    case PARAM2_AREF0_SETPOINT:
    case PARAM2_AREF1_SETPOINT:
        result = result2 = 0; // just ignore
        len = 4;
        break;
#undef result2
#endif
    case PARAM_VTARGET:
    case PARAM_VADJUST:
        result  = gVoltage;
        break;
    case PARAM_SCK_DURATION:
        result  = gSCKDuration;
        break;
    case PARAM_RESET_POLARITY:
        result  = gResetPolarity;
        break;
    case PARAM_CONTROLLER_INIT:
        result  = gControllerInit;
        break;
    case PARAM_OSC_PSCALE:
        result = gPrescale;
        break;
    case PARAM_OSC_CMATCH:
        result = gClockMatch;
        break;
    case PARAM_TOPCARD_DETECT:
        result = 0;
        break;
    case PARAM_DISCHARGEDELAY: // TPI only
        result = gDischargeDelay;
        break;
    default:
        SMoCommand::SendResponse(STATUS_CMD_FAILED);
        return;
    }
    SMoCommand::SendResponse(STATUS_CMD_OK, len);
#undef result
}

void    
SMoGeneral::LoadAddress()
{
    SMoGeneral::gAddress.c[3] = SMoCommand::gBody[1];
    SMoGeneral::gAddress.c[2] = SMoCommand::gBody[2];
    SMoGeneral::gAddress.c[1] = SMoCommand::gBody[3];
    SMoGeneral::gAddress.c[0] = SMoCommand::gBody[4];
    SMoCommand::SendResponse();
}

void 
SMoGeneral::SetControlStack()
{
    memcpy(&SMoGeneral::gControlStack[0], &SMoCommand::gBody[1], 32);
    SMoCommand::SendResponse();
}

//
// LICENSE
//
// Redistribution and use in source and binary forms, with or without modification, 
// are permitted provided that the following conditions are met:
//
//  * Redistributions of source code must retain the above copyright notice, this 
//    list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright notice, 
//    this list of conditions and the following disclaimer in the documentation 
//    and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE 
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR 
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER 
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// 

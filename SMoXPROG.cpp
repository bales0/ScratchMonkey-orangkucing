// -*- mode: c++; tab-width: 4; indent-tabs-mode: nil -*-
//
// ScratchMonkey 0.1        - STK500v2 compatible programmer for Arduino
//
// File: SMoXPROG.cpp       - XPROG for TPI and PDI 
//

// This part of ScratchMonkey is written by Hisashi Ito <info at mewpro.cc> (c) 2015
// in order to support HVprog2, an STK500 clone open hardware that you can buy or make.
// http://mewpro.cc/2016/01/20/how-to-use-hvprog2/
//
// Derived from source codes of LUFA AVRISP mkII clone, avrdude, USBasp
// and ATtiny4_5_9_10_20_40Programmer.ino

// The same license as main part applies.

#include "SMoCommand.h"
#include "SMoGeneral.h"
#include "SMoXPROG.h"
#include "SMoTPI.h"
#include "SMoPDI.h"

struct SMoXPROG::param_t SMoXPROG::XPRGParam = {0, 0, 0x33, 0x32, 0};
static bool sActive = false;

static uint8_t SelectedProtocol = XPRG_MODE_TPI;


static bool
ValidRequest(uint8_t command)
{
    // gSize includes CMD_XPROG; all offsets below are in XPRG_Body.
    switch (command) {
    case XPRG_CMD_ENTER_PROGMODE:
    case XPRG_CMD_LEAVE_PROGMODE:
        return SMoCommand::gSize == 2;
    case XPRG_CMD_ERASE:
        if (SMoCommand::gSize != 7) return false;
        return SelectedProtocol == XPRG_MODE_TPI ?
            (XPRG_Body[1] == XPRG_ERASE_CHIP || XPRG_Body[1] == XPRG_ERASE_CONFIG) :
            (XPRG_Body[1] >= XPRG_ERASE_CHIP && XPRG_Body[1] <= XPRG_ERASE_USERSIG);
    case XPRG_CMD_CRC:
        return SMoCommand::gSize == 3 && XPRG_Body[1] >= XPRG_CRC_APP && XPRG_Body[1] <= XPRG_CRC_FLASH;
    case XPRG_CMD_SET_PARAM:
        if (SMoCommand::gSize < 3)
            return false;
        switch (XPRG_Body[1]) {
        case XPRG_PARAM_NVMBASE: {
            if (SMoCommand::gSize != 7) return false;
            const uint32_t base = (uint32_t)XPRG_Body[2] << 24 |
                (uint32_t)XPRG_Body[3] << 16 | (uint16_t)XPRG_Body[4] << 8 | XPRG_Body[5];
            return base <= UINT32_MAX-0x10;
        }
        case XPRG_PARAM_EEPPAGESIZE:
        case XPRG_PARAM_FLASHPAGESIZE: {
            if (SMoCommand::gSize != 5)
                return false;
            uint16_t size = (uint16_t(XPRG_Body[2]) << 8) | XPRG_Body[3];
            return size >= 2 && size <= 512 && !(size & (size - 1));
        }
        case XPRG_PARAM_NVMCMD_REG:
        case XPRG_PARAM_NVMCSR_REG:
            return SMoCommand::gSize == 4 && XPRG_Body[2] < 64;
        default:
            return false;
        }
    case XPRG_CMD_WRITE_MEM: {
        if (SMoCommand::gSize < 10)
            return false;
        uint16_t size = (uint16_t(XPRG_Body[7]) << 8) | XPRG_Body[8];
        if (!size || size > 256 || SMoCommand::gSize != size + 10 ||
            XPRG_Body[1] < XPRG_MEM_TYPE_APPL || XPRG_Body[1] > XPRG_MEM_TYPE_USERSIG ||
            (XPRG_Body[2] & ~3)) return false;
        const uint32_t address = (uint32_t)XPRG_Body[3] << 24 |
            (uint32_t)XPRG_Body[4] << 16 | (uint16_t)XPRG_Body[5] << 8 | XPRG_Body[6];
        if (address > UINT32_MAX-size) return false;
        if (SelectedProtocol == XPRG_MODE_TPI)
            return address <= 0xFFFFUL && size <= 0x10000UL-address && !(address & 1);
        if (XPRG_Body[1] == XPRG_MEM_TYPE_FUSE || XPRG_Body[1] == XPRG_MEM_TYPE_LOCKBITS)
            return size == 1;
        if (XPRG_Body[1] != XPRG_MEM_TYPE_EEPROM && (address & 1)) return false;
        const uint16_t pageSize = XPRG_Body[1] == XPRG_MEM_TYPE_EEPROM ?
            SMoXPROG::XPRGParam.EEPageSize : SMoXPROG::XPRGParam.FlashPageSize;
        if (!(XPRG_Body[2] & (1 << XPRG_MEM_WRITE_WRITE)) && pageSize &&
            size > pageSize-(address & (pageSize-1))) return false;
        return true;
    }
    case XPRG_CMD_READ_MEM: {
        if (SMoCommand::gSize != 9)
            return false;
        uint16_t size = (uint16_t(XPRG_Body[6]) << 8) | XPRG_Body[7];
        const uint32_t address = (uint32_t)XPRG_Body[2] << 24 |
            (uint32_t)XPRG_Body[3] << 16 | (uint16_t)XPRG_Body[4] << 8 | XPRG_Body[5];
        return size && size <= 256 && address <= UINT32_MAX-size &&
            XPRG_Body[1] >= XPRG_MEM_TYPE_APPL && XPRG_Body[1] <= XPRG_MEM_TYPE_FACTORY_CALIBRATION &&
            (SelectedProtocol != XPRG_MODE_TPI || (address <= 0xFFFFUL && size <= 0x10000UL-address));
    }
    default:
        return false;
    }
}

static uint16_t
SetParam()
{
    switch (XPRG_Body[1]) {
    case XPRG_PARAM_NVMBASE:
        SMoXPROG::XPRGParam.NVMBase = (uint32_t)XPRG_Body[2] << 24 |
            (uint32_t)XPRG_Body[3] << 16 | (uint16_t)XPRG_Body[4] << 8 | XPRG_Body[5];
        break;
    case XPRG_PARAM_EEPPAGESIZE:
        SMoXPROG::XPRGParam.EEPageSize = (uint16_t(XPRG_Body[2]) << 8) | XPRG_Body[3];
        break;
    case XPRG_PARAM_NVMCMD_REG:
        SMoXPROG::XPRGParam.NVMCMD = XPRG_Body[2];
        break;
    case XPRG_PARAM_NVMCSR_REG:
        SMoXPROG::XPRGParam.NVMCSR = XPRG_Body[2];
        break;
    case XPRG_PARAM_FLASHPAGESIZE:
        SMoXPROG::XPRGParam.FlashPageSize = (uint16_t(XPRG_Body[2]) << 8) | XPRG_Body[3];
        break;
    default:
        XPRG_Body[1] = XPRG_ERR_FAILED;
        return 2;
    }
    XPRG_Body[1] = XPRG_ERR_OK;
    return 2;
}

void
SMoXPROG::XPROG()
{
    uint8_t XPROGCommand = SMoCommand::gSize >= 2 ? XPRG_Body[0] : 0;
    if (!ValidRequest(XPROGCommand)) {
        XPRG_Body[1] = XPRG_ERR_FAILED;
        SMoCommand::SendResponse(XPROGCommand, 3);
        return;
    }

    if (XPROGCommand != XPRG_CMD_ENTER_PROGMODE && XPROGCommand != XPRG_CMD_SET_PARAM &&
        !sActive) {
        XPRG_Body[1] = XPROGCommand == XPRG_CMD_LEAVE_PROGMODE ? XPRG_ERR_OK : XPRG_ERR_FAILED;
        SMoCommand::SendResponse(XPROGCommand, 3);
        return;
    }
    if (XPROGCommand == XPRG_CMD_ENTER_PROGMODE && sActive)
        Release();
    uint16_t answerlen;
    switch (SelectedProtocol) {
    case XPRG_MODE_TPI:
        switch (XPROGCommand) {
        case XPRG_CMD_ENTER_PROGMODE:
            answerlen = SMoTPI::EnterProgmode();
            break;
        case XPRG_CMD_LEAVE_PROGMODE:
            answerlen = SMoTPI::LeaveProgmode();
            break;
        case XPRG_CMD_ERASE:
            answerlen = SMoTPI::Erase();
            break;
        case XPRG_CMD_WRITE_MEM:
            answerlen = SMoTPI::WriteMem();
            break;
        case XPRG_CMD_READ_MEM:
            answerlen = SMoTPI::ReadMem();
            break;
        case XPRG_CMD_CRC:
            answerlen = SMoTPI::CRC();
            break;
        case XPRG_CMD_SET_PARAM:
            answerlen = SetParam();
            break;
        default:
            SMoCommand::SendResponse(STATUS_CMD_FAILED);
            return;
        }
        break;
    case XPRG_MODE_PDI:
        switch (XPROGCommand) {
        case XPRG_CMD_ENTER_PROGMODE:
            SMoGeneral::gVoltage = 33; // report 3.3V from now on if queried
            SMoXPROG::XPRGParam.FlashPageSize = 0; // assume the old method
            answerlen = SMoPDI::EnterProgmode();
            break;
        case XPRG_CMD_LEAVE_PROGMODE:
            SMoGeneral::gVoltage = 50; // report 5.0V from now on if queried
            answerlen = SMoPDI::LeaveProgmode();
            break;
        case XPRG_CMD_ERASE:
            answerlen = SMoPDI::Erase();
            break;
        case XPRG_CMD_WRITE_MEM:
            answerlen = SMoPDI::WriteMem();
            break;
        case XPRG_CMD_READ_MEM:
            answerlen = SMoPDI::ReadMem();
            break;
        case XPRG_CMD_CRC:
            answerlen = SMoPDI::CRC();
            break;
        case XPRG_CMD_SET_PARAM:
            answerlen = SetParam();
            break;
        default:
            SMoCommand::SendResponse(STATUS_CMD_FAILED);
            return;
        }
        break;
    default:
        SMoCommand::SendResponse(STATUS_CMD_FAILED);
        return;
    }
    if (XPRG_Body[1] != XPRG_ERR_OK) {
        Release();
        answerlen = 2;
    } else if (XPROGCommand == XPRG_CMD_ENTER_PROGMODE) {
        sActive = true;
        SMoCommand::gProgrammingMode = CMD_XPROG;
    } else if (XPROGCommand == XPRG_CMD_LEAVE_PROGMODE) {
        sActive = false;
        SMoCommand::gProgrammingMode = 0;
    }
    SMoCommand::SendResponse(XPROGCommand, answerlen+1);  
}

void
SMoXPROG::XPROG_SetMode()
{
    if (SMoCommand::gSize != 2 ||
        (XPRG_Body[0] != XPRG_MODE_TPI && XPRG_Body[0] != XPRG_MODE_PDI)) {
        SMoCommand::SendResponse(STATUS_CMD_FAILED);
        return;
    }
    uint8_t mode = XPRG_Body[0];
 
    if (sActive) Release();
    SelectedProtocol = mode;
    SMoCommand::SendResponse();
}

void SMoXPROG::Release()
{
    if (SelectedProtocol == XPRG_MODE_PDI) SMoPDI::Release();
    else SMoTPI::Release();
    ResetSession();
}

bool SMoXPROG::HighVoltageActive()
{
    return sActive && SelectedProtocol == XPRG_MODE_TPI;
}

void SMoXPROG::ResetSession()
{
    sActive = false;
    SMoCommand::gProgrammingMode = 0;
    SMoGeneral::gVoltage = 50;
}

#include "SMoCommand.h"

bool SMoCommand::ValidRequest()
{
    if (!gSize || gSize > 275) return false;
    const uint8_t command = gBody[0];
    switch (command) {
    case CMD_SIGN_ON:
    case CMD_OSCCAL:
        return gSize == 1;
    case CMD_GET_PARAMETER:
        return gSize == 2;
    case CMD_SET_PARAMETER:
        if (gSize < 2) return false;
        return gSize == (gBody[1] >= 0xC0 ? 4 : 3);
    case CMD_LOAD_ADDRESS:
        return gSize == 5;
    case CMD_SET_CONTROL_STACK:
        return gSize == 33 && gBody[28] < 8;
    case CMD_ENTER_PROGMODE_ISP:
        return gSize == 12 && gBody[7] >= 1 && gBody[7] <= 4;
    case CMD_ENTER_PROGMODE_PP:
        return gSize == 8;
    case CMD_ENTER_PROGMODE_HVSP:
    case CMD_ENTER_PROGMODE_HVSP_STK600:
        return gSize == 9;
    case CMD_LEAVE_PROGMODE_ISP:
    case CMD_LEAVE_PROGMODE_PP:
    case CMD_LEAVE_PROGMODE_HVSP:
    case CMD_LEAVE_PROGMODE_HVSP_STK600:
        return gSize == 3;
    case CMD_CHIP_ERASE_ISP:
        return gSize == 7;
    case CMD_CHIP_ERASE_PP:
    case CMD_CHIP_ERASE_HVSP:
        return gSize == 3;
    case CMD_PROGRAM_FLASH_ISP:
    case CMD_PROGRAM_EEPROM_ISP:
    case CMD_PROGRAM_FLASH_PP:
    case CMD_PROGRAM_EEPROM_PP:
    case CMD_PROGRAM_FLASH_HVSP:
    case CMD_PROGRAM_EEPROM_HVSP: {
        const uint8_t header = command < CMD_ENTER_PROGMODE_PP ? 10 : 5;
        if (gSize < header) return false;
        const uint16_t count = uint16_t(gBody[1]) << 8 | gBody[2];
        const bool flash = command == CMD_PROGRAM_FLASH_ISP ||
            command == CMD_PROGRAM_FLASH_PP || command == CMD_PROGRAM_FLASH_HVSP;
        return count && count <= uint16_t(275-header) && gSize == count+header &&
            (!flash || !(count & 1));
    }
    case CMD_READ_FLASH_ISP:
    case CMD_READ_EEPROM_ISP:
    case CMD_READ_FLASH_PP:
    case CMD_READ_EEPROM_PP:
    case CMD_READ_FLASH_HVSP:
    case CMD_READ_EEPROM_HVSP: {
        if (gSize != (command < CMD_ENTER_PROGMODE_PP ? 4 : 3)) return false;
        const uint16_t count = uint16_t(gBody[1]) << 8 | gBody[2];
        const bool flash = command == CMD_READ_FLASH_ISP ||
            command == CMD_READ_FLASH_PP || command == CMD_READ_FLASH_HVSP;
        return count && count <= 272 && (!flash || !(count & 1));
    }
    case CMD_PROGRAM_FUSE_ISP:
    case CMD_PROGRAM_LOCK_ISP:
        return gSize == 5;
    case CMD_READ_FUSE_ISP:
    case CMD_READ_LOCK_ISP:
    case CMD_READ_SIGNATURE_ISP:
    case CMD_READ_OSCCAL_ISP:
        return gSize == 6 && gBody[1] >= 1 && gBody[1] <= 4;
    case CMD_PROGRAM_FUSE_PP:
    case CMD_PROGRAM_LOCK_PP:
        return gSize == 5 && gBody[1] <= 3;
    case CMD_PROGRAM_FUSE_HVSP:
    case CMD_PROGRAM_LOCK_HVSP:
        return gSize == 4 && gBody[1] <= 2;
    case CMD_READ_FUSE_PP:
    case CMD_READ_LOCK_PP:
    case CMD_READ_FUSE_HVSP:
    case CMD_READ_LOCK_HVSP:
        return gSize == 2 && gBody[1] <= 2;
    case CMD_READ_SIGNATURE_PP:
    case CMD_READ_OSCCAL_PP:
    case CMD_READ_SIGNATURE_HVSP:
    case CMD_READ_OSCCAL_HVSP:
        return gSize == 2; // address is a byte, not a control-stack index
    case CMD_SPI_MULTI:
        return gSize >= 4 && gSize == uint16_t(gBody[1])+4;
    case CMD_XPROG:
    case CMD_XPROG_SETMODE:
        // XPROG validates its own subcommand, parameters and memory ranges.
        return true;
    default:
        // Unsupported commands must never execute the historical no-op ACKs.
        return false;
    }
}

bool SMoCommand::CheckSession()
{
    const uint8_t command = gBody[0];
    uint8_t required = 0;
    if (command >= CMD_ENTER_PROGMODE_ISP && command <= CMD_SPI_MULTI)
        required = CMD_ENTER_PROGMODE_ISP;
    else if (command >= CMD_ENTER_PROGMODE_PP && command <= CMD_READ_OSCCAL_PP)
        required = CMD_ENTER_PROGMODE_PP;
    else if ((command >= CMD_ENTER_PROGMODE_HVSP && command <= CMD_READ_OSCCAL_HVSP) ||
             command == CMD_ENTER_PROGMODE_HVSP_STK600 || command == CMD_LEAVE_PROGMODE_HVSP_STK600)
        required = CMD_ENTER_PROGMODE_HVSP;
    else if (command == CMD_XPROG || command == CMD_XPROG_SETMODE)
        return !gProgrammingMode || gProgrammingMode == CMD_XPROG;
    else if (command == CMD_SET_CONTROL_STACK)
        return !gProgrammingMode || gProgrammingMode == CMD_ENTER_PROGMODE_PP ||
            gProgrammingMode == CMD_ENTER_PROGMODE_HVSP || gProgrammingMode == CMD_ENTER_PROGMODE_HVSP_STK600;
    else
        return true;
    const bool enter = command == required || command == CMD_ENTER_PROGMODE_HVSP_STK600;
    if (enter) return !gProgrammingMode;
    const bool leave = command == CMD_LEAVE_PROGMODE_ISP || command == CMD_LEAVE_PROGMODE_PP ||
        command == CMD_LEAVE_PROGMODE_HVSP || command == CMD_LEAVE_PROGMODE_HVSP_STK600;
    if (leave && !gProgrammingMode) {
        return true;
    }
    return gProgrammingMode == required ||
        (required == CMD_ENTER_PROGMODE_HVSP && gProgrammingMode == CMD_ENTER_PROGMODE_HVSP_STK600);
}

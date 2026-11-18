#ifndef SMO_SERIAL_FRAME_H
#define SMO_SERIAL_FRAME_H

#include <SPI.h>
#include <util/parity.h>
#include "SMoConfig.h"
#include "SMoXPROG.h"

// TPI/PDI are LSB-first: start, eight data bits, even parity, two stop bits.
// Preserve unused SPI bits between consecutive frames in a repeated PDI read.
class SMoSerialFrameReader {
    uint8_t bits = 0;
    uint8_t remaining = 0;

    uint8_t NextBit() {
        if (!remaining) {
            bits = SPI.transfer(0xFF);
            remaining = 8;
        }
        uint8_t bit = bits & 1;
        bits >>= 1;
        --remaining;
        return bit;
    }

public:
    uint8_t Read(uint8_t *value) {
        const uint32_t started = millis();
        do {
            if (!remaining && (uint32_t)(millis() - started) >= DEFAULTTIMEOUT)
                return XPRG_ERR_TIMEOUT;
        } while (NextBit());

        uint8_t data = 0;
        for (uint8_t bit = 0; bit < 8; ++bit)
            data |= NextBit() << bit;
        const uint8_t parity = NextBit();
        const uint8_t stop1 = NextBit();
        const uint8_t stop2 = NextBit();
        if (parity != parity_even_bit(data) || !stop1 || !stop2)
            return XPRG_ERR_FAILED;
        *value = data;
        return XPRG_ERR_OK;
    }
};

#endif

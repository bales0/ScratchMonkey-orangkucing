#pragma once
#include "Arduino.h"
#include <functional>
enum { LSBFIRST=0, MSBFIRST=1, SPI_MODE0=0, SPI_MODE3=3 };
struct SPISettings { SPISettings(unsigned long, int, int) {} };
struct SPIMock {
    bool active=false;
    unsigned transfers=0;
    std::deque<uint8_t> receive;
    std::function<uint8_t(uint8_t)> onTransfer;
    std::function<void(const uint8_t *,size_t)> onWrite;
    void begin() { active=true; SPCR |= _BV(SPE); }
    void end() { active=false; SPCR &= uint8_t(~_BV(SPE)); }
    void beginTransaction(SPISettings) { active=true; }
    void endTransaction() {}
    uint8_t transfer(uint8_t value) {
        ++transfers;
        if(onTransfer) return onTransfer(value);
        if(receive.empty()) return 0xFF;
        uint8_t b=receive.front(); receive.pop_front(); return b;
    }
    void transfer(void *p, size_t n) {
        transfers+=n;
        if(onWrite) onWrite(static_cast<uint8_t *>(p),n);
    }
};
inline SPIMock SPI;

#pragma once
#include "Arduino.h"
enum { LSBFIRST=0, MSBFIRST=1, SPI_MODE3=3 };
struct SPISettings { SPISettings(unsigned long, int, int) {} };
struct SPIMock {
    bool active=false;
    unsigned transfers=0;
    std::deque<uint8_t> receive;
    void begin() { active=true; }
    void end() { active=false; }
    void beginTransaction(SPISettings) { active=true; }
    void endTransaction() {}
    uint8_t transfer(uint8_t) {
        ++transfers;
        if(receive.empty()) return 0xFF;
        uint8_t b=receive.front(); receive.pop_front(); return b;
    }
    void transfer(void *, size_t n) { transfers+=n; }
};
inline SPIMock SPI;

#pragma once
#include <cstdint>
#include <cstddef>
#include <deque>
#include <vector>
#include <array>
#include <cstring>
#include <utility>
#include <functional>
#define B00100000 0b00100000
#define B01100000 0b01100000
#define B01101000 0b01101000
#define B00010000 0b00010000
#define B110000 0b110000
#define B1111 0b1111
#define B10010000 0b10010000
#define B10000000 0b10000000
#define B11000000 0b11000000
#define B11100000 0b11100000
#define B00000100 0b00000100
#define B00000000 0b00000000
#define B01000000 0b01000000
#define B10100000 0b10100000
#define B00001000 0b00001000
#define B00000001 0b00000001
#define B00000010 0b00000010
#define B00000011 0b00000011
#define B00001100 0b00001100
#define B00000111 0b00000111
#define _BV(bit) (1U << (bit))
#define TCCR2 mockTCCR2
#define TIMER2_COMP_vect mockTimerInterrupt
#define ISR(name) void name()
enum class MockPortOperation { DataDirection, ControlSignals, DataRead, SerialWrite, Silent };
struct MockPortEvent {
    MockPortOperation operation;
    uint8_t value;
};
inline bool mockPortTraceEnabled=false;
inline std::vector<MockPortEvent> mockPortTrace;
inline std::function<void()> mockDataReadHook;
inline std::function<void(uint8_t)> mockClockWriteHook;
inline std::function<int(int)> mockDigitalReadHook;
inline void mockTrace(MockPortOperation operation, uint8_t value) {
    if(mockPortTraceEnabled) mockPortTrace.push_back({operation,value});
}
struct MockRegister {
    MockPortOperation operation;
    bool traceRead=false;
    uint8_t value=0;
    operator uint8_t() const {
        if(traceRead && mockDataReadHook) mockDataReadHook();
        if(traceRead) mockTrace(operation,value);
        return value;
    }
    MockRegister &operator=(uint8_t next) {
        value=next;
        if(!traceRead && operation!=MockPortOperation::Silent) mockTrace(operation,value);
        if(operation==MockPortOperation::Silent && mockClockWriteHook) mockClockWriteHook(value);
        return *this;
    }
    MockRegister &operator|=(unsigned int bits) { return *this=uint8_t(value|bits); }
    MockRegister &operator&=(unsigned int bits) { return *this=uint8_t(value&bits); }
};
inline MockRegister DDRB{MockPortOperation::DataDirection};
inline MockRegister PORTC{MockPortOperation::ControlSignals};
inline MockRegister PINB{MockPortOperation::DataRead,true};
inline MockRegister PORTD{MockPortOperation::Silent};
inline uint8_t mockTCCR2, TCNT2, OCR2, TIMSK, PORTB, DDRC, PINC, SPCR, SPDR;
enum { WGM21=3, CS21=1, OCIE2=7, SPE=6, CPOL=3, CPHA=2, COM20=4 };
enum { LOW=0, HIGH=1, INPUT=0, OUTPUT=1, MOSI=5, MISO=6, SCK=7, SS=4 };
inline uint32_t mockTime=0, mockTick=1;
inline std::array<int, 64> mockPins{};
inline std::array<int, 64> mockPinModes{};
inline unsigned mockGPIO=0;
inline bool mockPinTraceEnabled=false;
inline std::vector<std::pair<int,int>> mockPinWrites;
inline int mockADC=-1;
inline uint32_t millis() { uint32_t now=mockTime; mockTime+=mockTick; return now; }
inline void delay(unsigned long ms) { mockTime+=ms; }
inline void delayMicroseconds(unsigned int) {}
inline void pinMode(int pin, int mode) { ++mockGPIO; mockPinModes.at(pin)=mode; }
inline void digitalWrite(int pin, int value) {
    ++mockGPIO;
    mockPins.at(pin)=value;
    if(mockPinTraceEnabled) mockPinWrites.emplace_back(pin,value);
}
inline void analogWrite(int pin, int value) {
    ++mockGPIO;
    mockPins.at(pin)=value;
    if(mockPinTraceEnabled) mockPinWrites.emplace_back(pin,value);
}
inline int digitalRead(int pin) { return mockDigitalReadHook ? mockDigitalReadHook(pin) : mockPins.at(pin); }
inline int analogRead(int) { return mockADC >= 0 ? mockADC : (mockPins[13] ? 820 : 0); }
inline void noInterrupts() {}
inline void interrupts() {}
struct SerialMock {
    std::deque<uint8_t> input;
    std::vector<uint8_t> output;
    void begin(unsigned long) {}
    int available() { return !input.empty(); }
    int read() { int b=input.front(); input.pop_front(); return b; }
    size_t write(uint8_t b) {
        mockTrace(MockPortOperation::SerialWrite,b);
        output.push_back(b);
        return 1;
    }
    size_t write(const uint8_t *p, size_t n) {
        for(size_t i=0;i<n;++i) write(p[i]);
        return n;
    }
};
inline SerialMock Serial;

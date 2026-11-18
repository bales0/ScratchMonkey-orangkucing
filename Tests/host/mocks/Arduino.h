#pragma once
#include <cstdint>
#include <cstddef>
#include <deque>
#include <vector>
#include <array>
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
inline uint8_t mockTCCR2, TCNT2, OCR2, TIMSK, DDRB, PORTB, SPCR, SPDR;
enum { WGM21=3, CS21=1, OCIE2=7, SPE=6 };
enum { LOW=0, HIGH=1, INPUT=0, OUTPUT=1, MOSI=5, MISO=6, SCK=7, SS=4 };
inline uint32_t mockTime=0, mockTick=1;
inline std::array<int, 64> mockPins{};
inline uint32_t millis() { uint32_t now=mockTime; mockTime+=mockTick; return now; }
inline void delay(unsigned long ms) { mockTime+=ms; }
inline void delayMicroseconds(unsigned int) {}
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int value) { mockPins.at(pin)=value; }
inline void analogWrite(int pin, int value) { mockPins.at(pin)=value; }
inline int digitalRead(int pin) { return mockPins.at(pin); }
inline int analogRead(int) { return 0; }
inline void noInterrupts() {}
inline void interrupts() {}
struct SerialMock {
    std::deque<uint8_t> input;
    std::vector<uint8_t> output;
    int available() { return !input.empty(); }
    int read() { int b=input.front(); input.pop_front(); return b; }
    size_t write(uint8_t b) { output.push_back(b); return 1; }
    size_t write(const uint8_t *p, size_t n) { output.insert(output.end(),p,p+n); return n; }
};
inline SerialMock Serial;

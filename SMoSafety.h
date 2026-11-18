#ifndef SMO_SAFETY_H
#define SMO_SAFETY_H

#include <SPI.h>
#include "SMoConfig.h"
#include "SMoCommand.h"

inline void SMoStopClock()
{
#if SMO_LAYOUT==SMO_LAYOUT_HVPROG2
#ifdef TCCR2
    TCCR2 = 0;
    TIMSK &= uint8_t(~_BV(OCIE2));
#else
    TCCR2B = 0;
    TCCR2A = 0;
    TIMSK2 &= uint8_t(~_BV(OCIE2A));
#endif
#else
    TCCR1B = 0;
    TCCR1A = 0;
    TIMSK1 &= uint8_t(~_BV(OCIE1A));
#endif
}

inline bool SMoWaitVoltage(bool high)
{
#ifdef SMO_AVCC
    const uint32_t started = millis();
    while (high ? analogRead(SMO_AVCC) < 743 : analogRead(SMO_AVCC) > 10)
        if ((uint32_t)(millis() - started) >= DEFAULTTIMEOUT)
            return false;
#else
    (void)high;
#endif
    return true;
}

inline void SMoReleaseHVBus()
{
#if SMO_LAYOUT==SMO_LAYOUT_HVPROG2
    DDRB = 0;
    PORTB = 0;
    DDRC = 0;
    PORTC = 0;
    pinMode(15, INPUT); // PD7, XTAL1/SCI
#endif
}

inline void SMoAbortHV()
{
    // HVProg2 T1 inverts PD2: HIGH pulls PROGRST down to zero.
#ifdef SMO_AVCC
    analogWrite(SMO_SVCC, 0);
#else
    digitalWrite(SMO_SVCC, LOW);
#endif
    digitalWrite(SMO_HVRESET, HIGH);
    delayMicroseconds(10);
#if SMO_LAYOUT==SMO_LAYOUT_HVPROG2
    digitalWrite(SMO_HVENABLE, LOW);
#endif
    SMoReleaseHVBus();
    SMoCommand::gProgrammingMode = 0;
    SMoStopClock();
    SPI.endTransaction();
    SPI.end();
}

#endif

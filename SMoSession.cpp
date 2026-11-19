#include "SMoSession.h"
#include "SMoCommand.h"
#include "SMoSafety.h"
#include "SMoXPROG.h"

static uint32_t sLastResponse;
static bool sArmed;

static bool HighVoltageActive()
{
    const uint8_t mode = SMoCommand::gProgrammingMode;
    return mode == CMD_ENTER_PROGMODE_PP || mode == CMD_ENTER_PROGMODE_HVSP ||
        mode == CMD_ENTER_PROGMODE_HVSP_STK600 ||
        (mode == CMD_XPROG && SMoXPROG::HighVoltageActive());
}

void SMoSession::Refresh()
{
    sArmed = HighVoltageActive();
    if (sArmed) sLastResponse = millis();
}

void SMoSession::Poll()
{
    if (!HighVoltageActive()) {
        sArmed = false;
        return;
    }
    if (!sArmed || (uint32_t)(millis() - sLastResponse) < kIdleTimeout)
        return;
    sArmed = false;

#if SMO_LAYOUT==SMO_LAYOUT_HVPROG2
    // Emergency idle shutdown, separate from the proven normal HVSP LEAVE.
    // T2 HIGH enables the MAX662. T1 HIGH clamps PROGRST to ground, keeping
    // the charge on C8 isolated from RESET through R9 while VTARGET is off.
    digitalWrite(SMO_HVENABLE, LOW);
    digitalWrite(SMO_HVRESET, HIGH);
    analogWrite(SMO_SVCC, 0);
    SMoStopClock();
    SPI.endTransaction();
    SPI.end();
    SMoReleaseHVBus();
#else
    SMoAbortHV();
#endif
    SMoXPROG::ResetSession();
    SMoCommand::ResetParser(); // discard an incomplete packet from the expired host
}

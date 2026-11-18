// -*- mode: c++; tab-width: 4; indent-tabs-mode: nil -*-
//
// ScratchMonkey 0.1        - STK500v2 compatible programmer for Arduino
//
// File: SMoCommand.cpp     - Command parser 
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

#include <Arduino.h>

#include "SMoCommand.h"
#include "SMoConfig.h"

const uint16_t  kHeaderSize         = 5;
const uint16_t  kMaxBodySize        = 275;  // STK500 hardware limit

static uint8_t  sSequenceNumber;
static uint16_t sNumBytesRead       = 0;
static uint16_t sNumBytesWanted     = 1;
static uint8_t  sCheckSum           = 0;
static uint32_t sLastByteTime       = 0;

uint16_t SMoCommand::gSize;
uint8_t SMoCommand::gProgrammingMode = 0;
uint8_t  SMoCommand::gBody[kMaxBodySize+1];

static enum {
    kIdleState,         // Waiting for MESSAGE_START
    kHeaderState,       // Reading message header
    kBodyState,         // Reading message body
    kCompleteState      // Message being processed
}               sState              = kIdleState;

static void
ResetToIdle()
{
    sState          = kIdleState;
    sNumBytesRead   = 0;
    sNumBytesWanted = 1;
    sCheckSum       = 0;
}
//
// Parse next command, return command code if command is fully read
// and checksum matches. Handles timeouts and checksum errors 
// autonomously.
//
int 
SMoCommand::GetNextCommand()
{
    // Discard a stalled frame before consuming a possible new MESSAGE_START.
    if ((sState == kHeaderState || sState == kBodyState) &&
        (uint32_t)(millis() - sLastByteTime) >= DEFAULTTIMEOUT)
        ResetToIdle();
    if (!Serial.available() || sState == kCompleteState)
        return kIncomplete;
    sLastByteTime = millis();
    sCheckSum ^= (gBody[sNumBytesRead++] = Serial.read());
    if (sNumBytesRead < sNumBytesWanted)
        return kIncomplete;
    switch (sState) {
    case kIdleState:
        if (gBody[0] != MESSAGE_START) {
            reportHeaderError:
            ResetToIdle();
            return kHeaderError;
        } else {                            // Start of message
            sState          = kHeaderState;
            sNumBytesWanted = kHeaderSize;

            return kIncomplete;
        }
    case kHeaderState:
        if (gBody[4] != TOKEN)
            goto reportHeaderError;
        sNumBytesWanted = gBody[2] << 8 | gBody[3];
        if (!sNumBytesWanted || sNumBytesWanted > kMaxBodySize)
            goto reportHeaderError;
        sState          = kBodyState;
        sSequenceNumber = gBody[1];
        sNumBytesRead   = 0;
        ++sNumBytesWanted;  // For checksum byte
        
        return kIncomplete;
    case kBodyState:
        if (sCheckSum) {
            gBody[0] = ANSWER_CKSUM_ERROR;
            SendResponse(ANSWER_CKSUM_ERROR);
            return kChecksumError;
        } else {
            sState  = kCompleteState;
            gSize   = sNumBytesRead-1;
#if SMO_LAYOUT==SMO_LAYOUT_HVPROG2
            digitalWrite(SMO_GLED, HIGH);
            digitalWrite(SMO_RLED, LOW);
#endif
            return gBody[0];   // Success!
        }
    default:
      return kIncomplete;
    }
}

void        
SMoCommand::SendResponse(uint8_t status, uint16_t bodySize)
{
    if (bodySize < 2 || bodySize > kMaxBodySize) {
        status = STATUS_CMD_FAILED;
        bodySize = 2;
    }
    if (gBody[0] != CMD_XPROG && gBody[0] != CMD_XPROG_SETMODE) {
        switch (gBody[0]) {
        case CMD_ENTER_PROGMODE_ISP:
        case CMD_ENTER_PROGMODE_PP:
        case CMD_ENTER_PROGMODE_HVSP:
        case CMD_ENTER_PROGMODE_HVSP_STK600:
            if (status == STATUS_CMD_OK) gProgrammingMode = gBody[0];
            break;
        case CMD_LEAVE_PROGMODE_ISP:
        case CMD_LEAVE_PROGMODE_PP:
        case CMD_LEAVE_PROGMODE_HVSP:
        case CMD_LEAVE_PROGMODE_HVSP_STK600:
            if (status == STATUS_CMD_OK) gProgrammingMode = 0;
            break;
        }
    }
    gBody[1] = status;
    sCheckSum   = MESSAGE_START ^ TOKEN ^ sSequenceNumber;
    Serial.write(MESSAGE_START);
    Serial.write(sSequenceNumber);
    sCheckSum  ^= bodySize >> 8;
    Serial.write(bodySize >> 8);
    sCheckSum  ^= bodySize & 0xFF;
    Serial.write(bodySize & 0xFF);
    Serial.write(TOKEN);

    for (uint16_t i=0; i<bodySize; ++i)
        sCheckSum   ^= gBody[i];

    Serial.write(&gBody[0], bodySize);
    Serial.write(sCheckSum);

#if SMO_LAYOUT==SMO_LAYOUT_HVPROG2
    if ((gBody[0] != CMD_XPROG ? status : gBody[2] ) == STATUS_CMD_OK) {
        digitalWrite(SMO_GLED, LOW);
        digitalWrite(SMO_RLED, HIGH);
    } else {
        digitalWrite(SMO_GLED, HIGH);
        digitalWrite(SMO_RLED, LOW);
    }
#endif
    ResetToIdle();
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

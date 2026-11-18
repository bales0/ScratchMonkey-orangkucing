#include "Arduino.h"
#include "SPI.h"
#include "SMoCommand.h"
#include "SMoGeneral.h"
#include "SMoXPROG.h"
#include "SMoTPI.h"
#include "SMoPDI.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

SMoGeneral::address_t SMoGeneral::gAddress{};
uint8_t SMoGeneral::gVoltage=50;
uint8_t SMoGeneral::gDischargeDelay=232;

void check(bool ok, const char *message) {
    if(!ok) throw std::runtime_error(message);
}
std::vector<uint8_t> frame(std::vector<uint8_t> body) {
    std::vector<uint8_t> packet={MESSAGE_START, 1, uint8_t(body.size()>>8), uint8_t(body.size()), TOKEN};
    packet.insert(packet.end(),body.begin(),body.end());
    uint8_t sum=0;
    for(auto b:packet) sum^=b;
    packet.push_back(sum);
    return packet;
}
std::vector<uint8_t> response() {
    check(Serial.output.size()>=8,"short response");
    uint8_t sum=0;
    for(auto b:Serial.output) sum^=b;
    check(!sum,"response checksum");
    size_t n=(size_t(Serial.output[2])<<8)|Serial.output[3];
    check(n+6==Serial.output.size(),"response length");
    return {Serial.output.begin()+5,Serial.output.end()-1};
}
std::vector<uint8_t> request(std::vector<uint8_t> body) {
    Serial.output.clear();
    std::copy(body.begin(),body.end(),SMoCommand::gBody);
    SMoCommand::gSize=body.size();
    if(body[0]==CMD_XPROG_SETMODE) SMoXPROG::XPROG_SetMode();
    else SMoXPROG::XPROG();
    return response();
}
void mode(uint8_t protocol) {
    check(request({CMD_XPROG_SETMODE,protocol})==std::vector<uint8_t>({CMD_XPROG_SETMODE,STATUS_CMD_OK}),"mode select");
}
void rejected(std::vector<uint8_t> packet) {
    unsigned before=SPI.transfers;
    auto reply=request(packet);
    check(reply.size()==3 && reply[0]==CMD_XPROG && reply[2]==XPRG_ERR_FAILED,"XPROG rejection");
    check(SPI.transfers==before,"invalid request touched hardware");
}
void parser_tests() {
    auto packet=frame({CMD_SIGN_ON});
    Serial.input={packet[0],packet[1],packet[2]};
    while(Serial.available()) check(SMoCommand::GetNextCommand()==0,"partial header completed");
    mockTime+=200;
    Serial.input.assign(packet.begin(),packet.end());
    int command=0;
    while(Serial.available()) command=SMoCommand::GetNextCommand();
    check(command==CMD_SIGN_ON,"parser did not recover from stalled header");
    SMoCommand::SendResponse();

    // Timeout must also recover with a partially received body across millis wrap.
    mockTime=UINT32_MAX-20;
    Serial.input.assign(packet.begin(),packet.end()-1);
    while(Serial.available()) SMoCommand::GetNextCommand();
    mockTime+=200;
    SMoCommand::GetNextCommand();
    Serial.input.assign(packet.begin(),packet.end());
    while(Serial.available()) command=SMoCommand::GetNextCommand();
    check(command==CMD_SIGN_ON,"parser body timeout/wrap recovery");
    SMoCommand::SendResponse();

    auto bad=packet; bad.back()^=1;
    Serial.input.assign(bad.begin(),bad.end());
    Serial.output.clear();
    while(Serial.available()) command=SMoCommand::GetNextCommand();
    check(command==SMoCommand::kChecksumError,"checksum error accepted");
    check(response()[0]==ANSWER_CKSUM_ERROR,"checksum response");
    auto empty=frame({}); Serial.input.assign(empty.begin(),empty.end());
    bool rejectedHeader=false;
    while(Serial.available()) rejectedHeader|=SMoCommand::GetNextCommand()==SMoCommand::kHeaderError;
    check(rejectedHeader,"empty frame accepted");
}
void xprog_tests() {
    mode(XPRG_MODE_TPI);
    check(request({CMD_XPROG_SETMODE,XPRG_MODE_UPDI})[1]==STATUS_CMD_FAILED,"UPDI accepted");
    check(request({CMD_XPROG_SETMODE})[1]==STATUS_CMD_FAILED,"short mode accepted");
    rejected({CMD_XPROG});
    rejected({CMD_XPROG,0xFE});
    rejected({CMD_XPROG,XPRG_CMD_SET_PARAM,0xFE,0});
    rejected({CMD_XPROG,XPRG_CMD_SET_PARAM,XPRG_PARAM_NVMBASE,0});
    rejected({CMD_XPROG,XPRG_CMD_SET_PARAM,XPRG_PARAM_FLASHPAGESIZE,0,0});
    rejected({CMD_XPROG,XPRG_CMD_READ_MEM,1,0,0,0,0,1,1}); // 257 bytes
    rejected({CMD_XPROG,XPRG_CMD_READ_MEM,1,0,0,0,0,0,0});
    rejected({CMD_XPROG,XPRG_CMD_WRITE_MEM,1,2,0,0,0,0,0,2,0xAA}); // payload short
    auto reply=request({CMD_XPROG,XPRG_CMD_SET_PARAM,XPRG_PARAM_NVMBASE,0x01,0x02,0x03,0x04});
    check(reply[2]==XPRG_ERR_OK && SMoXPROG::XPRGParam.NVMBase==0x01020304,"NVM base decoding");
    reply=request({CMD_XPROG,XPRG_CMD_SET_PARAM,XPRG_PARAM_FLASHPAGESIZE,2,0});
    check(reply[2]==XPRG_ERR_OK && SMoXPROG::XPRGParam.FlashPageSize==512,"flash page size decoding");
}
void timeout_tests() {
    mode(XPRG_MODE_TPI);
    mockTick=1; mockTime=0;
    auto reply=request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE});
    check(reply[2]==XPRG_ERR_TIMEOUT && mockTime<1000,"TPI missing target timeout");
    check(!SPI.active && mockPins[11]==LOW,"TPI timeout did not release SPI/HV");
    reply=request({CMD_XPROG,XPRG_CMD_READ_MEM,1,0,0,0,0,0,1});
    check(reply.size()==3 && reply[2]==XPRG_ERR_TIMEOUT,"TPI read timeout lost");

    // Decode valid TPI frames whose status never sets NVMEN.
    SPI.receive.clear();
    SPI.receive.push_back(0xFF); SPI.receive.push_back(0xFF); // activation clocks
    for(int i=0;i<1000;i++) { SPI.receive.push_back(0); SPI.receive.push_back(0xFE); SPI.receive.push_back(0xFF); }
    mockTime=0;
    reply=request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE});
    check(reply[2]==XPRG_ERR_TIMEOUT && mockTime<1000,"TPI NVM activation has no deadline");
    SPI.receive.clear();

    mode(XPRG_MODE_PDI);
    SMoXPROG::XPRGParam.FlashPageSize=2;
    mockTick=101;
    reply=request({CMD_XPROG,XPRG_CMD_WRITE_MEM,1,2,0,0,0,0,0,2,0x12,0x34});
    check(reply==std::vector<uint8_t>({CMD_XPROG,XPRG_CMD_WRITE_MEM,XPRG_ERR_TIMEOUT}),"PDI write timeout status/framing");
    mockTick=1;
}
int main() {
    try {
        parser_tests(); std::cout<<"PASS parser recovery, wraparound, checksum\n";
        xprog_tests(); std::cout<<"PASS XPROG validation and parameter decoding\n";
        timeout_tests(); std::cout<<"PASS TPI deadlines/cleanup and PDI error propagation\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}

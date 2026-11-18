#include "Arduino.h"
#include "SPI.h"
#include "SMoCommand.h"
#include "SMoGeneral.h"
#include "SMoXPROG.h"
#include "SMoTPI.h"
#include "SMoPDI.h"
#include "SMoHVPP.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

void protocol_tests();
int firmware_bridge(const char *);

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
    Serial.input={MESSAGE_START,1,1,20,TOKEN}; // body size 276 exceeds 275
    rejectedHeader=false;
    while(Serial.available()) rejectedHeader|=SMoCommand::GetNextCommand()==SMoCommand::kHeaderError;
    check(rejectedHeader,"oversize frame header accepted");
    Serial.input.assign(packet.begin(),packet.end());
    while(Serial.available()) command=SMoCommand::GetNextCommand();
    check(command==CMD_SIGN_ON,"parser did not recover after oversize frame");
    SMoCommand::SendResponse();
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
void hvpp_read_tests() {
    // Distinct control-stack values expose low/high-byte selection mistakes.
    std::fill_n(SMoGeneral::gControlStack,32,0);
    SMoGeneral::gControlStack[8]=0x10;  // load command, low byte
    SMoGeneral::gControlStack[0]=0x20;  // load address, low byte
    SMoGeneral::gControlStack[20]=0x40; // enable read, low byte
    SMoGeneral::gControlStack[21]=0x60; // enable read, high byte
    SMoGeneral::gControlStack[12]=0x80; // done, low byte
    SMoGeneral::gControlStack[13]=0xA0; // done, high byte
    SMoGeneral::gControlStack[25]=0xFB; // RDY/BSY is an input

    for(uint8_t index=0;index<5;++index) {
        const bool calibration=index>=3;
        const uint8_t command=calibration ? CMD_READ_OSCCAL_PP : CMD_READ_SIGNATURE_PP;
        const uint8_t address=calibration ? index-3 : index;
        const uint8_t data=calibration ? 0xA5 : uint8_t(0x1E + index);
        DDRB=0xFF;
        PINB=data;
        SMoCommand::gBody[0]=command;
        SMoCommand::gBody[1]=address;
        SMoCommand::gSize=2;
        Serial.output.clear();
        mockPortTrace.clear();
        mockPortTraceEnabled=true;
        if(calibration) SMoHVPP::ReadOscCal();
        else SMoHVPP::ReadSignature();
        mockPortTraceEnabled=false;

        const std::vector<MockPortEvent> expected={
            {MockPortOperation::ControlSignals,0x10},
            {MockPortOperation::ControlSignals,0x20},
            {MockPortOperation::DataDirection,0x00},
            {MockPortOperation::ControlSignals,uint8_t(calibration ? 0x60 : 0x40)},
            {MockPortOperation::DataRead,data},
            {MockPortOperation::ControlSignals,uint8_t(calibration ? 0xA0 : 0x80)},
            {MockPortOperation::DataDirection,0xFF},
        };
        check(mockPortTrace.size()>expected.size(),"HVPP missing port/response trace");
        for(size_t i=0;i<expected.size();++i) {
            check(mockPortTrace[i].operation==expected[i].operation &&
                  mockPortTrace[i].value==expected[i].value,
                  "HVPP must read with INPUT, apply DONE, restore OUTPUT, then reply");
        }
        for(size_t i=expected.size();i<mockPortTrace.size();++i)
            check(mockPortTrace[i].operation==MockPortOperation::SerialWrite,
                  "HVPP changed ports after starting response");
        check(PORTB==address,"HVPP signature/calibration address");
        check(uint8_t(DDRB)==0xFF,"HVPP data bus not restored to OUTPUT");
        check(response()==std::vector<uint8_t>({command,STATUS_CMD_OK,data}),
              "HVPP signature/calibration response");
    }
}
int main(int argc,char **argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--bridge") return firmware_bridge(argv[2]);
        parser_tests(); std::cout<<"PASS parser recovery, wraparound, checksum\n";
        xprog_tests(); std::cout<<"PASS XPROG validation and parameter decoding\n";
        protocol_tests();
        hvpp_read_tests(); std::cout<<"PASS HVPP signature/calibration port sequence and response\n";
        return 0;
    } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}

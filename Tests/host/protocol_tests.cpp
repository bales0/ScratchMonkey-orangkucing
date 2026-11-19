#include "Arduino.h"
#include "SPI.h"
#include "SMoCommand.h"
#include "SMoGeneral.h"
#include "SMoXPROG.h"
#include "SMoPDI.h"
#include "SMoTPI.h"
#include "SMoISP.h"
#include "SMoHVPP.h"
#include "SMoHVSP.h"
#include "SMoSerialFrame.h"
#include "SMoSession.h"
#include <map>
#include <algorithm>
#include <iostream>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif

void check(bool, const char *);
std::vector<uint8_t> frame(std::vector<uint8_t>);
std::vector<uint8_t> response();
std::vector<uint8_t> request(std::vector<uint8_t>);
void mode(uint8_t);
void loop();
void setup();

static std::vector<uint8_t> wire(const std::vector<uint8_t> &values, unsigned idle=0,
                                 bool badParity=false, bool badStop=false) {
    std::vector<bool> bits(idle,true);
    for(uint8_t value: values) {
        bits.push_back(false);
        for(int i=0;i<8;++i) bits.push_back((value>>i)&1);
        bits.push_back(parity_even_bit(value) ^ badParity);
        bits.push_back(!badStop);
        bits.push_back(true);
    }
    std::vector<uint8_t> bytes((bits.size()+7)/8,0xFF);
    for(size_t i=0;i<bits.size();++i)
        if(!bits[i]) bytes[i/8] &= uint8_t(~(1U<<(i%8)));
    return bytes;
}

static void receive(std::vector<uint8_t> bytes) {
    SPI.receive.assign(bytes.begin(),bytes.end());
}

static std::vector<uint8_t> dispatch(std::vector<uint8_t> body) {
    const auto packet=frame(body);
    Serial.input.assign(packet.begin(),packet.end());
    Serial.output.clear();
    while(Serial.available()) loop();
    return response();
}

static bool target_pins_equal(const std::array<int,64> &before) {
    for(unsigned pin=0;pin<before.size();++pin)
        if(pin!=28 && pin!=29 && before[pin]!=mockPins[pin]) return false;
    return true;
}

static void serial_frame_tests() {
    SPI.onWrite={}; SPI.onTransfer={};
    for(unsigned idle=0;idle<8;++idle) {
        receive(wire({0x00,0xFF,0xA5,0x80},idle));
        SMoSerialFrameReader reader;
        for(uint8_t expected: {0x00,0xFF,0xA5,0x80}) {
            uint8_t got=0;
            check(reader.Read(&got)==XPRG_ERR_OK && got==expected,"12-bit frame alignment/repeated read");
        }
    }
    for(bool parity: {false,true}) {
        receive(wire({0xA5},0,parity,!parity));
        SMoSerialFrameReader reader;
        uint8_t got=0x55;
        check(reader.Read(&got)==XPRG_ERR_FAILED && got==0x55,"bad frame accepted or output corrupted");
    }
    for(uint32_t start: {0U,UINT32_MAX-20}) {
        SPI.receive.clear(); mockTick=1; mockTime=start;
        SMoSerialFrameReader reader;
        uint8_t got=0;
        check(reader.Read(&got)==XPRG_ERR_TIMEOUT,"missing start bit/wraparound");
        check(uint32_t(mockTime-start)<200,"start-bit deadline exceeded");
    }
    std::cout<<"PASS TPI/PDI frames: all alignments, repeated bytes, parity/stop, deadlines/wrap\n";
}

// Decode the real firmware's SPI output, not its WRITE_MEM arguments. Model
// XMEGA's NVM page buffer separately from nonvolatile Flash and count commits.
struct PDIDevice {
    std::map<uint32_t,uint8_t> memory;
    std::map<uint16_t,uint8_t> buffer;
    std::vector<uint32_t> commits;
    std::vector<uint8_t> erases;
    uint32_t nvmBase=0x010001C0, pointer=0, address=0;
    uint16_t pageSize=512, repeat=1;
    uint8_t command=0, pending=0, needed=0, nvmCommand=0;
    bool silent=false, badParity=false, busy=false;

    void attach() {
        SPI.onWrite=[this](const uint8_t *p,size_t n) {
            const unsigned offset=n==2 ? 4 : 0;
            for(unsigned bit=offset;bit+12<=n*8;bit+=12) {
                uint8_t value=0;
                for(unsigned i=0;i<8;++i)
                    value |= ((p[(bit+1+i)/8]>>((bit+1+i)%8))&1)<<i;
                byte(value);
            }
        };
    }
    ~PDIDevice() { SPI.onWrite={}; SPI.onTransfer={}; SPI.receive.clear(); }
    uint8_t read(uint32_t a) {
        if(a==nvmBase+15) return busy ? 0x80 : 0;
        auto found=memory.find(a);
        return found==memory.end() ? 0xFF : found->second;
    }
    void reply(const std::vector<uint8_t> &values) {
        if(silent) return;
        SPI.receive.push_back(0xFF); // guard byte
        for(auto b:wire(values,0,badParity)) SPI.receive.push_back(b);
    }
    void write(uint32_t a,uint8_t value) {
        if(a==nvmBase+10) { nvmCommand=value; return; }
        if(nvmCommand==0x22 || nvmCommand==0x2A || nvmCommand==0x32 || nvmCommand==0x18) {
            const uint32_t base=a&~uint32_t(pageSize-1);
            for(uint16_t i=0;i<pageSize;++i) memory[base+i]=0xFF;
            buffer.clear(); erases.push_back(nvmCommand); return;
        }
        if(nvmCommand==0x40 && a==nvmBase+11) {
            for(auto &entry:memory)
                if(entry.first>=0x800000 && entry.first<0x8E0000) entry.second=0xFF;
            buffer.clear(); erases.push_back(nvmCommand); return;
        }
        if(nvmCommand==0x23 || nvmCommand==0x33) {
            buffer[a&(pageSize-1)]=value;
        } else if(nvmCommand==0x24 || nvmCommand==0x25 || nvmCommand==0x2C ||
                  nvmCommand==0x2D || nvmCommand==0x34 || nvmCommand==0x35 || nvmCommand==0x1A) {
            uint32_t base=a&~uint32_t(pageSize-1);
            commits.push_back(base);
            bool erase=nvmCommand==0x25 || nvmCommand==0x2D || nvmCommand==0x35;
            for(uint16_t i=0;i<pageSize;++i) {
                const uint8_t loaded=buffer.count(i) ? buffer[i] : 0xFF;
                memory[base+i]=(erase ? 0xFF : read(base+i)) & loaded;
            }
            buffer.clear(); // real NVM automatically clears it after commit
        } else memory[a]=value;
    }
    void byte(uint8_t value) {
        if(needed) {
            if(pending==0x4C || pending==0x0C || pending==0x6B) {
                address |= uint32_t(value) << (8*(4-needed));
                if(--needed) return;
                if(pending==0x4C) { pending=0x55; needed=1; }
                else if(pending==0x0C) { reply({read(address)}); pending=0; }
                else { pointer=address; pending=0; }
            } else {
                if(pending==0x55) write(address,value);
                else if(pending==0x64) write(pointer++,value);
                else if(pending==0xA0) repeat=uint16_t(value)+1;
                else if(pending==0xC1 && value==0x59) buffer.clear(); // reset key
                if(!--needed) pending=0;
            }
            return;
        }
        if(value==0x4C || value==0x0C || value==0x6B) {
            pending=value; needed=4; address=0;
        } else if(value==0x64 || value==0xA0 || (value&0xF0)==0xC0) {
            pending=value; needed=1;
        } else if(value==0xE0) { pending=value; needed=8; }
        else if(value==0x80) reply({2});
        else if(value==0x24) {
            std::vector<uint8_t> result;
            while(repeat--) result.push_back(read(pointer++));
            repeat=1;
            reply(result);
        } else check(false,"unmodelled PDI instruction");
    }
};

static void pdi_enter(PDIDevice &device,bool pageParameter=true) {
    device.attach();
    mode(XPRG_MODE_PDI);
    check(request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE})[2]==0,"PDI enter");
    check(request({CMD_XPROG,XPRG_CMD_SET_PARAM,XPRG_PARAM_NVMBASE,1,0,1,0xC0})[2]==0,"PDI base");
    if(pageParameter)
        check(request({CMD_XPROG,XPRG_CMD_SET_PARAM,XPRG_PARAM_FLASHPAGESIZE,
                       uint8_t(device.pageSize>>8),uint8_t(device.pageSize)})[2]==0,"PDI page size");
    check(request({CMD_XPROG,XPRG_CMD_SET_PARAM,XPRG_PARAM_EEPPAGESIZE,
                   uint8_t(device.pageSize>>8),uint8_t(device.pageSize)})[2]==0,"PDI EEPROM size");
}

static std::vector<uint8_t> pdi_write(uint8_t type,uint8_t flags,uint32_t address,
                                     const std::vector<uint8_t> &data) {
    std::vector<uint8_t> body={CMD_XPROG,XPRG_CMD_WRITE_MEM,type,flags,
        uint8_t(address>>24),uint8_t(address>>16),uint8_t(address>>8),uint8_t(address),
        uint8_t(data.size()>>8),uint8_t(data.size())};
    body.insert(body.end(),data.begin(),data.end());
    return request(body);
}

static void pdi_tests() {
    for(unsigned size: {32,64,128,256,512}) {
        PDIDevice device; device.pageSize=size;
        pdi_enter(device);
        const uint32_t base=0x00810000; // exercise addresses above 64 KiB
        std::vector<uint8_t> data(size);
        for(unsigned i=0;i<size;++i) data[i]=uint8_t(i*17+3);
        for(unsigned offset=0;offset<size;offset+=256) {
            const unsigned end=std::min(offset+256,size);
            auto reply=pdi_write(1,end==size ? 2 : 0,base+offset,
                                 {data.begin()+offset,data.begin()+end});
            check(reply==std::vector<uint8_t>({CMD_XPROG,XPRG_CMD_WRITE_MEM,0}),"PDI write reply");
            check(device.commits.size()==(end==size ? 1U : 0U),"premature/missing page commit");
            if(end<size) {
                auto unloaded=request({CMD_XPROG,XPRG_CMD_READ_MEM,1,0,0x81,0,0,1,0});
                check(unloaded.size()==259 && std::all_of(unloaded.begin()+3,unloaded.end(),
                      [](uint8_t value) { return value==0xFF; }),"first 256B fragment changed nonvolatile Flash");
                check(device.buffer.size()==256,"reading Flash discarded the pending page buffer");
            }
        }
        for(unsigned i=0;i<size;++i) check(device.read(base+i)==data[i],"PDI page-buffer data lost");
        check(device.buffer.empty(),"page buffer not cleared by commit");
        check(pdi_write(1,2,base,std::vector<uint8_t>(2,0xFF))[2]==0,"PDI repeated write");
        for(unsigned i=0;i<size;++i) check(device.read(base+i)==data[i],"write without erase changed untouched data");
        check(pdi_write(3,3,base,std::vector<uint8_t>(3,0x5A))[2]==0,"EEPROM erase/write partial page");
        check(device.read(base)==0x5A && device.read(base+2)==0x5A && device.read(base+3)==0xFF,"EEPROM erase/write semantics");
        check(pdi_write(2,2,base+size,std::vector<uint8_t>(5,0x39))[2]==0,"PDI partial boot page");
        check(device.read(base+size+4)==0x39 && device.read(base+size+5)==0xFF,"odd final Flash padding");
        check(request({CMD_XPROG,XPRG_CMD_LEAVE_PROGMODE})[2]==0,"PDI leave");
    }
    {
        PDIDevice device; pdi_enter(device);
        check(pdi_write(1,0,0x00800000,std::vector<uint8_t>(256,0x12))[2]==0,"PDI unfinished buffer load");
        check(device.commits.empty(),"PDI incomplete page reached Flash");
        check(request({CMD_XPROG,XPRG_CMD_LEAVE_PROGMODE})[2]==0,"PDI abort before final fragment");
        pdi_enter(device);
        check(device.buffer.empty(),"PDI re-entry retained aborted page buffer");
        check(pdi_write(1,2,0x00800180,std::vector<uint8_t>(256,0x3C))[2]==0,"PDI crossing page boundary");
        check(device.commits.size()==2 && device.read(0x00800180)==0x3C && device.read(0x0080027F)==0x3C,"PDI cross-page commits/data");
        check(request({CMD_XPROG,XPRG_CMD_ERASE,XPRG_ERASE_APP_PAGE,0,0x80,0,0})[2]==0,"PDI application page erase");
        check(device.read(0x00800180)==0xFF && device.read(0x00800200)==0x3C,"PDI page erase range");
        check(request({CMD_XPROG,XPRG_CMD_ERASE,XPRG_ERASE_CHIP,0,0,0,0})[2]==0,"PDI chip erase");
        check(device.read(0x00800200)==0xFF && device.erases.size()==2,"PDI chip erase not executed");
        check(request({CMD_XPROG,XPRG_CMD_LEAVE_PROGMODE})[2]==0,"PDI leave after erase");
    }
    {
        PDIDevice device;
        pdi_enter(device,false); // legacy clients omit FLASHPAGESIZE
        check(pdi_write(1,0,0x00800000,std::vector<uint8_t>(256,0x12))[2]==0,"legacy page buffer load");
        check(device.commits.empty(),"legacy intermediate fragment was committed");
        check(pdi_write(1,2,0x00800100,std::vector<uint8_t>(256,0x34))[2]==0,"legacy final fragment");
        check(device.commits.size()==1 && device.read(0x00800000)==0x12 &&
              device.read(0x008001FF)==0x34,"legacy 512B page commit/data");
        check(request({CMD_XPROG,XPRG_CMD_LEAVE_PROGMODE})[2]==0,"legacy PDI leave");
    }
    {
        PDIDevice device;
        pdi_enter(device);
        check(pdi_write(4,2,0x008F0020,{0xFE})[2]==0 && device.read(0x008F0020)==0xFE,"PDI fuse write");
        check(pdi_write(5,2,0x008F0021,{0xFC})[2]==0 && device.read(0x008F0021)==0xFC,"PDI lock write");
        check(pdi_write(6,2,0x008E0400,{0x12,0x34,0x56})[2]==0 &&
              device.read(0x008E0402)==0x56,"PDI user signature buffer/write");
        device.memory[0x008E0200]=0xA5;
        check(request({CMD_XPROG,XPRG_CMD_READ_MEM,7,0,0x8E,2,0,0,1})==
              std::vector<uint8_t>({CMD_XPROG,XPRG_CMD_READ_MEM,0,0xA5}),"PDI calibration read");
        device.memory[device.nvmBase+6]=0x12;
        device.memory[device.nvmBase+5]=0x34;
        device.memory[device.nvmBase+4]=0x56;
        check(request({CMD_XPROG,XPRG_CMD_CRC,3})==
              std::vector<uint8_t>({CMD_XPROG,XPRG_CMD_CRC,0,0x12,0x34,0x56}),"PDI CRC status/order");
        device.busy=true;
        check(pdi_write(1,2,0x00800000,{0x12,0x34})[2]==XPRG_ERR_TIMEOUT && !SPI.active,
              "PDI busy timeout/status cleanup");
    }
    {
        PDIDevice device;
        pdi_enter(device);
        device.silent=true; mockTime=UINT32_MAX-20;
        auto reply=request({CMD_XPROG,XPRG_CMD_READ_MEM,1,0,0x80,0,0,0,1});
        check(reply==std::vector<uint8_t>({CMD_XPROG,XPRG_CMD_READ_MEM,XPRG_ERR_TIMEOUT}),"PDI disconnected read status");
        check(!SPI.active && !mockTCCR2 && !(TIMSK&_BV(OCIE2)) && !mockPins[11],"PDI error leaked clock/SPI/HV");
        check(!(uint8_t(DDRB)&_BV(5)) && !mockPins[10],"PDI MOSI/gate not released");
        device.silent=false;
        pdi_enter(device); // no reset of the programmer or C++ statics
        device.badParity=true;
        reply=request({CMD_XPROG,XPRG_CMD_READ_MEM,1,0,0x80,0,0,0,1});
        check(reply[2]==XPRG_ERR_FAILED && !SPI.active,"PDI bad parity status/cleanup");
        device.badParity=false;
        pdi_enter(device);
        check(request({CMD_XPROG,XPRG_CMD_READ_MEM,1,0,0x80,0,0,0,1})[2]==0,"PDI recovery after parity error");
        mode(XPRG_MODE_TPI);
        check(!SPI.active && !mockTCCR2,"PDI to TPI mode did not release peripherals");
    }
    std::cout<<"PASS real PDI SPI/NVM model: 32/64/128/256/512B pages, one commit, partial/repeated/EEPROM/boot writes, disconnect/parity/reopen\n";
}

static void isp_tests() {
    SPI.onWrite={}; SPI.onTransfer={}; SPI.receive.clear();
    auto program=[](uint8_t flags,uint8_t poll,uint8_t value) {
        Serial.output.clear();
        std::vector<uint8_t> body={CMD_PROGRAM_EEPROM_ISP,0,1,flags,4,0xC0,0xC2,0xA0,0,poll,value};
        std::copy(body.begin(),body.end(),SMoCommand::gBody);
        SMoCommand::gSize=body.size();
        SMoISP::ProgramEEPROM();
        return response();
    };
    mockTime=UINT32_MAX-20; mockTick=1;
    check(program(0x04,0xFF,0x12)[1]==STATUS_RDY_BSY_TOUT,"ISP value polling has no timeout");
    receive(std::vector<uint8_t>(16,0));
    check(program(0x08,0,0x12)[1]==STATUS_CMD_OK,"ISP zero RDY bit must mean ready");
    SPI.receive.clear();
    check(program(0x08,0,0x12)[1]==STATUS_RDY_BSY_TOUT,"ISP busy bit must time out");
    check(program(0x02,0,0x12)[1]==0,"ISP timed delay");
    check(program(0x04,0xFF,0xFF)[1]==0,"ISP equal poll/data must use timed delay");
    std::vector<uint8_t> tx;
    SPI.onTransfer=[&](uint8_t value) { tx.push_back(value); return 0xA5; };
    SMoGeneral::gAddress.l=0x800100FE;
    Serial.output.clear();
    const std::vector<uint8_t> body={CMD_READ_FLASH_ISP,0,8,0x20};
    std::copy(body.begin(),body.end(),SMoCommand::gBody);
    SMoISP::ReadFlash();
    unsigned loads=0;
    for(size_t i=0;i<tx.size();i+=4) loads+=tx[i]==0x4D;
    check(loads==1,"ISP extended address changed after 256 words");
    SPI.onTransfer={};
    for(const auto spec: {std::array<uint8_t,3>{4,4,0}, {0,255,255}, {255,0,0}}) {
        std::vector<uint8_t> body={CMD_SPI_MULTI,spec[0],spec[1],spec[2]};
        for(unsigned i=0;i<spec[0];++i) body.push_back(uint8_t(i+0x20));
        std::vector<uint8_t> sent;
        SPI.onTransfer=[&](uint8_t value) { sent.push_back(value); return uint8_t(sent.size()); };
        std::copy(body.begin(),body.end(),SMoCommand::gBody);
        SMoCommand::gSize=body.size(); Serial.output.clear();
        SMoISP::SPIMulti();
        auto reply=response();
        check(reply.size()==size_t(spec[1])+3 && reply.back()==0,"SPI_MULTI response bounds");
        check(sent.size()==std::max<unsigned>(spec[0],unsigned(spec[1])+spec[2]),"SPI_MULTI clock count");
        for(unsigned i=0;i<spec[0];++i) check(sent[i]==body[4+i],"SPI_MULTI overwrote unsent bytes");
        for(unsigned i=0;i<spec[1];++i) check(reply[2+i]==uint8_t(spec[2]+i+1),"SPI_MULTI receive offset");
    }
    SPI.onTransfer={};
    std::cout<<"PASS ISP value/RDY deadlines, ready polarity, timed delay, extended addressing\n";
}

struct TPIDevice {
    std::map<uint16_t,uint8_t> memory;
    uint16_t pointer=0;
    uint8_t pending=0, needed=0, identity=0x80, signature=0x90;
    unsigned guard=128;
    uint8_t nvmCommand=0;
    unsigned erases=0;
    bool silent=false, active=true, busy=false, badParity=false, badStop=false;
    unsigned identityReads=0;
    void reply(uint8_t value) {
        if(silent) return;
        for(uint8_t b:wire({value},guard+2,badParity,badStop)) SPI.receive.push_back(b);
    }
    void attach() {
        SPI.onWrite=[this](const uint8_t *p,size_t n) {
            for(unsigned bit=n==2 ? 4 : 0;bit+12<=n*8;bit+=12) {
                uint8_t value=0;
                for(unsigned i=0;i<8;++i)
                    value |= ((p[(bit+1+i)/8]>>((bit+1+i)%8))&1)<<i;
                byte(value);
            }
        };
    }
    ~TPIDevice() { SPI.onWrite={}; SPI.onTransfer={}; SPI.receive.clear(); }
    void byte(uint8_t value) {
        if(needed) {
            if(pending==0x68) pointer=(pointer&0xFF00)|value;
            else if(pending==0x69) pointer=(pointer&0xFF)|(uint16_t(value)<<8);
            else if(pending==0x64) {
                memory[pointer]=(memory.count(pointer) ? memory[pointer] : 0xFF)&value;
                ++pointer;
            }
            else if(pending==0xF3) nvmCommand=value;
            else if(pending==0x60 && nvmCommand==0x10) {
                for(auto &entry:memory) if(entry.first>=0x4000) entry.second=0xFF;
                ++erases;
            }
            else if(pending==0x60 && nvmCommand==0x14) {
                memory[pointer&~1U]=memory[pointer|1U]=0xFF;
                ++erases;
            }
            else if(pending==0xC2) guard=(value&7)==7 ? 0 : 128U>>(value&7);
            if(!--needed) pending=0;
        } else if(value==0x8F) { ++identityReads; reply(identity); }
        else if(value==0x80) reply(active ? 2 : 0);
        else if(value==0x72) reply(busy ? 0x80 : 0);
        else if(value==0x20 || value==0x24) {
            reply(pointer==0x3FC1 ? signature : (memory.count(pointer) ? memory[pointer] : 0xFF));
            if(value==0x24) ++pointer;
        } else if(value==0xE0) { pending=value; needed=8; }
        else { pending=value; needed=1; }
    }
};

static void tpi_tests() {
    for(uint8_t signature: {0x8F,0x90,0x91,0x92}) {
        TPIDevice device; device.signature=signature; device.attach();
        mode(XPRG_MODE_TPI);
        check(request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE})[2]==0,"TPI identity/NVM activation");
        check(device.guard==0,"TPI guard register must contain all three GT bits");
        check(pdi_write(1,2,0x4000,{0x12,0x34,0x56})[2]==0,"TPI Flash write");
        check(device.memory[0x4000]==0x12 && device.memory[0x4002]==0x56 && device.memory[0x4003]==0xFF,"TPI data/padding");
        check(pdi_write(4,2,0x3F40,{0xFE})[2]==0,"TPI fuse write");
        check(device.memory[0x3F40]==0xFE && device.memory[0x3F41]==0xFF,"TPI fuse padding");
        check(request({CMD_XPROG,XPRG_CMD_READ_MEM,4,0,0,0x3F,0x40,0,1})==
              std::vector<uint8_t>({CMD_XPROG,XPRG_CMD_READ_MEM,0,0xFE}),"TPI fuse readback");
        check(pdi_write(4,2,0x3F40,{0xFF})[2]==0 && device.memory[0x3F40]==0xFE,"TPI model allowed zero-to-one without erase");
        check(request({CMD_XPROG,XPRG_CMD_ERASE,XPRG_ERASE_CONFIG,0,0,0x3F,0x40})[2]==0,"TPI configuration erase");
        check(device.memory[0x3F40]==0xFF && device.erases==1,"TPI configuration erase range");
        check(pdi_write(4,2,0x3F40,{0xFD})[2]==0 && device.memory[0x3F40]==0xFD,"TPI fuse reprogram after erase");
        check(request({CMD_XPROG,XPRG_CMD_ERASE,XPRG_ERASE_CHIP,0,0,0x40,0})[2]==0,"TPI chip erase");
        check(device.memory[0x4000]==0xFF && device.memory[0x3F40]==0xFD && device.erases==2,"TPI chip erase range");
        check(request({CMD_XPROG,XPRG_CMD_LEAVE_PROGMODE})[2]==0 && !mockPins[11] && !SPI.active,"TPI cleanup");
    }
    {
        TPIDevice device; device.attach(); mode(XPRG_MODE_TPI);
        device.silent=true; mockTime=UINT32_MAX-20;
        check(request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE})[2]==XPRG_ERR_TIMEOUT,"TPI missing target");
        check(device.identityReads==3 && !SPI.active && !mockPins[11],"TPI retry limit/timeout cleanup");
        device.silent=false; device.badParity=true;
        check(request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE})[2]==XPRG_ERR_FAILED,"TPI bad parity accepted");
        device.badParity=false; device.identity=0x81;
        check(request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE})[2]==XPRG_ERR_FAILED,"TPI wrong identity accepted");
        device.identity=0x80; device.active=false;
        check(request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE})[2]==XPRG_ERR_TIMEOUT,"TPI NVM activation unbounded");
        device.active=true;
        check(request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE})[2]==0,"TPI re-open after failed activation");
        device.badStop=true;
        check(request({CMD_XPROG,XPRG_CMD_READ_MEM,1,0,0,0x40,0,0,1})[2]==XPRG_ERR_FAILED,"TPI bad stop bit accepted");
        check(!SPI.active && !mockPins[11],"TPI framing error leaked HV/SPI");
    }
    std::cout<<"PASS TPI 4/5/9/10/20/40 write sizes, fuse/readback, identity, bounded retries, NVM timeout, parity/stop and recovery\n";
}

static void validation_tests() {
    SMoCommand::gProgrammingMode=0;
    mockPins[11]=HIGH;
    setup();
    check(!mockPins[11] && mockPinModes[11]==OUTPUT,"startup left the 12V generator enabled/floating");
    const std::vector<std::vector<uint8_t>> bad={
        {CMD_ENTER_PROGMODE_PP},{CMD_ENTER_PROGMODE_HVSP},{CMD_ENTER_PROGMODE_ISP},
        {CMD_PROGRAM_FLASH_ISP,0,0,0,0,0,0,0,0,0},
        {CMD_PROGRAM_EEPROM_PP,0,2,0,0,0xFF},
        {CMD_READ_FLASH_PP,0,0},{CMD_READ_EEPROM_HVSP,1,0x11},
        {CMD_READ_SIGNATURE_ISP,0,0,0,0,0},
        {CMD_PROGRAM_FUSE_HVSP,0xFF,0,0}, {CMD_LOAD_ADDRESS,0},
        {CMD_SET_CONTROL_STACK}, {CMD_SPI_MULTI,255,255,255},
    };
    for(auto body:bad) {
        auto savedPins=mockPins;
        const unsigned before=SPI.transfers;
        auto reply=dispatch(body);
        check(reply[1]==STATUS_CMD_FAILED,"invalid legacy request accepted");
        check(before==SPI.transfers,"invalid packet transferred SPI");
        for(unsigned pin=0;pin<mockPins.size();++pin)
            if(pin!=28 && pin!=29) check(mockPins[pin]==savedPins[pin],"invalid packet touched target GPIO");
    }
    // All command bytes and all buffer lengths are safe for the validator itself.
    for(unsigned cmd=1;cmd<256;++cmd) {
        std::fill_n(SMoCommand::gBody,276,0xFF);
        SMoCommand::gBody[0]=cmd;
        for(unsigned size=0;size<=275;++size) {
            SMoCommand::gSize=size;
            SMoCommand::ValidRequest();
        }
    }
    mockADC=0;
    auto reply=dispatch({CMD_ENTER_PROGMODE_PP,0,0,0,1,0,0,0});
    check(reply[1]==STATUS_CMD_FAILED && !mockPins[11] && !mockPins[13],"HVPP ADC failure returned success/left voltage enabled");
    check(!SMoCommand::gProgrammingMode,"failed HV enter became an active session");
    auto pins=mockPins;
    check(dispatch({CMD_LEAVE_PROGMODE_PP,0,0})[1]==0 && target_pins_equal(pins),"idle HV leave changed power");
    mockADC=-1;
    check(dispatch({CMD_ENTER_PROGMODE_PP,0,0,0,1,0,0,0})[1]==0,"HVPP enter with settled voltage");
    pins=mockPins;
    check(dispatch({CMD_XPROG_SETMODE,0})[1]==STATUS_CMD_FAILED && target_pins_equal(pins),"cross-protocol command touched active HV session");
    check(dispatch({CMD_LEAVE_PROGMODE_ISP,0,0})[1]==STATUS_CMD_FAILED &&
          SMoCommand::gProgrammingMode==CMD_ENTER_PROGMODE_PP,"wrong LEAVE lost active HV state");
    check(dispatch({CMD_ENTER_PROGMODE_PP,0,0,0,1,0,0,0})[1]==0,"HV re-entry after chip erase");
    check(dispatch({CMD_LEAVE_PROGMODE_PP,0,0})[1]==0,"HVPP leave");
    unsigned transferred=0;
    SPI.onTransfer=[&](uint8_t) { return (++transferred%4)==3 ? 0x53 : 0; };
    check(dispatch({CMD_ENTER_PROGMODE_ISP,200,0,0,32,0,0x53,3,0xAC,0x53,0,0})[1]==0,
          "ordinary ISP entry");
    check(!mockPins[11],"ISP entry enabled the 12V generator");
    pins=mockPins;
    check(dispatch({CMD_READ_SIGNATURE_HVSP,0})[1]==STATUS_CMD_FAILED && target_pins_equal(pins),
          "HVSP ran during an ISP session");
    check(dispatch({CMD_LEAVE_PROGMODE_ISP,0,0})[1]==0,"ISP leave");
    SPI.onTransfer={};
    SMoCommand::gSize=1;
    SMoCommand::gBody[0]=CMD_SIGN_ON; Serial.output.clear();
    SMoCommand::SendResponse(0,276);
    check(response().size()==2 && response()[1]==STATUS_CMD_FAILED,"oversize response read beyond buffer");
    std::cout<<"PASS malformed commands, buffer limits, ADC failure and protocol isolation\n";
}

static void hv_tests() {
    auto call=[](std::vector<uint8_t> body,void (*fn)()) {
        std::copy(body.begin(),body.end(),SMoCommand::gBody);
        SMoCommand::gSize=body.size(); Serial.output.clear();
        fn(); return response();
    };
    std::fill_n(SMoGeneral::gControlStack,32,0);
    SMoGeneral::gControlStack[25]=0xFD;
    SMoGeneral::gControlStack[27]=1;
    PINC=0;
    check(call({CMD_PROGRAM_FUSE_PP,0,0xAA,0,3},SMoHVPP::ProgramFuse)[1]==STATUS_RDY_BSY_TOUT,"HVPP fuse timeout lost");
    check(!mockPins[11] && !mockPins[13],"HVPP timeout left HV or target supply on");
    PINC=2;
    check(call({CMD_PROGRAM_LOCK_PP,0,0xAA,0,3},SMoHVPP::ProgramLock)[1]==0,"HVPP lock ready");
    PINC=0;
    check(call({CMD_CHIP_ERASE_PP,0,3},SMoHVPP::ChipErase)[1]==STATUS_RDY_BSY_TOUT,"HVPP erase timeout lost");
    check(call({CMD_CHIP_ERASE_PP,3,0},SMoHVPP::ChipErase)[1]==0,"HVPP timed erase rejected");
    SMoGeneral::gControlStack[0]=0x0E;
    SMoGeneral::gControlStack[24]=0xBE;
    check(call({CMD_PROGRAM_FUSE_PP,0,0xAA,2,3},SMoHVPP::ProgramFuse)[1]==0,"classic HVPP timed fuse rejected");
    std::fill_n(SMoGeneral::gControlStack,32,0);
    SMoGeneral::gControlStack[12]=0x7A;
    mockPins[2]=0;
    check(call({CMD_PROGRAM_FUSE_HVSP,0,0xAA,3},SMoHVSP::ProgramFuse)[1]==STATUS_RDY_BSY_TOUT,"HVSP fuse timeout lost");
    mockPins[2]=1;
    check(call({CMD_PROGRAM_LOCK_HVSP,0,0xAA,3},SMoHVSP::ProgramLock)[1]==0,"HVSP lock ready");
    mockPins[2]=0;
    check(call({CMD_CHIP_ERASE_HVSP,3,0},SMoHVSP::ChipErase)[1]==STATUS_RDY_BSY_TOUT,"HVSP erase timeout lost");
    check(call({CMD_CHIP_ERASE_HVSP,0,3},SMoHVSP::ChipErase)[1]==0,"HVSP timed erase rejected");
    SMoGeneral::gControlStack[12]=0;
    check(call({CMD_PROGRAM_FUSE_HVSP,0,0xAA,3},SMoHVSP::ProgramFuse)[1]==0,"tiny11 timed fuse rejected");
    for(uint8_t address: {0,1,0x20}) {
        mockPinWrites.clear();
        mockPinTraceEnabled=true;
        call({CMD_READ_OSCCAL_HVSP,address},SMoHVSP::ReadOscCal);
        mockPinTraceEnabled=false;
        std::vector<int> data;
        for(auto event:mockPinWrites) if(event.first==0) data.push_back(event.second);
        check(data.size()==44,"HVSP calibration frame count");
        uint8_t loaded=0;
        for(unsigned i=12;i<20;++i) loaded=uint8_t((loaded<<1)|data[i]);
        check(loaded==address,"HVSP discarded calibration address");
    }
    for(int adc: {0,1023}) {
        mockADC=adc; SMoCommand::gProgrammingMode=0;
        check(dispatch({CMD_ENTER_PROGMODE_HVSP,0,0,0,0,1,0,0,0})[1]==STATUS_CMD_FAILED,"HVSP ADC timeout success");
        check(!mockPins[11] && !mockPins[13],"HVSP ADC error leaked HV/supply");
    }
    mockADC=-1;
    std::cout<<"PASS HVPP/HVSP fuse/lock/erase RDY and timed cycles, timeout cleanup and ADC failure\n";
}

static void session_tests() {
    const uint32_t timeout=SMoSession::kIdleTimeout;
    mockTick=0; mockTime=0; mockADC=-1;
    SPI.onTransfer={}; SPI.onWrite={};
    SMoCommand::gProgrammingMode=0;
    const std::vector<uint8_t> enterHVSP={CMD_ENTER_PROGMODE_HVSP,0,0,6,1,1,25,1,0};
    const std::vector<uint8_t> enterHVPP={CMD_ENTER_PROGMODE_PP,0,0,1,1,25,1,0};
    for(const auto &enter: {enterHVSP,enterHVPP}) {
        for(uint32_t start: {0U,UINT32_MAX-1000}) {
            mockTime=start;
            check(dispatch(enter)[1]==0,"HV lease enter");
            mockTime+=timeout-1;
            loop();
            check(mockPins[11]==HIGH,"HV lease expired early");
            ++mockTime;
            mockPinWrites.clear(); mockPinTraceEnabled=true;
            Serial.output.clear(); loop(); mockPinTraceEnabled=false;
            check(!SMoCommand::gProgrammingMode && !mockPins[11] && !mockPins[13],"idle HV did not shut down");
            check(mockPins[10]==HIGH && !uint8_t(DDRB) && !DDRC && !mockTCCR2 && !SPI.active,"idle HV left pins/peripherals active");
            check(mockPinWrites.size()>=3 && mockPinWrites[0]==std::make_pair(11,int(LOW)) &&
                  mockPinWrites[1]==std::make_pair(10,int(HIGH)) &&
                  mockPinWrites[2]==std::make_pair(13,0),"idle shutdown must disable HV, clamp RESET, then turn off VTARGET");
            check(Serial.output.empty(),"lease expiry emitted an unsolicited reply");
            check(dispatch(enter)[1]==0,"HV re-entry after host loss");
            check(dispatch({uint8_t(enter[0]+1),0,0})[1]==0,"HV leave after recovery");
        }
    }
    check(dispatch(enterHVPP)[1]==0,"HV keepalive enter");
    for(unsigned i=0;i<4;++i) {
        mockTime+=timeout/2;
        check(dispatch({CMD_GET_PARAMETER,PARAM_VTARGET})[1]==0 && mockPins[11],"valid keepalive lost HV");
    }
    mockTime+=timeout-1;
    // A well-framed but invalid request and a bad checksum must not renew.
    check(dispatch({CMD_READ_FLASH_PP,0,0})[1]==STATUS_CMD_FAILED,"invalid read accepted");
    auto corrupt=frame({CMD_GET_PARAMETER,PARAM_VTARGET}); corrupt.back()^=1;
    Serial.input.assign(corrupt.begin(),corrupt.end());
    while(Serial.available()) loop();
    ++mockTime; loop();
    check(!mockPins[11],"invalid traffic kept HV alive");

    check(dispatch(enterHVPP)[1]==0,"long HV command enter");
    mockTime+=timeout-1;
    SMoGeneral::gAddress.l=0; PINB=0xA5;
    mockDataReadHook=[] { mockTime+=1000; };
    auto reply=dispatch({CMD_READ_FLASH_PP,0,64}); // simulate slow IO inside a valid command
    mockDataReadHook={};
    check(reply.size()==67 && reply[1]==0 && mockPins[11],"lease expired inside a long valid command");
    mockTime+=timeout-1; loop();
    check(mockPins[11],"successful long command did not renew lease");
    ++mockTime; loop(); check(!mockPins[11],"HV idle after long operation did not expire");

    {
        TPIDevice device; device.attach(); mode(XPRG_MODE_TPI);
        check(request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE})[2]==0,"TPI lease enter");
        mockTime+=timeout-1;
        check(dispatch({CMD_XPROG,0})[2]==XPRG_ERR_FAILED,"invalid XPROG accepted");
        ++mockTime; loop();
        check(!mockPins[11] && !mockPins[13] && !SPI.active && !SMoXPROG::HighVoltageActive(),"TPI idle left HV/session active");
        check(request({CMD_XPROG,XPRG_CMD_ENTER_PROGMODE})[2]==0,"TPI reopen after expiry");
        check(request({CMD_XPROG,XPRG_CMD_LEAVE_PROGMODE})[2]==0,"TPI leave after expiry recovery");
    }
    check(dispatch(enterHVPP)[1]==0,"HV partial-frame expiry enter");
    mockTime+=timeout-1;
    Serial.input={MESSAGE_START,1};
    while(Serial.available()) loop();
    ++mockTime; loop();
    check(!mockPins[11],"partial packet kept HV alive");
    check(dispatch(enterHVPP)[1]==0,"new connection was consumed as an expired partial header");
    check(dispatch({CMD_LEAVE_PROGMODE_PP,0,0})[1]==0,"HV leave after partial-frame recovery");
    mockTick=1;
    std::cout<<"PASS HV idle lease: HVSP/HVPP/TPI, wraparound, ordered shutdown, invalid traffic, keepalive, long command and reconnect\n";
}

void protocol_tests() {
    serial_frame_tests();
    pdi_tests();
    tpi_tests();
    isp_tests();
    hv_tests();
    validation_tests();
    session_tests();
}

// Electrical/protocol model for standard AVR programming. It responds at the
// SPI/GPIO boundary; requests still pass through the real firmware handlers.
struct LegacyDevice {
    std::string protocol;
    std::array<uint8_t,3> signature;
    std::array<uint8_t,4> spi{};
    unsigned spiIndex=0, bitIndex=0;
    uint8_t command=0, previousInstruction=0, instruction=0, data=0, output=0;
    uint16_t address=0;
    bool clocked=false;
    LegacyDevice(std::string mode):protocol(mode),signature(mode=="hvpp" ?
        std::array<uint8_t,3>{0x1E,0x95,2} : std::array<uint8_t,3>{0x1E,0x90,7}) {}
    uint8_t read(uint8_t instruction) {
        if(command==8) {
            if(instruction&0x10) return address&1 ? 0x54 : 0x57;
            return address<signature.size() ? signature[address] : 0xFF;
        }
        if(command==4) return instruction==0x68 ? 0x6A : 0xFF;
        return 0xFF;
    }
    void attach() {
        if(protocol=="isp") {
            SPI.onTransfer=[this](uint8_t value) {
                const unsigned index=spiIndex++%4;
                spi[index]=value;
                if(index==2 && spi[0]==0xAC && spi[1]==0x53) return uint8_t(0x53);
                if(index!=3) return uint8_t(0);
                if(spi[0]==0x30) return spi[2]<3 ? signature[spi[2]] : uint8_t(0xFF);
                if(spi[0]==0x38) return uint8_t(spi[2]&1 ? 0x54 : 0x57);
                if(spi[0]==0x50) return uint8_t(spi[1]==0 ? 0x6A : 0xFF);
                return uint8_t(0xFF);
            };
        } else if(protocol=="hvpp") {
            mockClockWriteHook=[this](uint8_t value) {
                if(value&0x80) return;
                const uint8_t controls=uint8_t(PORTC);
                const uint8_t mask=SMoGeneral::gControlStack[25];
                if(controls==(SMoGeneral::gControlStack[8]&mask)) command=PORTB;
                else if(controls==(SMoGeneral::gControlStack[0]&mask)) address=(address&0xFF00)|PORTB;
                else if(controls==(SMoGeneral::gControlStack[1]&mask)) address=(address&0xFF)|(uint16_t(PORTB)<<8);
            };
            mockDataReadHook=[this] {
                const uint8_t controls=uint8_t(PORTC);
                const uint8_t mask=SMoGeneral::gControlStack[25];
                if(command==8) PINB.value=read(controls==(SMoGeneral::gControlStack[21]&mask) ? 0x78 : 0x68);
                else PINB.value=controls==(SMoGeneral::gControlStack[20]&mask) ? 0x6A : 0xFF;
            };
        } else {
            mockClockWriteHook=[this](uint8_t value) { if(value&0x80) clocked=true; };
            mockDigitalReadHook=[this](int pin) {
                if(pin!=2) return mockPins.at(pin);
                if(!clocked) return 1; // ready, outside an SCI transfer
                clocked=false;
                if(!bitIndex) { output=read(previousInstruction); instruction=data=0; }
                if(bitIndex>=1 && bitIndex<=8) {
                    instruction=uint8_t((instruction<<1)|mockPins[1]);
                    data=uint8_t((data<<1)|mockPins[0]);
                }
                const int value=bitIndex<8 ? ((output>>(7-bitIndex))&1) : 1;
                if(++bitIndex==11) {
                    bitIndex=0; previousInstruction=instruction;
                    if(instruction==0x4C) command=data;
                    else if(instruction==0x0C) address=(address&0xFF00)|data;
                    else if(instruction==0x1C) address=(address&0xFF)|(uint16_t(data)<<8);
                }
                return value;
            };
        }
    }
};

// Binary stdin/stdout adapter lets the real AVRDUDE drive the same compiled
// parser/dispatcher/PDI implementation and NVM model over a loopback socket.
int firmware_bridge(const char *protocol) {
#ifdef _WIN32
    _setmode(_fileno(stdin),_O_BINARY);
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    PDIDevice pdi;
    TPIDevice tpi;
    LegacyDevice legacy(protocol);
    if(std::string(protocol)=="pdi") {
        pdi.memory[0x01000090]=0x1E;
        pdi.memory[0x01000091]=0x97;
        pdi.memory[0x01000092]=0x4C;
        pdi.attach();
    } else if(std::string(protocol)=="tpi") {
        tpi.memory[0x3FC0]=0x1E;
        tpi.memory[0x3FC2]=0x03;
        tpi.attach();
    } else {
        legacy.attach();
    }
    for(;;) {
        int high=std::cin.get(),low=std::cin.get();
        if(high==EOF || low==EOF) break;
        const unsigned count=(unsigned(high)<<8)|low;
        std::vector<uint8_t> result;
        if(count==0) {
            const uint32_t commits=pdi.commits.size();
            result={uint8_t(commits>>24),uint8_t(commits>>16),uint8_t(commits>>8),uint8_t(commits)};
        } else {
            check(count<=275,"oversize bridge packet");
            std::vector<uint8_t> body(count);
            std::cin.read(reinterpret_cast<char *>(body.data()),count);
            check(unsigned(std::cin.gcount())==count,"truncated bridge packet");
            result=dispatch(body);
        }
        std::cout.put(uint8_t(result.size()>>8));
        std::cout.put(uint8_t(result.size()));
        std::cout.write(reinterpret_cast<const char *>(result.data()),result.size());
        std::cout.flush();
    }
    return 0;
}

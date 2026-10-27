#pragma once
// Optional native backend only; no SDK import reaches portable builds.
#include "dynarmic/interface/A32/coprocessor.h"
#include <cstdint>

// These exact CP15 maintenance operations are stateless in cp15.cpp. WFI,
// control/MPU/TCM changes and other coprocessor instructions remain outside JIT.
constexpr bool nds_native_stateless_cache(unsigned crn,unsigned crm,unsigned op2) {
    return crn==7&&((crm==10&&(op2==2||op2==4))||((crm==6||crm==14)&&op2==1));
}
constexpr bool nds_native_stateless_cache_encoding(uint32_t word) {
    return (word>>28)!=15&&(word&0x0ff00f10u)==0x0e000f10u&&
        nds_native_stateless_cache((word>>16)&15,word&15,(word>>5)&7);
}

class NdsNativeCacheMaintenance final : public Dynarmic::A32::Coprocessor {
    uint32_t sink=0;
public:
    std::optional<Callback> CompileInternalOperation(bool,unsigned,Dynarmic::A32::CoprocReg,Dynarmic::A32::CoprocReg,Dynarmic::A32::CoprocReg,unsigned) override {return {};}
    CallbackOrAccessOneWord CompileSendOneWord(bool two,unsigned op1,Dynarmic::A32::CoprocReg crn,Dynarmic::A32::CoprocReg crm,unsigned op2) override {
        if(!two&&op1==0&&nds_native_stateless_cache(unsigned(crn),unsigned(crm),op2))return &sink;
        return {};
    }
    CallbackOrAccessTwoWords CompileSendTwoWords(bool,unsigned,Dynarmic::A32::CoprocReg) override {return {};}
    CallbackOrAccessOneWord CompileGetOneWord(bool,unsigned,Dynarmic::A32::CoprocReg,Dynarmic::A32::CoprocReg,unsigned) override {return {};}
    CallbackOrAccessTwoWords CompileGetTwoWords(bool,unsigned,Dynarmic::A32::CoprocReg) override {return {};}
    std::optional<Callback> CompileLoadWords(bool,bool,Dynarmic::A32::CoprocReg,std::optional<uint8_t>) override {return {};}
    std::optional<Callback> CompileStoreWords(bool,bool,Dynarmic::A32::CoprocReg,std::optional<uint8_t>) override {return {};}
};

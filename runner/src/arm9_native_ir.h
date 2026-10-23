#pragma once
// Imported only by the optional native implementation and its gated probe.
#include "dynarmic/frontend/A32/a32_ir_emitter.h"
#include "dynarmic/ir/opcodes.h"
#include "dynarmic/frontend/A32/a32_types.h"
#include "arm9_native_memory.h"
#include <cstdio>
#include <cstdlib>

inline void nds_arm9_finish_memory(Dynarmic::A32::IREmitter& ir,NdsArm9ReadKind kind=NdsArm9ReadKind::Word) {
    // The pinned frontend produces one read16 + extension for a halfword load.
    // Route that operation through the wide ARM9 callback, preserving both odd
    // address behavior and exact device read width. No frontend/dependency fork.
    for(auto at=ir.block.begin();at!=ir.block.end();++at) {
        using Op=Dynarmic::IR::Opcode;
        if(at->GetOpcode()!=Op::A32ReadMemory16)continue;
        Dynarmic::IR::Inst* extension=nullptr;
        for(auto user=ir.block.begin();user!=ir.block.end();++user) {
            if((user->GetOpcode()==Op::ZeroExtendHalfToWord||user->GetOpcode()==Op::SignExtendHalfToWord)&&
               !user->GetArg(0).IsImmediate()&&user->GetArg(0).GetInst()==&*at)extension=&*user;
        }
        if(!extension||at->UseCount()!=1) {
            std::fputs("[arm9-native] unsupported halfword IR shape for pinned frontend\n",stderr);std::abort();
        }
        kind=extension->GetOpcode()==Op::SignExtendHalfToWord?NdsArm9ReadKind::HalfSigned:NdsArm9ReadKind::HalfUnsigned;
        const auto saved=ir.current_location;
        ir.current_location=Dynarmic::A32::LocationDescriptor{Dynarmic::IR::LocationDescriptor{at->GetArg(0).GetU64()}};
        ir.SetInsertionPointBefore(at);
        const auto wide=ir.ReadMemory32(Dynarmic::IR::U32{at->GetArg(1)},at->GetArg(2).GetAccType());
        extension->ReplaceUsesWith(wide);at->Invalidate();ir.current_location=saved;
    }
    // Insert AFTER translation has established ARM predication. Emitting a PC
    // store in PreCodeTranslationHook makes a first conditional instruction look
    // non-leading and can manufacture a zero-work block linking to itself.
    bool tagged=false;
    for(auto at=ir.block.begin();at!=ir.block.end();++at) {
        using Op=Dynarmic::IR::Opcode;
        switch(at->GetOpcode()) {
        case Op::A32ReadMemory8:case Op::A32ReadMemory16:case Op::A32ReadMemory32:case Op::A32ReadMemory64:
        case Op::A32WriteMemory8:case Op::A32WriteMemory16:case Op::A32WriteMemory32:case Op::A32WriteMemory64: {
            const auto location=Dynarmic::A32::LocationDescriptor{Dynarmic::IR::LocationDescriptor{at->GetArg(0).GetU64()}};
            ir.SetInsertionPointBefore(at);
            // ARM9 has no VFP register file. Native S0 is private callback
            // metadata, written once before this instruction's memory accesses.
            // One store also survives register elimination for multiword loads.
            if(!tagged) {
                ir.SetExtendedRegister(Dynarmic::A32::ExtReg::S0,ir.Imm32(uint32_t(kind)));
                tagged=true;
            }
            ir.BranchWritePC(ir.Imm32(location.PC()));
            break;
        }
        default:break;
        }
    }
    ir.SetInsertionPointBefore(ir.block.end());
}

#pragma once
// Imported only by the optional native implementation and its gated probe.
#include "dynarmic/frontend/A32/a32_ir_emitter.h"
#include "dynarmic/ir/opcodes.h"

inline void nds_arm9_stamp_memory_context(Dynarmic::A32::IREmitter& ir) {
    // Insert AFTER translation has established ARM predication. Emitting a PC
    // store in PreCodeTranslationHook makes a first conditional instruction look
    // non-leading and can manufacture a zero-work block linking to itself.
    for(auto at=ir.block.begin();at!=ir.block.end();++at) {
        using Op=Dynarmic::IR::Opcode;
        switch(at->GetOpcode()) {
        case Op::A32ReadMemory8:case Op::A32ReadMemory16:case Op::A32ReadMemory32:case Op::A32ReadMemory64:
        case Op::A32WriteMemory8:case Op::A32WriteMemory16:case Op::A32WriteMemory32:case Op::A32WriteMemory64: {
            const auto location=Dynarmic::A32::LocationDescriptor{Dynarmic::IR::LocationDescriptor{at->GetArg(0).GetU64()}};
            ir.SetInsertionPointBefore(at);
            ir.BranchWritePC(ir.Imm32(location.PC()));
            break;
        }
        default:break;
        }
    }
    ir.SetInsertionPointBefore(ir.block.end());
}

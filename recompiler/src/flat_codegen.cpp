#include "flat_codegen.h"
#include "arm_codegen.h"
#include "arm_decode.h"
#include "thumb_decode.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
using namespace armv4t;
// Preserve source comments and strings; only the C return keyword is rewritten.
std::string region_returns(const std::string& input) {
    std::string out;
    for (std::size_t i=0;i<input.size();) {
        const auto begin=i;
        if (input[i]=='/' && i+1<input.size() && input[i+1]=='*') {
            auto end=input.find("*/",i+2);
            i=end==std::string::npos?input.size():end+2;
        } else if (input[i]=='/' && i+1<input.size() && input[i+1]=='/') {
            auto end=input.find('\n',i+2);i=end==std::string::npos?input.size():end;
        } else if (input[i]=='"' || input[i]=='\'') {
            const char quote=input[i++];
            while(i<input.size()) {
                if(input[i]=='\\' && i+1<input.size()){i+=2;continue;}
                if(input[i++]==quote)break;
            }
        } else if(std::isalpha(static_cast<unsigned char>(input[i])) || input[i]=='_') {
            do {++i;} while(i<input.size() &&
                (std::isalnum(static_cast<unsigned char>(input[i])) || input[i]=='_'));
            if(input.compare(begin,i-begin,"return")==0){out+="goto NDS_REGION_EXIT";continue;}
        } else ++i;
        out.append(input,begin,i-begin);
    }
    return out;
}
bool admitted(const Instr& in,const std::string& body) {
    if(in.is_undefined || (in.set_flags && in.rd==15))return false;
    if((in.op==IrOp::LDM || in.op==IrOp::STM) && !in.thumb && (in.raw&(1u<<22)))return false;
    switch(in.op) {
        case IrOp::MRS:case IrOp::MSR:case IrOp::MCR:case IrOp::MRC:
        case IrOp::CDP:case IrOp::SWI:return false;
        default:break;
    }
    // A helper which can change architectural state behind the local registers
    // is not silently admitted. Add supported operations with semantic tests.
    const std::set<std::string> allowed{
        "runtime_tick","runtime_should_yield","runtime_unwinding","runtime_slice_yield",
        "runtime_insn_slow","runtime_trace_event","runtime_live_transfer",
        "runtime_call_push_return","runtime_call_cancel_return","runtime_call_should_return",
        "runtime_dispatch","runtime_dispatch_with_exchange","runtime_dispatch_literal_call",
        "runtime_dispatch_literal_branch","runtime_dispatch_literal_fallthrough",
        "runtime_link_call","runtime_link_branch","runtime_link_fallthrough",
        "runtime_mem_cycles","runtime_mul_cycles","runtime_clz",
        "arm_cond_passes","arm_shift_lsl","arm_shift_lsr","arm_shift_asr","arm_shift_ror",
        "arm_set_nz","arm_set_nzc_logic","arm_set_nzcv_add","arm_set_nzcv_adc",
        "arm_set_nzcv_sub","arm_set_nzcv_sbc"};
    for(std::size_t i=0;i<body.size();) {
        if(!(std::isalpha(static_cast<unsigned char>(body[i])) || body[i]=='_')){++i;continue;}
        auto begin=i++;
        while(i<body.size() && (std::isalnum(static_cast<unsigned char>(body[i])) || body[i]=='_'))++i;
        const std::string token=body.substr(begin,i-begin);
        auto next=i;while(next<body.size() && std::isspace(static_cast<unsigned char>(body[next])))++next;
        if(next<body.size() && body[next]=='(' &&
            (token.rfind("runtime_",0)==0 || token.rfind("arm_set_",0)==0 ||
             token.rfind("arm_shift_",0)==0) && !allowed.count(token)) return false;
    }
    return true;
}
std::string hex(uint32_t v) {char s[24];std::snprintf(s,sizeof(s),"0x%08Xu",v);return s;}
std::string label(uint32_t v) {char s[24];std::snprintf(s,sizeof(s),"L_%08X",v);return s;}
}

std::string nds_emit_flat_region(const std::vector<ndsrecomp::Function>& functions,
    std::size_t first,std::size_t last,const uint8_t* image,
    std::size_t image_size,uint32_t image_base) {
    if(first>=last || last>functions.size())throw std::runtime_error("empty flat region");
    const bool thumb=functions[first].mode==ndsrecomp::CpuMode::Thumb;
    const uint32_t step=thumb?2:4;
    // This is exactly the span proven by the dispatch writer for this group.
    const uint32_t region_end=functions[last-1].end_addr;
    std::map<uint32_t,armv4t::Instr> instructions;
    for(auto n=first;n<last;++n) {
        const auto& f=functions[n];
        if((f.mode==ndsrecomp::CpuMode::Thumb)!=thumb)throw std::runtime_error("mixed-mode flat region");
        for(uint32_t pc=f.addr;pc<f.end_addr && uint64_t(pc)+step<=region_end;pc+=step) {
            const uint64_t source=uint64_t(f.source_addr?f.source_addr:f.addr)+(pc-f.addr);
            if(source<image_base || source-image_base+step>image_size)break;
            // A clipped ARM tail may have only half of its word proven. Let the
            // single-step floor read its actual bytes instead of extending proof.
            if(uint64_t(pc)+step>f.end_addr)break;
            const auto* p=image+(source-image_base);
            uint32_t word=p[0]|(uint32_t(p[1])<<8);
            if(!thumb)word|=(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
            instructions.emplace(pc,thumb?ThumbDecoder::decode(uint16_t(word),pc):ArmDecoder::decode(word,pc));
        }
    }
    std::ostringstream out;
    out<<"    NdsExecRegion _region;\n"
       <<"    if (!nds_region_begin(&_region)) { runtime_region_single_step(); return; }\n"
       <<"    goto NDS_REGION_TRANSFER;\n";
    CodegenCtx ctx;ctx.names_by_key=nullptr;ctx.inline_leaves=nullptr;
    ctx.current_function_thumb=thumb;
    for(const auto& [pc,in]:instructions) {
        bool missing=false;const std::string body=ArmCodegen::emit_instr(in,ctx,&missing);
        out<<label(pc)<<": {\n";
        if(!thumb && (pc==0x02147FD8u || pc==0x021483A0u || pc==0x02147288u))
            out<<"    if (g_nds_region_hle_vectors) { _region.cpu.R[15]="<<hex(pc)<<"; goto NDS_REGION_EXIT; }\n";
        if(missing || !admitted(in,body)) {
            out<<"    _region.cpu.R[15]="<<hex(pc)<<"; _region.single_step=1; goto NDS_REGION_EXIT;\n";
        } else {
            out<<region_returns(body);
            // Stores can invalidate executing bytes. Reads/writes of hardware
            // publish state and request an exit after this instruction completes.
            const bool memory=in.op>=IrOp::LDR && in.op<=IrOp::SWPB;
            if(memory)out<<"    if (nds_region_needs_exit(&_region)) goto NDS_REGION_EXIT;\n";
            // Bounded instruction checkpoints also cover straight-line code.
            if(((pc/step)&15u)==15u)
                out<<"    if (nds_region_needs_exit(&_region)) goto NDS_REGION_EXIT;\n";
            if(instructions.count(pc+step))out<<"    goto "<<label(pc+step)<<";\n";
            else out<<"    goto NDS_REGION_TRANSFER;\n";
        }
        out<<"}\n";
    }
    out<<"NDS_REGION_TRANSFER:\n"
       <<"    if (nds_region_needs_exit(&_region)) goto NDS_REGION_EXIT;\n"
       <<"    if (((_region.cpu.cpsr & CPSR_T_BIT)!=0) != "<<(thumb?1:0)<<") goto NDS_REGION_EXIT;\n"
       <<"    switch (_region.cpu.R[15]) {\n";
    for(const auto& [pc,in]:instructions)out<<"    case "<<hex(pc)<<": goto "<<label(pc)<<";\n";
    out<<"    default: if (_region.cpu.R[15]>="<<hex(functions[first].addr)
       <<" && _region.cpu.R[15]<"<<hex(region_end)
       <<") _region.single_step=1; goto NDS_REGION_EXIT;\n    }\n"
       <<"NDS_REGION_EXIT:\n    nds_region_end(&_region);\n";
    return out.str();
}

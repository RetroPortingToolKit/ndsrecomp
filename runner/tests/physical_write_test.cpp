#include "runtime_arm.h"
#include "physical_write.h"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
const uint32_t* notified=nullptr;
uint32_t notified_count=0,calls=0;
void require(bool condition,const char* message) {
    if(!condition) {std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}
}
}
extern "C" void runtime_note_code_write_pages(const uint32_t* first,uint32_t count) {
    notified=first;notified_count=count;++calls;
    require(first[0]!=0&&first[count-1]!=0,"generation bumps precede notification and skip zero");
}

int main() {
    std::array<uint8_t,4*4096> bytes{},written{};
    std::array<uint32_t,4> generation{0xffffffffu,5,9,11};
    NdsBusFastWin backing{bytes.data(),written.data(),generation.data(),uint32_t(bytes.size()-1)};
    nds_busf_note_write(&backing,4094,4);
    require(calls==1&&notified==generation.data()&&notified_count==2,
            "one crossing write publishes its complete physical range once");
    require(generation[0]==1&&generation[1]==6&&generation[2]==9,"only touched generations change");
    for(unsigned i=0;i<written.size();++i)
        require(written[i]==(i>=4094&&i<4098),"exact per-byte write provenance");
    NdsBusFastWin alias=backing;
    nds_busf_note_write(&alias,4096,2);
    require(calls==2&&notified==generation.data()+1&&notified_count==1&&generation[1]==7,
            "aliases publish the same physical cell");
    require(nds_physical_write_may_touch(generation.data(),2,generation.data()+1),"last page included");
    require(!nds_physical_write_may_touch(generation.data()+1,1,generation.data()),"preceding page excluded");
    require(!nds_physical_write_may_touch(generation.data(),2,generation.data()+2),"following page excluded");
    uint32_t other=0;
    require(!nds_physical_write_may_touch(generation.data(),2,&other),"unrelated backing excluded");
    require(nds_physical_write_may_touch(nullptr,0,&other)&&
            nds_physical_write_may_touch(generation.data(),2,nullptr),"unknown identity stays conservative");
    std::puts("PHYSICAL_WRITE passed: C ABI publisher, cross-page range, wrap, exact provenance, aliases and conservative guards");
}

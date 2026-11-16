#include "native_asset_data.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

namespace {
unsigned checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
void put16(std::vector<uint8_t>& b, size_t p, uint16_t v) {
    b[p] = uint8_t(v); b[p + 1] = uint8_t(v >> 8);
}
void put32(std::vector<uint8_t>& b, size_t p, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (i * 8));
}
nds_asset_data::View view(const std::vector<uint8_t>& b) {
    return {b.data(), 0, uint32_t(b.size())};
}
}
int main(int argc, char** argv) {
    using namespace nds_asset_data;
    std::vector<uint8_t> out{99};
    const std::vector<uint8_t> literals{0x10,3,0,0,0,'a','b','c'};
    require(decode(view(literals), &out) && out == std::vector<uint8_t>({'a','b','c'}), "LZ literals");
    const std::vector<uint8_t> overlap{0x10,18,0,0,0x40,'A',0xe0,0};
    require(decode(view(overlap), &out) && out == std::vector<uint8_t>(18,'A'), "LZ forward overlap");
    uint32_t validated=0;
    require(validate(view(overlap),&validated)&&validated==18,"Complete stream validation before guest allocation");
    const std::vector<uint8_t> flags{0x10,9,0,0,0,'A','B','C','D','E','F','G','H',0,'I'};
    require(decode(view(flags), &out) && out == std::vector<uint8_t>({'A','B','C','D','E','F','G','H','I'}), "LZ second flag group");
    const std::vector<uint8_t> rle{0x30,9,0,0,0x81,'Z',4,'a','b','c','d','e'};
    require(decode(view(rle), &out) && out == std::vector<uint8_t>({'Z','Z','Z','Z','a','b','c','d','e'}), "RLE repeat/literals");
    for (const auto& bad : std::vector<std::vector<uint8_t>>{
        {0x10,1,0,0}, {0x10,3,0,0,0x80,0,0},
        {0x10,17,0,0,0x40,'A',0xe0,0}, {0x30,2,0,0,0x80,'A'},
        {0x20,1,0,0,0}, {0x10,0,0,0}}) {
        out = {99};
        require(!decode(view(bad), &out) && out == std::vector<uint8_t>({99}), "Malformed input must not publish partial output");
        validated=123;
        require(!validate(view(bad),&validated)&&validated==123,"Malformed stream must decline before guest allocation");
    }
    uint32_t count = 123;
    require(!inspect(view(overlap), &count, 17) && count == 123, "Output bound");

    std::vector<uint8_t> rom(0x300);
    put32(rom,0x40,0x100); put32(rom,0x44,0x80);
    put32(rom,0x48,0x180); put32(rom,0x4c,16);
    put32(rom,0x100,16); put16(rom,0x104,0); put16(rom,0x106,2);
    put32(rom,0x108,40); put16(rom,0x10c,1); put16(rom,0x10e,0xf000);
    rom[0x110]=0x86;
    std::copy_n("Course",6,rom.begin()+0x111); put16(rom,0x117,0xf001);
    rom[0x119]=8; std::copy_n("root.bin",8,rom.begin()+0x11a); rom[0x122]=0;
    rom[0x128]=9; std::copy_n("test.carc",9,rom.begin()+0x129); rom[0x132]=0;
    put32(rom,0x180,0x200); put32(rom,0x184,0x203);
    put32(rom,0x188,0x210); put32(rom,0x18c,0x210+uint32_t(overlap.size()));
    std::copy_n("raw",3,rom.begin()+0x200);
    std::copy(overlap.begin(),overlap.end(),rom.begin()+0x210);
    View file;
    for (const auto name : {"/Course/test.carc", "rom:/Course/test.carc", "Course/./test.carc", "Course/../Course/test.carc"})
        require(find_file(rom.data(),uint32_t(rom.size()),name,&file) && file.offset==0x210 && file.size==overlap.size(), "NitroFS directory lookup");
    require(find_file(rom.data(),uint32_t(rom.size()),"test.carc",&file,0xf001) && decode(file,&out) && out==std::vector<uint8_t>(18,'A'), "Relative NitroFS file through complete decode");
    require(find_file(rom.data(),uint32_t(rom.size()),"../root.bin",&file,0xf001) && file.size==3 && file.data[0]=='r', "NitroFS parent");
    for (const auto name : {"save:/Course/test.carc", "/Course/", "/Course/missing", "/Course/test.carc/extra"})
        require(!find_file(rom.data(),uint32_t(rom.size()),name,&file), "Unknown/non-file path must decline");
    put32(rom,0x18c,0x301);
    require(!find_file(rom.data(),uint32_t(rom.size()),"Course/test.carc",&file), "FAT extent must fit cartridge");
    rom[0x110]=0x80;
    require(!find_file(rom.data(),uint32_t(rom.size()),"Course/test.carc",&file), "Malformed directory record");

    if (argc == 3) {
        std::ifstream input(argv[1],std::ios::binary);
        std::vector<uint8_t> cartridge((std::istreambuf_iterator<char>(input)),{});
        require(!cartridge.empty() && cartridge.size()<=UINT32_MAX, "Private cartridge input");
        require(find_file(cartridge.data(),uint32_t(cartridge.size()),argv[2],&file), "Private cartridge path lookup");
        require(decode(file,&out), "Private cartridge complete decode");
        std::cout << "file offset=" << file.offset << " compressed=" << file.size << " decoded=" << out.size() << " prefix=";
        for (unsigned i=0;i<4&&i<out.size();++i) std::cout << char(out[i]);
        std::cout << '\n';
    }
    std::cout << checks << " asset data checks passed\n";
}

#include "guest_job_profile.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void check(bool good) { if (!good) std::abort(); }
int main() {
    uint32_t pc = 0x02000100, cpsr = 0x1F, cpu = 1, depth = 0, returns[80]{};
    for (uint32_t i = 0; i < 80; ++i) returns[i] = 0x02001001 + 4 * i;
    NdsGuestJobSources sources{&pc, &cpsr, &cpu, returns, &depth, 80};
    const char* path = "guest_job_profile_test.bin";
    check(!nds_guest_jobs_start(sources, nullptr, 4));
    check(!nds_guest_jobs_start(sources, path, 3));
    check(nds_guest_jobs_start(sources, path, 4));
    check(!nds_guest_jobs_start(sources, path, 4));
    for (uint32_t i = 0; i < 6; ++i) {
        depth = i;
        pc += 4;
        nds_guest_jobs_capture(100 + i);
    }
    depth = 81;
    nds_guest_jobs_capture(106);  // rejected before reading out of bounds
    depth = 80;
    nds_guest_jobs_capture(107);  // truncation preserves the innermost 64 frames
    cpu = 9;
    nds_guest_jobs_capture(108);
    check(nds_guest_jobs_finish());
    check(nds_guest_jobs_finish());
    std::FILE* f = std::fopen(path, "rb");
    check(f != nullptr);
    std::string header;
    int c;
    while ((c = std::fgetc(f)) != '\n' && c != EOF) header += char(c);
    check(header.find("\"sample_count\":4") != std::string::npos);
    check(header.find("\"evicted\":3") != std::string::npos);
    check(header.find("\"invalid\":2") != std::string::npos);
    check(header.find("\"truncated\":1") != std::string::npos);
    NdsGuestJobSample rows[4]{};
    check(std::fread(rows, sizeof(rows), 1, f) == 1);
    check(std::fgetc(f) == EOF);
    check(rows[0].qpc == 103 && rows[2].qpc == 105 && rows[3].qpc == 107);
    check(rows[0].full_depth == 3 && rows[0].returns[0] == returns[2]);
    check(rows[3].full_depth == 80 && rows[3].returns[63] == returns[16]);
    check(rows[3].cpu == 1 && rows[3].cpsr == cpsr && rows[3].pc == pc);
    std::fclose(f);
    check(std::remove(path) == 0);
    std::puts("guest_job_profile_test passed");
}

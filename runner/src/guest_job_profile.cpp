#include "guest_job_profile.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>

namespace {
NdsGuestJobSources sources{};
std::unique_ptr<NdsGuestJobSample[]> samples;
std::string output;
uint32_t capacity = 0;
uint64_t written = 0, invalid = 0, truncated = 0;
}

bool nds_guest_jobs_start(const NdsGuestJobSources& in, const char* path,
                          uint32_t count) {
    if (samples || !path || !*path || !in.pc || !in.cpsr || !in.cpu ||
        !in.returns || !in.depth || !in.return_capacity || count < 2 ||
        count > 262144 || (count & (count - 1))) return false;
    auto storage = std::unique_ptr<NdsGuestJobSample[]>(
        new (std::nothrow) NdsGuestJobSample[count]);
    if (!storage) return false;
    sources = in;
    output = path;
    capacity = count;
    written = invalid = truncated = 0;
    samples = std::move(storage);
    return true;
}

void nds_guest_jobs_capture(uint64_t qpc) {
    if (!samples) return;
    NdsGuestJobSample s{};
    s.qpc = qpc;
    // The sampler suspended the only writer. Like its bounded host stack copy,
    // these are copies from stable, pre-registered process-lifetime storage.
    std::memcpy(&s.cpu, sources.cpu, sizeof(s.cpu));
    std::memcpy(&s.full_depth, sources.depth, sizeof(s.full_depth));
    if (s.cpu > 1 || s.full_depth > sources.return_capacity) {
        ++invalid;
        return;
    }
    std::memcpy(&s.pc, sources.pc, sizeof(s.pc));
    std::memcpy(&s.cpsr, sources.cpsr, sizeof(s.cpsr));
    const uint32_t n = std::min(s.full_depth, kNdsGuestJobFrames);
    for (uint32_t i = 0; i < n; ++i)
        s.returns[i] = sources.returns[s.full_depth - 1 - i];
    truncated += s.full_depth > kNdsGuestJobFrames;
    samples[written & (capacity - 1)] = s;
    ++written;
}

bool nds_guest_jobs_finish() {
    if (!samples) return true;
    std::FILE* f = std::fopen(output.c_str(), "wb");
    bool ok = f != nullptr;
    const uint64_t count = std::min<uint64_t>(written, capacity);
    if (f) {
        ok = std::fprintf(f,
            "{\"format\":\"ndsrecomp-guest-jobs\",\"version\":1,"
            "\"sample_bytes\":%u,\"sample_count\":%llu,\"written\":%llu,"
            "\"evicted\":%llu,\"invalid\":%llu,\"truncated\":%llu,"
            "\"clock\":\"hostprof-qpc\",\"frames\":64,"
            "\"order\":\"oldest-first\",\"context_is_not_execution_proof\":true}\n",
            unsigned(sizeof(NdsGuestJobSample)),
            static_cast<unsigned long long>(count),
            static_cast<unsigned long long>(written),
            static_cast<unsigned long long>(written - count),
            static_cast<unsigned long long>(invalid),
            static_cast<unsigned long long>(truncated)) > 0;
        for (uint64_t i = written - count; ok && i < written; ++i)
            ok = std::fwrite(&samples[i & (capacity - 1)],
                             sizeof(NdsGuestJobSample), 1, f) == 1;
        if (std::fclose(f) != 0) ok = false;
    }
    samples.reset();
    output.clear();
    return ok;
}

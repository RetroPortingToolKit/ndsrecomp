#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>
#include <array>

// Physical generation pointers preserve aliases. Only host validation is
// coalesced; guest writes and their provenance counters remain immediate.
class NdsArm9GenerationWatch {
    struct Watch { const uint32_t* generation; uint32_t value; };
    std::vector<Watch> watches;
    std::unordered_map<const uint32_t*,std::size_t> indices;
#if defined(NDS_NATIVE_PRECISE_WRITES)
    std::array<uint64_t,8> filter{};
    static std::size_t filter_bit(const uint32_t* generation) {
        return (reinterpret_cast<std::uintptr_t>(generation)>>2)&511u;
    }
#endif
    bool pending=false;
public:
    uint64_t checks=0;
    void add(const uint32_t* generation) {
        if(indices.emplace(generation,watches.size()).second) {
            watches.push_back({generation,*generation});
#if defined(NDS_NATIVE_PRECISE_WRITES)
            const auto bit=filter_bit(generation);filter[bit>>6]|=uint64_t(1)<<(bit&63);
#endif
        }
    }
    bool contains(const uint32_t* generation) const {
#if defined(NDS_NATIVE_PRECISE_WRITES)
        const auto bit=filter_bit(generation);
        if(!(filter[bit>>6]&(uint64_t(1)<<(bit&63))))return false;
#endif
        return indices.find(generation)!=indices.end();
    }
    std::size_t size() const {return watches.size();}
    void clear() {
        watches.clear();indices.clear();pending=false;
#if defined(NDS_NATIVE_PRECISE_WRITES)
        filter.fill(0);
#endif
    }
    void defer() {pending=true;}
    bool take_pending() {return std::exchange(pending,false);}
    bool changed() {
        for(const auto& watch:watches) {
            ++checks;
            if(*watch.generation!=watch.value)return true;
        }
        return false;
    }
};

inline bool nds_arm9_can_defer_generation_check(bool running,bool device_halted) {
    // A live block must keep immediate validation unless a completed-device
    // handoff already guarantees exit before the next native instruction.
    return !running||device_halted;
}

// A bus callback may use an exact physical page only during the corresponding
// native RAM store. Nested device/DMA scopes use nullptr and restore the outer
// proof on return. The context belongs to the single guest-execution thread.
struct NdsArm9PreciseStore {
    const uint32_t*& current;
    const uint32_t* previous;
    NdsArm9PreciseStore(const uint32_t*& current,const uint32_t* generation)
        :current(current),previous(std::exchange(current,generation)) {}
    ~NdsArm9PreciseStore() {current=previous;}
};

template<class NativeRange>
bool nds_arm9_notify_precise_write(const uint32_t* generation,NativeRange& natives,int writer=-1) {
    if(!generation)return false;
    int index=0;
    for(auto& n:natives) {
        const bool writing_cpu=index++==writer;
        if(!n||writing_cpu)continue; // writer already tests its exact page after its bus store
        ++n->precise_notifications;
        if(n->watches.contains(generation)) {++n->precise_code_hits;n->clear();}
    }
    return true;
}

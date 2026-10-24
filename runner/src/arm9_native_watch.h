#pragma once
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

// Physical generation pointers preserve aliases. Only host validation is
// coalesced; guest writes and their provenance counters remain immediate.
class NdsArm9GenerationWatch {
    struct Watch { const uint32_t* generation; uint32_t value; };
    std::vector<Watch> watches;
    std::unordered_map<const uint32_t*,std::size_t> indices;
    bool pending=false;
public:
    uint64_t checks=0;
    void add(const uint32_t* generation) {
        if(indices.emplace(generation,watches.size()).second)
            watches.push_back({generation,*generation});
    }
    bool contains(const uint32_t* generation) const {return indices.find(generation)!=indices.end();}
    std::size_t size() const {return watches.size();}
    void clear() {watches.clear();indices.clear();pending=false;}
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

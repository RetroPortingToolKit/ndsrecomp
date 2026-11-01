#pragma once
#include "runtime_arm.h"
#include "native_ram.h"
#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

// Page is the SDK's public {data,written,generation} descriptor, or the same
// contract in the SDK-free focused fixture. The table owns no guest backing.
// Refresh only at a native execution boundary after the CPU mapping is selected.
template<class Page>
class NdsNativeStorePages {
    using Table = std::array<Page*, 1u << 20>;
    std::unique_ptr<Table> table = std::make_unique<Table>();
    std::unordered_map<uint32_t*, Page> physical;
    std::vector<std::pair<uint32_t, Page*>> aliases;
public:
    NdsNativeStorePages() { table->fill(nullptr); }
    Page* const* data() const { return table->data(); }
    Page* at(uint32_t address) const { return (*table)[address >> 12]; }
    std::size_t mapped_aliases() const { return aliases.size(); }

    template<class Protected>
    void refresh(const Protected& protected_pages) {
        table->fill(nullptr);
        aliases.clear();
        physical.clear();
        const auto add = [&](uint64_t first, uint64_t limit) {
            for (uint64_t address = first & ~uint64_t{4095};
                 address < limit && address < 0x100000000ull; address += 4096) {
                // This virtual page has an instruction-context observer. Its
                // aliases keep their own original logical-address behavior.
                if (address == 0x027e0000u) continue;
                const auto page=NdsNativeRam::page(uint32_t(address));
                if(!page.can_publish())continue;
                const auto* window=page.window;
                const uint32_t lo=page.offset;
                auto* generation = window->gen + (lo >> 12);
                auto [entry, inserted] = physical.try_emplace(generation,
                    Page{window->data + lo, window->written + lo, generation});
                (void)inserted;
                auto* descriptor = &entry->second;
                const uint32_t index = uint32_t(address >> 12);
                aliases.emplace_back(index, descriptor);
                if (!protected_pages.contains(generation)) (*table)[index] = descriptor;
            }
        };
        add(0x02000000u, 0x04000000u);
        if (g_nds_active == NDS_ARM9) {
            add(0, g_busf_itcm_limit);
            add(g_busf_dtcm_base, uint64_t{g_busf_dtcm_base} + g_busf_dtcm_limit);
        }
    }

    // A newly translated block immediately protects every virtual alias of its
    // physical code page, including the other CPU's view. Protection is kept
    // conservatively until a whole native-cache reset, not removed mid-Run.
    void protect(const uint32_t* generation) {
        for (const auto& [index, descriptor] : aliases)
            if (descriptor->generation == generation) (*table)[index] = nullptr;
    }
};

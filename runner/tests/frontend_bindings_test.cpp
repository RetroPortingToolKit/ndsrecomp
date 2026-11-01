#include "../../launcher/nds_input_bindings.h"
#include "frontend_window_size.h"

#include <cstdio>
#include <cstdlib>

void require(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main() {
    require(nds_binding_mask(kNdsDefaultKeys, 29) == 1, "Z accelerates with DS A");
    require(nds_binding_mask(kNdsDefaultKeys, 27) == 2, "X is DS B, not SNES A");
    auto keys = kNdsDefaultKeys;
    keys[4] = 6;  // A -> C
    keys[5] = 0;  // B explicitly unbound
    NdsButtonBindings loaded{};
    require(nds_parse_bindings(nds_format_bindings(keys), false, &loaded) && loaded == keys,
            "edited and unbound keys survive transport");
    require(nds_binding_mask(loaded, 29) == 0 && nds_binding_mask(loaded, 6) == 1 &&
            nds_binding_mask(loaded, 0) == 0, "rebind replaces old key and preserves unbind");
    auto before = loaded;
    for (const char* invalid : {"", "1,2", "-1,0,0,0,0,0,0,0,0,0,0,0",
         "512,0,0,0,0,0,0,0,0,0,0,0", "0,0,0,0,0,0,0,0,0,0,0,0,", "0,0,0,0,0,0,0,0,0,0,0,x"}) {
        require(!nds_parse_bindings(invalid, false, &loaded) && loaded == before,
                "invalid input cannot partially overwrite bindings");
    }
    keys[5] = keys[4];
    require(nds_binding_mask(keys, 6) == 3, "shared physical key presses both assigned buttons");
    require(nds_binding_mask(kNdsDefaultPad, 1) == 1, "default gamepad south is A");
    require(nds_parse_bindings("12,13,14,15,111,2,3,4,109,11,7,5", true, &loaded),
            "signed gamepad axes supported");
    require(!nds_parse_bindings("12,13,14,15,112,2,3,4,10,11,7,5", true, &loaded),
            "nonexistent axis rejected");
    auto pad = kNdsDefaultPad;
    pad[5] = 0;  // Explicitly unbound B stays unbound beside SDL3 additions.
    for (int button = 22; button <= 26; ++button) {
        pad[4] = button;  // SDL3 MISC2..MISC6, encoded as hardware button + 1.
        require(nds_parse_bindings(nds_format_bindings(pad), true, &loaded) && loaded == pad,
                "captured SDL3 auxiliary button survives transport");
        require(nds_binding_mask(loaded, button) == 1 && nds_binding_mask(loaded, 0) == 0,
                "SDL3 auxiliary button maps A and preserves explicit unbind");
    }
    before = loaded;
    require(!nds_parse_bindings("12,13,14,15,27,0,3,4,10,11,7,5", true, &loaded) && loaded == before,
            "button beyond SDL3 public range cannot partially overwrite map");
    const auto small = nds_initial_window_size(448, 256, true, 3, 1, 1024, 768);
    require(small.top_width + small.bottom_width + 32 <= 1024 && small.height <= 688,
            "both windows fit a small desktop");
    const auto tv = nds_initial_window_size(448, 256, true, 3, 2.25f, 3840, 2080);
    require(tv.top_width > 1344 && tv.top_width + tv.bottom_width + 32 <= 3840,
            "225 percent DPI enlarges windows and keeps both visible");
    const auto normal = nds_initial_window_size(448, 256, true, 3, 1, 3840, 2080);
    require(normal.top_width == 1344 && normal.height == 576,
            "100 percent DPI respects requested 3x scale");
    const auto stacked = nds_initial_window_size(448, 256, false, 8, 2.25f, 1920, 1040);
    require(stacked.top_width <= 1824 && stacked.height <= 960,
            "stacked screens fit vertically");
    std::puts("frontend binding and DPI sizing checks passed");
}

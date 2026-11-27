#include "shared_input_event.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"input parity failed at %d\n",__LINE__); std::abort(); } } while(0)
int main() {
    NdsSharedInputEvents input;
    auto check = [&](const NdsButtonBindings& keys, const NdsButtonBindings& pad) {
        for (int key = -1; key < 512; ++key)
            CHECK(input.keyboard(keys, key) == nds_binding_mask(keys, key));
        for (int button = -1; button < 32; ++button)
            CHECK(input.gamepad(pad, button) == nds_binding_mask(pad, button + 1));
    };
    check(kNdsDefaultKeys, kNdsDefaultPad);
    auto keys = kNdsDefaultKeys; auto pad = kNdsDefaultPad;
    keys[0] = keys[4]; keys[1] = 0; keys[11] = 511;
    pad[0] = pad[4]; pad[1] = 0; pad[11] = kNdsMaxPadButtonCode;
    check(keys, pad); // duplicate assignments, explicit unbind and extended buttons
    pad[0] = 100; pad[1] = 111;
    check(keys, pad); // signed axes stay with the existing separate polling path
    check(kNdsDefaultKeys, kNdsDefaultPad); // live reconfiguration restores defaults
}

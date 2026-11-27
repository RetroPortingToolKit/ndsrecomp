#pragma once
#include "../../launcher/nds_input_bindings.h"
#include <recomp_input.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>

// Event snapshots use shared evaluation; DS binding order, rebinding and the
// existing separately polled axis/stylus/controller ownership remain local.
class NdsSharedInputEvents {
    struct Map {
        std::unique_ptr<ri_context, decltype(&ri_destroy)> context{
            ri_create(1, 12, nullptr, nullptr), &ri_destroy};
        NdsButtonBindings configured{};
        bool ready = false;
    };
    Map keyboard_, gamepad_;
    static void configure(Map& map, const NdsButtonBindings& bindings, bool pad) {
        if (!map.context) throw std::bad_alloc();
        if (map.ready && map.configured == bindings) return;
        ri_player player{};
        player.sources = pad ? RI_SOURCE_GAMEPAD : RI_SOURCE_KEYBOARD;
        player.deadzone_pct = 25;
        if (pad) std::strcpy(player.guid, "nds-event");
        constexpr unsigned actions[] = {6, 7, 5, 4, 0, 1, 10, 11, 9, 8, 3, 2};
        for (unsigned i = 0; i < 12; ++i) {
            if (!nds_valid_binding(bindings[i], pad))
                throw std::invalid_argument("invalid NDS input binding");
            if (pad) player.pad_binding[actions[i]] = bindings[i];
            else player.key_binding[actions[i]] = static_cast<uint16_t>(bindings[i]);
        }
        if (!ri_configure(map.context.get(), 0, &player))
            throw std::invalid_argument("invalid NDS input configuration");
        map.configured = bindings;
        map.ready = true;
    }
public:
    uint16_t keyboard(const NdsButtonBindings& bindings, int scancode) {
        configure(keyboard_, bindings, false);
        uint8_t keys[RI_MAX_KEYS]{};
        if (scancode >= 0 && scancode < RI_MAX_KEYS) keys[scancode] = 1;
        ri_update(keyboard_.context.get(), nullptr, 0, keys, RI_MAX_KEYS);
        return static_cast<uint16_t>(ri_sample(keyboard_.context.get(), 0));
    }
    uint16_t gamepad(const NdsButtonBindings& bindings, int button) {
        configure(gamepad_, bindings, true);
        ri_device device{};
        device.id = 1;
        std::strcpy(device.guid, "nds-event");
        if (button >= 0 && button < 32) device.buttons = 1u << button;
        ri_update(gamepad_.context.get(), &device, 1, nullptr, 0);
        return static_cast<uint16_t>(ri_sample(gamepad_.context.get(), 0));
    }
};

#pragma once

// Stable persisted indices: existing stacked/separate choices remain 0/1.
// Hosts that permit swapping expose the final four choices. Title launchers
// can remove that capability completely without changing shared UI settings.
struct NdsLauncherPipCapabilities { bool swap_main = true; };
inline constexpr const char* kNdsScreenLayoutLabels[] = {
    "Stacked window", "Separate windows",
    "Picture in picture: bottom right", "Picture in picture: bottom left",
    "Picture in picture: top right", "Picture in picture: top left",
    "Bottom screen main: PiP bottom right", "Bottom screen main: PiP bottom left",
    "Bottom screen main: PiP top right", "Bottom screen main: PiP top left",
};
inline constexpr int nds_launcher_screen_layout_count(NdsLauncherPipCapabilities cap = {}) {
    return cap.swap_main ? 10 : 6;
}
inline constexpr const char* nds_launcher_screen_layout_arg(int layout) {
    return layout >= 2 ? "pip" : layout == 1 ? "separate" : "stacked";
}
inline constexpr const char* nds_launcher_pip_corner_arg(int layout) {
    if (layout < 2 || layout > 9) return "bottom-right";
    switch ((layout-2)%4) {
        case 1: return "bottom-left";
        case 2: return "top-right";
        case 3: return "top-left";
        default: return "bottom-right";
    }
}
inline constexpr const char* nds_launcher_pip_main_arg(int layout, NdsLauncherPipCapabilities cap = {}) {
    return cap.swap_main && layout >= 6 && layout <= 9 ? "bottom" : "top";
}

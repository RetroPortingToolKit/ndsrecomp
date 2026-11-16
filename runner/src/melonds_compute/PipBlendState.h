// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "glad/glad.h"

namespace nds_survivor_present {
// Only translucent presentation uses this guard; opaque/observer draws add
// no state queries. Destruction also covers draw failure/early returns.
class PipBlendState {
    bool active;
    GLboolean enabled{};
    GLint sourceRgb{}, destinationRgb{}, sourceAlpha{}, destinationAlpha{};
    GLint equationRgb{}, equationAlpha{};
public:
    explicit PipBlendState(bool translucent) : active(translucent) {
        if (!active) return;
        enabled = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_BLEND_SRC_RGB, &sourceRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &destinationRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &sourceAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &destinationAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &equationRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equationAlpha);
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    }
    ~PipBlendState() {
        if (!active) return;
        glBlendEquationSeparate(equationRgb, equationAlpha);
        glBlendFuncSeparate(sourceRgb, destinationRgb, sourceAlpha, destinationAlpha);
        if (enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    }
    PipBlendState(const PipBlendState&) = delete;
    PipBlendState& operator=(const PipBlendState&) = delete;
};
}

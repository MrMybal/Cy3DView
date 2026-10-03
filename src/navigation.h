// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "cy_math.h"

namespace cy {
struct NavigationInput {
    bool w = false, a = false, s = false, d = false, z = false, q = false;
    bool boost = false, focused = true, blocked = false;
    Vec3 direction() const {
        if (!focused || blocked)
            return {};
        return {float(d) - float(a || q), 0, float(w || z) - float(s)};
    }
    bool apply(Camera& camera, float seconds) const {
        const auto input = direction();
        if (dot(input, input) == 0 || seconds <= 0)
            return false;
        camera.move(input.z, input.x, seconds, boost ? 4.f : 1.f);
        return true;
    }
};
} // namespace cy

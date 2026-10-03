// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "cy3d_plugin.h"
#include "cy_math.h"
#include <string>
#include <vector>
#include <array>
namespace cy {
struct AnimationData {
    struct Track {
        uint32_t node;
        std::vector<Cy3DKey> positions, rotations, scales;
    };
    struct Clip {
        std::string name;
        double duration;
        std::vector<Track> tracks;
    };
    std::vector<Cy3DNode> nodes;
    std::vector<Clip> clips;
    explicit AnimationData(const Cy3DScene& scene);
    std::vector<Mat4> evaluate(int clip, double time) const;
};
std::array<float, 9> normalMatrix(const Mat4& matrix);
} // namespace cy

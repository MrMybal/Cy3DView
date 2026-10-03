// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "animation.h"
#include <stdexcept>
namespace cy {
namespace {
std::array<float, 4> sample(const std::vector<Cy3DKey>& keys, double time, const float* fallback,
                            bool rotation) {
    std::array<float, 4> result{};
    std::copy_n(fallback, rotation ? 4 : 3, result.begin());
    if (keys.empty())
        return result;
    auto right = std::upper_bound(keys.begin(), keys.end(), time,
                                  [](double t, const Cy3DKey& key) { return t < key.time; });
    if (right == keys.begin()) {
        std::copy_n(right->value, 4, result.begin());
        return result;
    }
    auto left = right - 1;
    if (right == keys.end() || left->step) {
        std::copy_n(left->value, 4, result.begin());
        return result;
    }
    float t = static_cast<float>((time - left->time) / std::max(right->time - left->time, 1e-12));
    float a = 1 - t, b = t, sign = 1;
    if (rotation) {
        float cosine = 0;
        for (int i = 0; i < 4; ++i)
            cosine += left->value[i] * right->value[i];
        if (cosine < 0) {
            cosine = -cosine;
            sign = -1;
        }
        if (cosine < .9995f) {
            float angle = std::acos(std::clamp(cosine, -1.f, 1.f));
            a = std::sin((1 - t) * angle) / std::sin(angle);
            b = std::sin(t * angle) / std::sin(angle);
        }
    }
    for (int i = 0; i < 4; ++i)
        result[i] = a * left->value[i] + b * sign * right->value[i];
    if (rotation) {
        float length = 0;
        for (float v : result)
            length += v * v;
        if (length > 1e-20f)
            for (float& v : result)
                v /= std::sqrt(length);
    }
    return result;
}
Mat4 trs(const std::array<float, 4>& p, const std::array<float, 4>& q, const std::array<float, 4>& s) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    auto m = Mat4::identity();
    m.v[0] = (1 - 2 * (y * y + z * z)) * s[0];
    m.v[1] = 2 * (x * y + z * w) * s[0];
    m.v[2] = 2 * (x * z - y * w) * s[0];
    m.v[4] = 2 * (x * y - z * w) * s[1];
    m.v[5] = (1 - 2 * (x * x + z * z)) * s[1];
    m.v[6] = 2 * (y * z + x * w) * s[1];
    m.v[8] = 2 * (x * z + y * w) * s[2];
    m.v[9] = 2 * (y * z - x * w) * s[2];
    m.v[10] = (1 - 2 * (x * x + y * y)) * s[2];
    m.v[12] = p[0];
    m.v[13] = p[1];
    m.v[14] = p[2];
    return m;
}
} // namespace
AnimationData::AnimationData(const Cy3DScene& scene) {
    if (scene.node_count && !scene.nodes)
        throw std::runtime_error("Hierarchie absente");
    if (scene.animation_count && !scene.animations)
        throw std::runtime_error("Clips absents");
    if (scene.node_count)
        nodes.assign(scene.nodes, scene.nodes + scene.node_count);
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].parent < -1 || nodes[i].parent >= static_cast<int64_t>(i))
            throw std::runtime_error("Hierarchie invalide");
        for (float value : nodes[i].transform)
            if (!std::isfinite(value))
                throw std::runtime_error("Transformation non finie");
        for (float value : nodes[i].translation)
            if (!std::isfinite(value))
                throw std::runtime_error("Translation non finie");
        for (float value : nodes[i].rotation)
            if (!std::isfinite(value))
                throw std::runtime_error("Rotation non finie");
        for (float value : nodes[i].scale)
            if (!std::isfinite(value))
                throw std::runtime_error("Echelle non finie");
    }
    for (uint32_t i = 0; i < scene.animation_count; ++i) {
        const auto& source = scene.animations[i];
        if (!std::isfinite(source.duration) || source.duration < 0 || (source.track_count && !source.tracks))
            throw std::runtime_error("Animation invalide");
        Clip clip{source.name ? source.name : "Animation", source.duration, {}};
        auto copy = [](const Cy3DKey* keys, uint32_t count, std::vector<Cy3DKey>& target) {
            if (count && !keys)
                throw std::runtime_error("Cles absentes");
            if (count)
                target.assign(keys, keys + count);
            double previous = -1e300;
            for (const auto& key : target) {
                if (!std::isfinite(key.time) || key.time < previous)
                    throw std::runtime_error("Temps de cle invalide");
                previous = key.time;
                for (float v : key.value)
                    if (!std::isfinite(v))
                        throw std::runtime_error("Cle non finie");
            }
        };
        for (uint32_t j = 0; j < source.track_count; ++j) {
            const auto& s = source.tracks[j];
            if (s.node >= nodes.size())
                throw std::runtime_error("Noeud anime invalide");
            Track track{s.node, {}, {}, {}};
            copy(s.positions, s.position_count, track.positions);
            copy(s.rotations, s.rotation_count, track.rotations);
            copy(s.scales, s.scale_count, track.scales);
            clip.tracks.push_back(std::move(track));
        }
        clips.push_back(std::move(clip));
    }
}
std::vector<Mat4> AnimationData::evaluate(int clip, double time) const {
    std::vector<Mat4> pose(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
        std::copy_n(nodes[i].transform, 16, pose[i].v);
    if (clip >= 0 && static_cast<size_t>(clip) < clips.size())
        for (const auto& track : clips[clip].tracks) {
            const auto& n = nodes[track.node];
            pose[track.node] = trs(sample(track.positions, time, n.translation, false),
                                   sample(track.rotations, time, n.rotation, true),
                                   sample(track.scales, time, n.scale, false));
        }
    for (size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].parent >= 0)
            pose[i] = pose[nodes[i].parent] * pose[i];
    return pose;
}
std::array<float, 9> normalMatrix(const Mat4& m) {
    Vec3 a{m.v[0], m.v[1], m.v[2]}, b{m.v[4], m.v[5], m.v[6]}, c{m.v[8], m.v[9], m.v[10]};
    auto x = cross(b, c), y = cross(c, a), z = cross(a, b);
    float d = dot(a, x);
    if (std::abs(d) < 1e-20f)
        return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    x = x * (1 / d);
    y = y * (1 / d);
    z = z * (1 / d);
    return {x.x, x.y, x.z, y.x, y.y, y.z, z.x, z.y, z.z};
}
} // namespace cy

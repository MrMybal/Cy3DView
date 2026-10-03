// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include <algorithm>
#include <cmath>
namespace cy {
struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 b) const { return {x + b.x, y + b.y, z + b.z}; }
    Vec3 operator-(Vec3 b) const { return {x - b.x, y - b.y, z - b.z}; }
    Vec3 operator*(float f) const { return {x * f, y * f, z * f}; }
};
inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 a) {
    return std::sqrt(dot(a, a));
}
inline Vec3 normalized(Vec3 a) {
    float n = length(a);
    return n > 1e-20f ? a * (1.f / n) : Vec3{0, 1, 0};
}
struct Mat4 {
    float v[16]{};
    static Mat4 identity() {
        Mat4 m;
        for (int i = 0; i < 4; ++i)
            m.v[i * 5] = 1;
        return m;
    }
};
inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 m;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            for (int k = 0; k < 4; ++k)
                m.v[c * 4 + r] += a.v[k * 4 + r] * b.v[c * 4 + k];
    return m;
}
inline Mat4 perspective(float aspect, float nearPlane, float farPlane) {
    Mat4 m;
    float f = 1.f / std::tan(.39269908f);
    m.v[0] = f / aspect;
    m.v[5] = f;
    m.v[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    m.v[11] = -1;
    m.v[14] = 2 * farPlane * nearPlane / (nearPlane - farPlane);
    return m;
}
inline Mat4 lookAt(Vec3 eye, Vec3 target) {
    Vec3 f = normalized(target - eye), s = normalized(cross(f, {0, 1, 0})), u = cross(s, f);
    Mat4 m = Mat4::identity();
    m.v[0] = s.x;
    m.v[4] = s.y;
    m.v[8] = s.z;
    m.v[1] = u.x;
    m.v[5] = u.y;
    m.v[9] = u.z;
    m.v[2] = -f.x;
    m.v[6] = -f.y;
    m.v[10] = -f.z;
    m.v[12] = -dot(s, eye);
    m.v[13] = -dot(u, eye);
    m.v[14] = dot(f, eye);
    return m;
}
struct Camera {
    Vec3 target{};
    float yaw = .7f, pitch = .35f, distance = 5, radius = 1, ground = -1;
    bool navigated = false;
    Vec3 eye() const {
        return target +
               Vec3{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)} *
                   distance;
    }
    void fit(const float* minimum, const float* maximum, float aspect = 1.5f) {
        navigated = false;
        Vec3 a{minimum[0], minimum[1], minimum[2]}, b{maximum[0], maximum[1], maximum[2]};
        target = (a + b) * .5f;
        radius = std::max(length(b - a) * .5f, 1e-4f);
        float halfFov = std::atan(std::tan(.39269908f) * std::min(aspect, 1.f));
        distance = radius / std::sin(halfFov) * 1.15f;
        ground = a.y;
    }
    void zoom(float amount) {
        distance = std::clamp(distance * std::exp(-amount * .14f), radius * .02f, radius * 1000);
    }
    void pan(float x, float y, float height) {
        Vec3 right = normalized(cross(target - eye(), {0, 1, 0})),
             up = cross(right, normalized(target - eye()));
        target = target + right * (-x * distance / height) + up * (y * distance / height);
    }
    float nearPlane() const {
        return navigated ? radius * .001f : std::max(radius * .001f, distance - radius * 2);
    }
    void move(float forward, float sideways, float seconds, float boost = 1) {
        Vec3 direction =
            normalized(target - eye()) * forward + Vec3{std::cos(yaw), 0, -std::sin(yaw)} * sideways;
        if (dot(direction, direction) == 0)
            return;
        const float speed = std::max(radius * .02f, distance * .15f) * boost;
        target = target + normalized(direction) * (speed * seconds);
        // A close clipping plane is needed when the camera enters the model.
        navigated = true;
    }
};
} // namespace cy

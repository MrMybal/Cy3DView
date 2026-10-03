// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "renderer.h"
namespace cy {
void Renderer::tick(double dt) {
    if (!playing || !active_ || !active_->animation || active_->animation->clips.empty())
        return;
    animation_clip = std::clamp(animation_clip, 0, static_cast<int>(active_->animation->clips.size()) - 1);
    double duration = active_->animation->clips[animation_clip].duration;
    animation_time += std::max(0., dt) * animation_speed;
    if (duration > 0 && animation_time >= duration) {
        if (animation_loop)
            animation_time = std::fmod(animation_time, duration);
        else {
            animation_time = duration;
            playing = false;
        }
    }
}
void Renderer::updatePose() {
    if (!active_ || !active_->animation)
        return;
    auto& scene = *active_;
    if (scene.pose_time == animation_time && scene.pose_clip == animation_clip)
        return;
    scene.pose = scene.animation->evaluate(animation_clip, animation_time);
    scene.pose_time = animation_time;
    scene.pose_clip = animation_clip;
    for (auto& instance : scene.instances)
        if (instance.data.node >= 0) {
            const auto& m = scene.pose[instance.data.node];
            std::copy_n(m.v, 16, instance.data.transform);
            instance.normals = normalMatrix(m);
            Vec3 a{m.v[0], m.v[1], m.v[2]}, b{m.v[4], m.v[5], m.v[6]}, c{m.v[8], m.v[9], m.v[10]};
            instance.winding = dot(a, cross(b, c)) < 0 ? GL_CW : GL_CCW;
            auto center = scene.meshes[instance.data.mesh].center;
            instance.center = a * center.x + b * center.y + c * center.z + Vec3{m.v[12], m.v[13], m.v[14]};
        }
    for (auto& mesh : scene.meshes)
        if (!mesh.bones.empty()) {
            std::vector<Mat4> palette;
            palette.reserve(mesh.bones.size());
            for (const auto& bone : mesh.bones) {
                Mat4 offset;
                std::copy_n(bone.inverse_bind, 16, offset.v);
                palette.push_back(scene.pose[bone.node] * offset);
            }
            glBindBuffer(GL_TEXTURE_BUFFER, mesh.palette_buffer);
            glBufferSubData(GL_TEXTURE_BUFFER, 0, static_cast<GLsizeiptr>(palette.size() * sizeof(Mat4)),
                            palette.data());
        }
}
} // namespace cy

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "loader.h"
#include "cy_math.h"
#include <glad/gl.h>
#include <array>
#include <list>
namespace cy {
class Renderer {
    struct Mesh {
        GLuint vao = 0, vbo = 0, ebo = 0;
        GLsizei count = 0;
        uint32_t material = 0;
        uint32_t topology = 0;
        bool colors = false;
        std::vector<Cy3DBone> bones;
        GLuint palette_buffer = 0, palette_texture = 0;
        Vec3 center;
        std::vector<Vec3> splat_positions;
        std::vector<uint32_t> order, scratch;
        Vec3 last_eye{1e30f, 0, 0}, last_target{};
        std::array<float, 16> last_transform{};
    };
    struct Instance {
        Cy3DInstance data{};
        std::array<float, 9> normals{};
        GLenum winding = GL_CCW;
        Vec3 center;
    };
    struct GpuScene {
        std::weak_ptr<Scene> source;
        std::vector<Mesh> meshes;
        std::vector<GLuint> textures;
        std::vector<Instance> instances;
        std::vector<uint32_t> opaque, transparent;
        std::vector<Cy3DMaterial> materials;
        std::shared_ptr<AnimationData> animation;
        std::vector<Mat4> pose;
        double pose_time = -1e300;
        int pose_clip = -999;
        std::array<float, 3> minimum{}, maximum{};
        uint64_t bytes = 0;
        uint64_t metadata_bytes = 0;
        uint64_t cacheBytes() const { return bytes + metadata_bytes; }
        ~GpuScene();
    };
    struct Upload {
        std::shared_ptr<Scene> source;
        std::shared_ptr<GpuScene> target;
        uint32_t mesh = 0, texture = 0, material = 0, instance = 0;
        uint64_t offset = 0, transferred = 0, total = 0;
        int texture_row = 0;
        GLint max_texture_size = 0;
        enum class Phase { Meshes, Textures, Materials, Instances, Done } phase = Phase::Meshes;
    };
    struct Uniforms {
        GLint model, vp, normal_matrix, eye, unlit, base, mode;
        GLint metallic, roughness, emission, normal_scale, ao_strength, alpha_mode, alpha_cutoff;
        GLint exposure, environment, rotation, use_normals, use_ao, material_unlit;
        GLint skinning, palette, has_color, topology, point_size;
        std::array<GLint, CY3D_MAP_COUNT> maps, enabled, uv_set, channel, transform;
    } uniforms_{};
    GLuint program_ = 0, grid_ = 0, gridVbo_ = 0;
    GLuint skeleton_vao_ = 0, skeleton_vbo_ = 0, splat_program_ = 0;
    GLint splat_vp_, splat_model_, splat_screen_, splat_exposure_;
    void createSplats();
    void drawSplats(Mesh& mesh, const Instance& instance, const Mat4& vp, int width, int height);
    void updatePose();
    GLuint radiance_ = 0, irradiance_ = 0, brdf_ = 0;
    std::array<GLuint, 9> samplers_{};
    void createEnvironment();
    std::shared_ptr<GpuScene> active_;
    std::unique_ptr<Upload> pending_;
    std::list<std::shared_ptr<GpuScene>> cache_;
    uint64_t cache_limit_ = 256ull * 1024 * 1024, cache_bytes_ = 0;
    float grid_unit_ = 0, grid_ground_ = 0;
    GLuint framebuffer_ = 0, color_ = 0, depth_ = 0;
    int width_ = 0, height_ = 0;
    void remember(const std::shared_ptr<GpuScene>& scene);
    void activate(std::shared_ptr<GpuScene> scene);
    uint64_t uploadStep();

  public:
    Camera camera;
    bool wire = false, grid = true, textured = true;
    bool normal_maps = true, occlusion = true;
    float exposure = 0, environment = 1, light_rotation = 0;
    int mode = 0; // PBR materials, clay, normals
    int animation_clip = 0;
    double animation_time = 0;
    float animation_speed = 1, point_size = 3;
    bool playing = true, animation_loop = true, skeleton = false;
    void tick(double dt);
    const AnimationData* animations() const { return active_ ? active_->animation.get() : nullptr; }
    uint64_t gpu_bytes = 0;
    bool last_upload_cached = false;
    uint64_t cache_hits = 0;
    Renderer();
    ~Renderer();
    void upload(std::shared_ptr<Scene> scene);
    bool beginUpload(std::shared_ptr<Scene> scene); // true = reused GPU buffers immediately
    bool advanceUpload(double milliseconds = 4, uint64_t byte_budget = 4ull * 1024 * 1024);
    void cancelUpload();
    bool uploading() const { return pending_ != nullptr; }
    float uploadProgress() const;
    uint64_t gpuCacheBytes() const { return cache_bytes_; }
    void setGpuCacheLimit(uint64_t bytes);
    std::shared_ptr<Scene> currentScene() const { return active_ ? active_->source.lock() : nullptr; }
    GLuint draw(int width, int height);
    bool save(const fs::path& file);
    static bool saveWindow(const fs::path& file, int width, int height);
};
} // namespace cy

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "renderer.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <iostream>
#include <stdexcept>
using namespace cy;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
std::vector<uint8_t> pixels(Renderer& renderer) {
    auto texture = renderer.draw(512, 512);
    std::vector<uint8_t> result(512 * 512 * 4);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    require(glGetError() == GL_NO_ERROR, "OpenGL error");
    return result;
}
double difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i)
        if (i % 4 != 3)
            sum += std::abs(int(a[i]) - int(b[i]));
    return sum / (512 * 512 * 3);
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 4 || !glfwInit())
        return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    auto window = glfwCreateWindow(512, 512, "Animation and clouds", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)))
        return 1;
    int status = 0;
    try {
        Plugins plugins(fs::absolute(path(argv[1])));
        auto fixtures = fs::absolute(path(argv[2])), output = fs::absolute(path(argv[3]));
        std::atomic<bool> cancel = false;
        std::atomic<float> progress = 0;
        auto load = [&](const char* file) {
            auto scene = importScene(plugins, fixtures / path(file), cancel, progress);
            std::cout << file << ": " << scene->import_ms << " ms\n";
            return scene;
        };
        auto animated = load("skinned.glb");
        require(animated->animation && animated->animation->clips.size() == 3, "Animation clips lost");
        require(animated->data->meshes[0].bone_count == 2 && animated->data->meshes[0].skin,
                "Skeleton or weights lost");
        auto clipIndex = [&](const char* name) {
            for (size_t i = 0; i < animated->animation->clips.size(); ++i)
                if (animated->animation->clips[i].name == name)
                    return static_cast<int>(i);
            throw std::runtime_error("Clip name lost");
        };
        int bend = clipIndex("Bend"), move = clipIndex("Move"), step = clipIndex("Step");
        auto pose = animated->animation->evaluate(bend, 1);
        auto tip = animated->data->meshes[0].bones[1].node;
        require(std::abs(pose[tip].v[0]) < .001f && std::abs(pose[tip].v[1] - 1) < .001f &&
                    std::abs(pose[tip].v[13] - 1) < .001f,
                "Quaternion interpolation or hierarchy wrong");
        auto moving = animated->animation->evaluate(move, .5);
        require(std::abs(moving[animated->data->instances[0].node].v[12] - .5) < .001,
                "Animated root transform wrong");
        auto held = animated->animation->evaluate(step, .5), jump = animated->animation->evaluate(step, 1);
        require(std::abs(held[0].v[12]) < .001f && std::abs(jump[0].v[12] - 2) < .001f,
                "STEP interpolation lost");
        Renderer renderer;
        renderer.grid = false;
        renderer.playing = false;
        renderer.upload(animated);
        renderer.camera.yaw = 0;
        renderer.camera.pitch = 0;
        renderer.animation_clip = bend;
        renderer.animation_time = 0;
        renderer.camera.distance *= 1.8f;
        auto bind = pixels(renderer);
        require(renderer.save(output / "animation-bind.png"), "Cannot save bind pose");
        renderer.animation_time = 1;
        auto bent = pixels(renderer);
        require(difference(bind, bent) > 2, "GPU skinning did not deform the mesh");
        require(renderer.save(output / "animation-bent.png"), "Cannot save animated pose");
        renderer.skeleton = true;
        require(difference(bent, pixels(renderer)) > .01, "Skeleton overlay missing");
        renderer.skeleton = false;
        renderer.animation_clip = move;
        renderer.animation_time = 0;
        auto root = pixels(renderer);
        renderer.animation_time = 1;
        require(difference(root, pixels(renderer)) > 2, "Animated instance did not move");
        renderer.playing = true;
        renderer.animation_loop = false;
        renderer.animation_time = .8;
        renderer.tick(.5);
        require(!renderer.playing && std::abs(renderer.animation_time - 1) < .001, "Playback endpoint wrong");
        renderer.playing = true;
        renderer.animation_loop = true;
        renderer.animation_time = .8;
        renderer.tick(.5);
        require(std::abs(renderer.animation_time - .3) < .001, "Playback loop wrong");
        renderer.playing = false;
        for (auto file : {"cloud_ascii.ply", "cloud_little.ply", "cloud_big.ply", "cloud.xyz", "cloud.pts",
                          "cloud.splat", "gaussian.ply"}) {
            auto cloud = load(file);
            require(cloud->points == 1008 && cloud->triangles == 0, "Point geometry lost");
            require(cloud->data->meshes[0].colors, "Point colors lost");
            bool splat = std::string(file) == "cloud.splat" || std::string(file) == "gaussian.ply";
            require((cloud->splats == 1008) == splat, "Splat topology wrong");
            renderer.upload(cloud);
            renderer.camera.yaw = 0;
            renderer.camera.pitch = 0;
            auto visible = pixels(renderer);
            auto& m = const_cast<Cy3DMesh&>(cloud->data->meshes[0]);
            uint32_t count = m.index_count;
            m.index_count = 0;
            auto empty = importScene(plugins, fixtures / "cube.obj", cancel, progress);
            const_cast<Cy3DMesh&>(empty->data->meshes[0]).index_count = 0;
            renderer.upload(empty);
            renderer.camera.fit(cloud->data->bounds_min, cloud->data->bounds_max, 1);
            renderer.camera.yaw = 0;
            renderer.camera.pitch = 0;
            require(difference(visible, pixels(renderer)) > .2, "Cloud did not render");
            m.index_count = count;
            renderer.upload(cloud);
            pixels(renderer);
            require(renderer.save(output / (std::string(file) + ".png")), "Cannot save cloud render");
            if (!splat) {
                renderer.point_size = 9;
                require(difference(visible, pixels(renderer)) > .2, "Point size did not change rendering");
                renderer.point_size = 3;
            }
        }
        auto floatColor = load("float_color.ply");
        auto& floatMesh = floatColor->data->meshes[0];
        require(std::abs(floatMesh.colors[0] - .214041f) < .001f &&
                    std::abs(floatMesh.colors[3] - 128 / 255.f) < .001f &&
                    floatMesh.vertices[0].position[0] == 1,
                "PLY floating colors, byte alpha or reordered properties wrong");
        auto sortedA = load("sorted_a.splat"), sortedB = load("sorted_b.splat");
        renderer.upload(sortedA);
        renderer.camera.yaw = 0;
        renderer.camera.pitch = 0;
        auto sorted = pixels(renderer);
        auto center = (256 * 512 + 256) * 4;
        require(sorted[center] > sorted[center + 2], "Nearest splat did not dominate compositing");
        renderer.upload(sortedB);
        renderer.camera.yaw = 0;
        renderer.camera.pitch = 0;
        require(difference(sorted, pixels(renderer)) < .01, "Splat compositing depends on input order");
        renderer.camera.yaw = 3.14159265f;
        auto reversed = pixels(renderer);
        require(reversed[center + 2] > reversed[center], "Splat sort did not follow the camera");
        bool rejected = false;
        try {
            load("bad_cloud.ply");
        } catch (...) {
            rejected = true;
        }
        require(rejected, "Truncated point cloud accepted");
        cancel = true;
        rejected = false;
        try {
            load("cloud_little.ply");
        } catch (...) {
            rejected = true;
        }
        require(rejected, "Point loader ignored cancellation");
        cancel = false;
        for (auto file : {"quad.usd", "quad.usda", "quad.usdz", "recent.usdc", "unicode/épreuve.usda"}) {
            auto scene = load(file);
            require(scene->triangles >= 2, "USD mesh missing");
            require(!scene->reusable, "USD external dependency cache must not be reused");
        }
        auto usd = load("quad.usda");
        require(usd->data->bounds_min[0] > .9f && usd->data->bounds_max[0] > 2.9f, "USD node transform lost");
        std::cout << "Animation, GPU skinning, skeleton, 7 cloud encodings and 4 USD variants passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        status = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}

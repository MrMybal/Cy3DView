// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "renderer.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <bit>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cy;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::vector<uint8_t> pixels(Renderer& renderer) {
    auto texture = renderer.draw(640, 480);
    std::vector<uint8_t> result(640 * 480 * 4);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, result.data());
    return result;
}
void u32(std::ofstream& output, uint32_t value) {
    char bytes[4] = {static_cast<char>(value), static_cast<char>(value >> 8), static_cast<char>(value >> 16),
                     static_cast<char>(value >> 24)};
    output.write(bytes, 4);
}
void makeSTL(const fs::path& file) {
    std::ofstream output(file, std::ios::binary);
    char header[80]{};
    output.write(header, 80);
    u32(output, 200000);
    for (uint32_t i = 0; i < 200000; ++i) {
        float x = static_cast<float>(i % 500), y = static_cast<float>(i / 500);
        for (float number : {0.f, 0.f, 1.f, x, y, 0.f, x + 1, y, 0.f, x, y + 1, 0.f})
            u32(output, std::bit_cast<uint32_t>(number));
        output.put(0);
        output.put(0);
    }
    require(output.good(), "Cannot generate STL stress fixture");
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "gpu_test plugins fixtures output_directory\n";
        return 1;
    }
    glfwSetErrorCallback([](int, const char* error) { std::cerr << error << '\n'; });
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    auto window = glfwCreateWindow(640, 480, "GPU contract", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    int status = 0;
    try {
        auto output = fs::absolute(path(argv[3]));
        fs::create_directories(output);
        auto stressFile = output / "gpu-stress.stl";
        makeSTL(stressFile);
        Plugins plugins(fs::absolute(path(argv[1])));
        std::atomic<bool> cancel{false};
        std::atomic<float> progress{0};
        auto cube = importScene(plugins, fs::absolute(path(argv[2])) / "cube.obj", cancel, progress);
        auto stress = importScene(plugins, stressFile, cancel, progress);
        Renderer renderer;
        renderer.upload(cube);
        auto original = pixels(renderer);
        require(renderer.currentScene() == cube, "Initial scene missing");
        require(!renderer.beginUpload(stress), "Unexpected first GPU cache hit");
        require(!renderer.advanceUpload(4, 1024 * 1024),
                "Large upload completed in a single byte-limited slice");
        require(renderer.currentScene() == cube && pixels(renderer) == original,
                "Old scene changed during upload");
        renderer.cancelUpload();
        require(!renderer.uploading(), "Cancellation did not release staged buffers");
        require(renderer.currentScene() == cube && pixels(renderer) == original,
                "Cancellation changed the displayed scene");
        renderer.beginUpload(stress);
        unsigned slices = 0;
        double maxSlice = 0;
        auto start = std::chrono::steady_clock::now();
        while (renderer.uploading()) {
            auto sliceStart = std::chrono::steady_clock::now();
            bool done = renderer.advanceUpload(4, 1024 * 1024);
            maxSlice = std::max(maxSlice, std::chrono::duration<double, std::milli>(
                                              std::chrono::steady_clock::now() - sliceStart)
                                              .count());
            ++slices;
            if (!done) {
                require(renderer.currentScene() == cube, "Scene switched before upload completed");
                require(pixels(renderer) == original, "Old geometry corrupted by staged upload");
            }
            glfwPollEvents();
            require(slices < 1000, "Upload did not complete");
        }
        require(renderer.currentScene() == stress && slices > 10, "Stress scene was not staged");
        require(pixels(renderer) != original, "New geometry did not render");
        auto total =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        auto cacheStart = std::chrono::steady_clock::now();
        require(renderer.beginUpload(cube), "GPU cache missed on return");
        auto cacheMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cacheStart).count();
        require(!renderer.uploading() && renderer.last_upload_cached && pixels(renderer) == original,
                "GPU cache did not restore exact rendering");
        require(renderer.gpuCacheBytes() <= 256ull * 1024 * 1024, "GPU cache exceeded its budget");
        renderer.setGpuCacheLimit(1);
        require(renderer.gpuCacheBytes() == 0, "GPU cache eviction failed");
        require(!renderer.beginUpload(stress), "Evicted GPU asset was reused");
        renderer.cancelUpload();
        // A new scene identity is created after dependency invalidation, even for the same model path.
        auto updated = importScene(plugins, fs::absolute(path(argv[2])) / "cube.obj", cancel, progress);
        renderer.setGpuCacheLimit(256ull * 1024 * 1024);
        require(!renderer.beginUpload(updated), "Stale scene reused a different import's buffers");
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBufferData(GL_ARRAY_BUFFER, 4, nullptr, GL_STATIC_DRAW); // simulate a driver-side failure
        bool failed = false;
        try {
            renderer.advanceUpload();
        } catch (...) {
            failed = true;
        }
        require(failed && !renderer.uploading() && renderer.currentScene() == cube,
                "Failed upload replaced the old scene");
        require(pixels(renderer) == original, "Failed upload corrupted the old scene");
        // A large texture must also span several slices, not just the geometry.
        auto& image = updated->images.front();
        image.width = 2048;
        image.height = 2048;
        image.rgba.assign(2048 * 2048 * 4, 160);
        for (size_t i = 3; i < image.rgba.size(); i += 4)
            image.rgba[i] = 255;
        updated->gpu_bytes += image.rgba.size();
        renderer.beginUpload(updated);
        unsigned textureSlices = 0;
        while (renderer.uploading()) {
            renderer.advanceUpload(4, 1024 * 1024);
            ++textureSlices;
            require(textureSlices < 1000, "Texture upload did not complete");
        }
        require(textureSlices > 10 && renderer.currentScene() == updated,
                "Large texture was not uploaded progressively");
        pixels(renderer);
        require(glGetError() == GL_NO_ERROR, "Unexpected OpenGL error");
        require(renderer.save(output / "gpu-staged.png"), "Cannot save GPU test image");
        auto finalPixels = pixels(renderer);
        updated.reset();
        require(!renderer.currentScene() && pixels(renderer) == finalPixels,
                "GPU rendering retained or depended on freed CPU geometry");
        std::ofstream report(output / "gpu-metrics.json");
        report << "{\"triangles\":200000,\"slices\":" << slices << ",\"max_slice_ms\":" << maxSlice
               << ",\"test_total_ms\":" << total << ",\"cache_return_ms\":" << cacheMs
               << ",\"texture_slices\":" << textureSlices << "}\n";
        std::cout << "PASS staged geometry/textures, cancellation, rollback, GPU cache, eviction; " << slices
                  << " slices, max " << maxSlice << " ms, cache return " << cacheMs << " ms\n";
        fs::remove(stressFile);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}

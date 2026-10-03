// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "renderer.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <chrono>
#include <iostream>
#include <fstream>
#include <stdexcept>
using namespace cy;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::vector<uint8_t> pixels(Renderer& renderer) {
    auto texture = renderer.draw(512, 512);
    std::vector<uint8_t> output(512 * 512 * 4);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, output.data());
    require(glGetError() == GL_NO_ERROR, "OpenGL error during PBR rendering");
    return output;
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
    auto window = glfwCreateWindow(512, 512, "PBR contract", nullptr, nullptr);
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
        std::atomic<bool> cancel{false};
        std::atomic<float> progress{0};
        auto mapped = importScene(plugins, fixtures / "pbr_mapped.gltf", cancel, progress);
        const auto& material = mapped->data->materials[mapped->data->meshes[0].material];
        require(std::abs(material.metallic - .6f) < .001f && std::abs(material.roughness - .8f) < .001f,
                "PBR factors were not imported");
        require(material.maps[CY3D_METALLIC].texture == material.maps[CY3D_ROUGHNESS].texture &&
                    material.maps[CY3D_METALLIC].channel == 2 && material.maps[CY3D_ROUGHNESS].channel == 1,
                "Packed glTF metallic/roughness channels are wrong");
        require(material.maps[CY3D_OCCLUSION].texture == material.maps[CY3D_METALLIC].texture &&
                    material.maps[CY3D_OCCLUSION].channel == 0 && material.maps[CY3D_OCCLUSION].uv_set == 1 &&
                    std::abs(material.occlusion_strength - .3f) < .001f,
                "glTF occlusion properties lost");
        require(material.maps[CY3D_BASE_COLOR].uv_set == 1 &&
                    material.maps[CY3D_BASE_COLOR].wrap_u == CY3D_CLAMP &&
                    material.maps[CY3D_BASE_COLOR].wrap_v == CY3D_MIRROR,
                "UV set / wrapping lost");
        // Evaluate a non-uniform scale, rotation and offset in original glTF UV space.
        const auto& t = material.maps[CY3D_BASE_COLOR].transform;
        float u = .23f, v = .71f;
        float actualU = t[0] * u + t[3] * (1 - v) + t[6];
        float actualV = 1 - (t[1] * u + t[4] * (1 - v) + t[7]);
        require(std::abs(actualU - (.15f + std::cos(.3f) * .8f * u - std::sin(.3f) * .6f * v)) < .0001f &&
                    std::abs(actualV - (.25f + std::sin(.3f) * .8f * u + std::cos(.3f) * .6f * v)) < .0001f,
                "KHR_texture_transform conversion is wrong");
        require(std::abs(material.normal_scale - .4f) < .001f &&
                    std::abs(material.emissive[2] - .6f) < .001f && material.alpha_mode == CY3D_MASK &&
                    std::abs(material.alpha_cutoff - .4f) < .001f,
                "Normal, emission or alpha properties lost");
        require(mapped->images.size() == 4, "PBR images not deduplicated");
        for (const auto& image : mapped->images)
            require(!image.rgba.empty(), "PBR texture decode failed");
        auto start = std::chrono::steady_clock::now();
        Renderer renderer;
        auto startup =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        renderer.grid = false;
        renderer.upload(mapped);
        renderer.camera.yaw = 0;
        renderer.camera.pitch = 0;
        auto reference = pixels(renderer);
        renderer.normal_maps = false;
        require(difference(reference, pixels(renderer)) > .15, "Normal map did not affect rendering");
        renderer.normal_maps = true;
        renderer.occlusion = false;
        require(difference(reference, pixels(renderer)) > .1, "Occlusion did not affect ambient lighting");
        renderer.occlusion = true;
        renderer.textured = false;
        require(difference(reference, pixels(renderer)) > 1, "PBR maps did not affect rendering");
        renderer.textured = true;
        renderer.exposure = 1;
        require(difference(reference, pixels(renderer)) > 1, "Exposure did not affect rendering");
        renderer.exposure = 0;
        renderer.light_rotation = 1;
        require(difference(reference, pixels(renderer)) > .5, "Studio rotation did not affect rendering");
        renderer.light_rotation = 0;
        pixels(renderer);
        require(renderer.save(output / "pbr-mapped.png"), "Cannot save mapped PBR render");
        // Re-import for each factor change so no stale GPU cache can hide the result.
        auto variant = [&](float metal, float rough, float emission) {
            auto scene = importScene(plugins, fixtures / "pbr_mapped.gltf", cancel, progress);
            auto& m = const_cast<Cy3DMaterial&>(scene->data->materials[scene->data->meshes[0].material]);
            for (auto& map : m.maps)
                map.texture = -1;
            m.alpha_mode = CY3D_OPAQUE;
            m.metallic = metal;
            m.roughness = rough;
            m.emissive[0] = emission;
            m.emissive[1] = m.emissive[2] = 0;
            renderer.upload(scene);
            renderer.camera.yaw = 0;
            renderer.camera.pitch = 0;
            return pixels(renderer);
        };
        auto dielectric = variant(0, .25f, 0), metal = variant(1, .25f, 0), matte = variant(1, .9f, 0),
             emissive = variant(0, .25f, 1);
        require(difference(dielectric, metal) > 3, "Metalness did not change the BRDF");
        require(difference(metal, matte) > 1, "Roughness did not change the reflections");
        require(difference(dielectric, emissive) > 2, "Emission did not affect rendering");
        // sRGB texture sampling should linearize exactly once; alpha must stay linear.
        auto colorScene = importScene(plugins, fixtures / "pbr_mapped.gltf", cancel, progress);
        auto& colorMaterial =
            const_cast<Cy3DMaterial&>(colorScene->data->materials[colorScene->data->meshes[0].material]);
        auto baseMap = colorMaterial.maps[CY3D_BASE_COLOR];
        for (auto& map : colorMaterial.maps)
            map.texture = -1;
        colorMaterial.maps[CY3D_BASE_COLOR] = baseMap;
        colorMaterial.unlit = 1;
        colorMaterial.alpha_mode = CY3D_OPAQUE;
        std::fill_n(colorMaterial.color, 4, 1.f);
        auto& image = colorScene->images[baseMap.texture];
        for (size_t i = 0; i < image.rgba.size(); i += 4) {
            image.rgba[i] = image.rgba[i + 1] = image.rgba[i + 2] = 128;
            image.rgba[i + 3] = 255;
        }
        renderer.upload(colorScene);
        renderer.camera.yaw = 0;
        renderer.camera.pitch = 0;
        auto srgb = pixels(renderer);
        auto center = (256 * 512 + 256) * 4;
        float linear = std::pow((128 / 255.f + .055f) / 1.055f, 2.4f);
        float tone = (linear * (2.51f * linear + .03f)) / (linear * (2.43f * linear + .59f) + .14f);
        int expected = static_cast<int>((1.055f * std::pow(tone, 1 / 2.4f) - .055f) * 255 + .5f);
        require(std::abs(int(srgb[center]) - expected) <= 2 && srgb[center] == srgb[center + 1],
                "Color space conversion is wrong");
        // The same image may be color and linear data in different material slots.
        auto mixed = importScene(plugins, fixtures / "pbr_mapped.gltf", cancel, progress);
        auto& mixedMaterial =
            const_cast<Cy3DMaterial&>(mixed->data->materials[mixed->data->meshes[0].material]);
        auto mixedIndex = mixedMaterial.maps[CY3D_METALLIC].texture;
        mixedMaterial.maps[CY3D_BASE_COLOR] = mixedMaterial.maps[CY3D_METALLIC];
        mixed->texture_usage[mixedIndex] = 3;
        mixed->gpu_bytes += mixed->images[mixedIndex].rgba.size();
        renderer.upload(mixed);
        pixels(renderer);
        // Alpha mask must discard; blend must preserve the opaque viewport's alpha.
        auto alphaScene = importScene(plugins, fixtures / "pbr_mapped.gltf", cancel, progress);
        auto& alphaMaterial =
            const_cast<Cy3DMaterial&>(alphaScene->data->materials[alphaScene->data->meshes[0].material]);
        for (auto& map : alphaMaterial.maps)
            map.texture = -1;
        alphaMaterial.alpha_mode = CY3D_MASK;
        alphaMaterial.color[3] = .2f;
        alphaMaterial.alpha_cutoff = .4f;
        renderer.upload(alphaScene);
        renderer.camera.yaw = 0;
        renderer.camera.pitch = 0;
        auto mask = pixels(renderer);
        require(mask[center] < 30 && mask[center + 1] < 30, "Alpha mask did not discard covered geometry");
        auto blendScene = importScene(plugins, fixtures / "pbr_mapped.gltf", cancel, progress);
        auto& blendMaterial =
            const_cast<Cy3DMaterial&>(blendScene->data->materials[blendScene->data->meshes[0].material]);
        for (auto& map : blendMaterial.maps)
            map.texture = -1;
        blendMaterial.alpha_mode = CY3D_BLEND;
        blendMaterial.color[3] = .5f;
        renderer.upload(blendScene);
        renderer.camera.yaw = 0;
        renderer.camera.pitch = 0;
        auto blend = pixels(renderer);
        require(difference(mask, blend) > 2 && blend[center + 3] == 255,
                "Alpha blend or viewport composition failed");
        auto studio = importScene(plugins, fixtures.parent_path().parent_path() / "samples/pbr_studio.gltf",
                                  cancel, progress);
        renderer.upload(studio);
        renderer.camera.yaw = 0;
        renderer.camera.pitch = .1f;
        for (int i = 0; i < 10; ++i)
            pixels(renderer);
        GLuint query = 0;
        glGenQueries(1, &query);
        glBeginQuery(GL_TIME_ELAPSED, query);
        for (int i = 0; i < 20; ++i)
            renderer.draw(1280, 720);
        glEndQuery(GL_TIME_ELAPSED);
        GLuint64 elapsed = 0;
        glGetQueryObjectui64v(query, GL_QUERY_RESULT, &elapsed);
        glDeleteQueries(1, &query);
        require(renderer.save(output / "pbr-studio.png"), "Cannot save studio render");
        require(glGetError() == GL_NO_ERROR, "PBR left a GL error");
        std::ofstream report(output / "pbr-metrics.json");
        report << "{\"renderer_init_cpu_ms\":" << startup << ",\"studio_triangles\":" << studio->triangles
               << ",\"studio_gpu_ms_at_1280x720\":" << double(elapsed) / 20000000. << "}\n";
        std::cout << "PASS PBR import, UV transforms, maps, factors, color space and rendering; GPU "
                  << double(elapsed) / 20000000. << " ms/frame\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}

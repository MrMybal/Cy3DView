// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "extensions.h"
#include "renderer.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <iostream>
#include <cmath>
using namespace cy;
namespace {
void require(bool v, const char* message) {
    if (!v)
        throw std::runtime_error(message);
}
std::vector<uint8_t> pixels(Renderer& r) {
    auto texture = r.draw(512, 512);
    std::vector<uint8_t> data(512 * 512 * 4);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    require(glGetError() == GL_NO_ERROR, "GPU error.");
    return data;
}
double difference(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    double sum = 0;
    for (size_t i = 0; i < a.size(); ++i)
        if (i % 4 != 3)
            sum += std::abs(int(a[i]) - int(b[i]));
    return sum / (512 * 512 * 3);
}
int clip(const Scene& s, const char* name) {
    for (size_t i = 0; i < s.animation->clips.size(); ++i)
        if (s.animation->clips[i].name.find(name) != std::string::npos)
            return static_cast<int>(i);
    throw std::runtime_error("Animation clip missing.");
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
    auto window = glfwCreateWindow(512, 512, "Export rendering", nullptr, nullptr);
    if (!window)
        return 1;
    glfwMakeContextCurrent(window);
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)))
        return 1;
    int status = 0;
    try {
        auto pluginDir = fs::absolute(path(argv[1])), fixtures = fs::absolute(path(argv[2])),
             output = fs::absolute(path(argv[3])) / "export-render";
        fs::create_directories(output);
        Plugins plugins(pluginDir);
        Extensions extensions(pluginDir);
        std::atomic<bool> cancel{false};
        std::atomic<float> progress{0};
        auto load = [&](const fs::path& p) { return importScene(plugins, p, cancel, progress); };
        auto exportModel = [&](const std::shared_ptr<Scene>& scene, const char* input, const char* id) {
            for (const auto& action : extensions.formats())
                if (action.id == id) {
                    auto filename = output / (std::string(input) + "-" + id + "." + action.extension);
                    OperationControl control;
                    auto bundle = exportScene(action, scene, filename, fixtures / input, control);
                    bundle->commit(true);
                    return load(filename);
                }
            throw std::runtime_error("Exporter missing.");
        };
        Renderer renderer;
        renderer.grid = false;
        renderer.playing = false;
        auto pbr = load(fixtures / "pbr_mapped.gltf");
        renderer.upload(pbr);
        auto camera = renderer.camera;
        auto reference = pixels(renderer);
        auto converted = exportModel(pbr, "pbr_mapped.gltf", "glb2");
        renderer.upload(converted);
        renderer.camera = camera;
        auto error = difference(reference, pixels(renderer));
        std::cout << "PBR GLB round-trip pixel difference: " << error << '\n';
        require(error < 1, "PBR appearance changed after conversion.");
        auto animated = load(fixtures / "skinned.glb");
        renderer.mode = 1;
        for (const char* format : {"glb2", "fbx", "fbxa"}) {
            renderer.upload(animated);
            renderer.animation_clip = clip(*animated, "Bend");
            renderer.animation_time = 1;
            renderer.camera.yaw = 0;
            renderer.camera.pitch = 0;
            camera = renderer.camera;
            reference = pixels(renderer);
            auto result = exportModel(animated, "skinned.glb", format);
            renderer.upload(result);
            renderer.camera = camera;
            renderer.animation_clip = clip(*result, "Bend");
            renderer.animation_time = 1;
            error = difference(reference, pixels(renderer));
            std::cout << format << " skin round-trip pixel difference: " << error << '\n';
            require(error < 1, "Exported skeleton changed the animated shape.");
        }
        auto tool = extensions.tools()[0];
        auto module = std::make_shared<ExtensionModule>(tool.library);
        auto state = std::shared_ptr<void>(module->api->create_tool(tool.id.c_str()),
                                           [module](void* p) { module->api->destroy_tool(p); });
        OperationControl control;
        auto edited = runTool(module, tool, state.get(), animated, control);
        renderer.upload(animated);
        renderer.animation_clip = clip(*animated, "Bend");
        renderer.animation_time = .5;
        camera = renderer.camera;
        reference = pixels(renderer);
        renderer.upload(edited);
        renderer.camera = camera;
        renderer.animation_clip = clip(*edited, "Bend");
        renderer.animation_time = .5;
        error = difference(reference, pixels(renderer));
        std::cout << "xatlas skin pixel difference: " << error << '\n';
        require(error < 1, "UV generation changed skinning.");
        require(renderer.save(output / "xatlas-skinned.png"), "Capture failed.");
        renderer.upload(animated);
        renderer.camera = camera;
        renderer.animation_clip = clip(*animated, "Bend");
        renderer.animation_time = .5;
        require(difference(reference, pixels(renderer)) < 1,
                "Returning to the original scene changed rendering.");
        std::cout << "PBR, exported skinning, UV edit and original-scene restoration passed.\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        status = 1;
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}

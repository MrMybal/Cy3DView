// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "branding.h"
#include "logo_data.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <stb_image.h>
#include <array>
#include <memory>
#include <stdexcept>
namespace cy {
Branding::Branding(GLFWwindow* window) {
    struct Encoded {
        const unsigned char* data;
        int size;
    };
    const Encoded encoded[] = {
        {brand::logo16, sizeof(brand::logo16)},  {brand::logo24, sizeof(brand::logo24)},
        {brand::logo32, sizeof(brand::logo32)},  {brand::logo48, sizeof(brand::logo48)},
        {brand::logo64, sizeof(brand::logo64)},  {brand::logo128, sizeof(brand::logo128)},
        {brand::logo256, sizeof(brand::logo256)}};
    using Pixels = std::unique_ptr<stbi_uc, decltype(&stbi_image_free)>;
    std::array<GLFWimage, 7> icons{};
    std::array<Pixels, 7> pixels{Pixels(nullptr, stbi_image_free), Pixels(nullptr, stbi_image_free),
                                 Pixels(nullptr, stbi_image_free), Pixels(nullptr, stbi_image_free),
                                 Pixels(nullptr, stbi_image_free), Pixels(nullptr, stbi_image_free),
                                 Pixels(nullptr, stbi_image_free)};
    for (size_t i = 0; i < icons.size(); ++i) {
        int channels = 0;
        pixels[i].reset(stbi_load_from_memory(encoded[i].data, encoded[i].size, &icons[i].width,
                                              &icons[i].height, &channels, 4));
        if (!pixels[i])
            throw std::runtime_error("Logo de l'application invalide");
        icons[i].pixels = pixels[i].get();
    }
#ifndef __APPLE__
    glfwSetWindowIcon(window, static_cast<int>(icons.size()), icons.data());
#else
    (void)window; // GLFW does not support changing the Cocoa window icon.
#endif
    GLint previous = 0;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
    const auto& logo = icons.back();
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, logo.width, logo.height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 logo.pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
}
Branding::~Branding() {
    if (texture_)
        glDeleteTextures(1, &texture_);
}
} // namespace cy

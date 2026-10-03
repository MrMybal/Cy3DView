// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include <glad/gl.h>
struct GLFWwindow;
namespace cy {
class Branding {
    GLuint texture_ = 0;

  public:
    explicit Branding(GLFWwindow* window);
    ~Branding();
    Branding(const Branding&) = delete;
    Branding& operator=(const Branding&) = delete;
    GLuint texture() const { return texture_; }
};
} // namespace cy

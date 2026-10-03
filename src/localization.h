// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include <filesystem>
#include <string>
#include <string_view>

namespace cy {
enum class Language { English, French };
class Localization {
  public:
    Language language = Language::English;
    // English is the source language. Unknown text from external plugins is preserved.
    const char* text(const char* english) const;
    const char* label(const char* english) const;
    std::string diagnostic(std::string_view message) const;
    void load(const std::filesystem::path& file);
    bool save(const std::filesystem::path& file) const;
};
} // namespace cy

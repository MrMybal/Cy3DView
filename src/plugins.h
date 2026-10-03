// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "cy3d_plugin.h"
#include "paths.h"
#include <memory>
#include <mutex>
#include <vector>
namespace cy {
struct Module {
    void* handle = nullptr;
    const Cy3DPlugin* api = nullptr;
    explicit Module(const fs::path& file);
    ~Module();
    Module(const Module&) = delete;
};
struct PluginEntry {
    std::string name, extensions, error;
    fs::path library;
    int priority = 0;
    std::shared_ptr<Module> module;
};
class Plugins {
    std::mutex mutex_;
    std::vector<PluginEntry> entries_;

  public:
    explicit Plugins(const fs::path& directory);
    std::shared_ptr<Module> forFile(const fs::path& file, const std::vector<Module*>& skipped = {});
    bool supports(const fs::path& file) const;
    const std::vector<PluginEntry>& entries() const { return entries_; }
};
} // namespace cy

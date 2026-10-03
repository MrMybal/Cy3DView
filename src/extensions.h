// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "cy3d_extension.h"
#include "loader.h"
#include <future>
namespace cy {
struct ExtensionModule {
    void* handle = nullptr;
    const Cy3DExtension* api = nullptr;
    explicit ExtensionModule(const fs::path&);
    ~ExtensionModule();
    ExtensionModule(const ExtensionModule&) = delete;
};
struct ExtensionAction {
    fs::path library;
    std::string id, name, name_fr, extension;
    uint32_t capabilities = 0;
};
class Extensions {
    std::vector<ExtensionAction> formats_, tools_;

  public:
    explicit Extensions(const fs::path&);
    const auto& formats() const { return formats_; }
    const auto& tools() const { return tools_; }
};
// A read-only scene view with decoded, embedded RGBA images. Source paths never reach exporters.
struct ExtensionInput {
    std::shared_ptr<Scene> source;
    Cy3DScene scene{};
    std::vector<Cy3DTexture> textures;
    explicit ExtensionInput(std::shared_ptr<Scene>);
};
struct OperationControl {
    std::atomic<bool> cancel{false};
    std::atomic<float> progress{0};
    Cy3DHost host();
};
struct ExportBundle {
    fs::path staging, destination;
    std::vector<fs::path> files;
    std::vector<fs::path> protected_files;
    ~ExportBundle();
    std::vector<fs::path> conflicts() const;
    void commit(bool replace);
};
std::shared_ptr<ExportBundle> exportScene(const ExtensionAction&, std::shared_ptr<Scene>, const fs::path&,
                                          const fs::path& source, OperationControl&);
std::shared_ptr<Scene> runTool(std::shared_ptr<ExtensionModule>, const ExtensionAction&, const void*,
                               std::shared_ptr<Scene>, OperationControl&);
} // namespace cy

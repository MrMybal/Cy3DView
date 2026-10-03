// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "extensions.h"
#include <fstream>
#include <sstream>
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace cy {
namespace {
void unload(void* handle) {
#ifdef _WIN32
    if (handle)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
    if (handle)
        dlclose(handle);
#endif
}
bool identifier(const std::string& s) {
    return !s.empty() && s.size() < 128 && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
fs::path resolvedPath(const fs::path& p) {
    return fs::weakly_canonical(fs::absolute(p));
}
bool same(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
    std::error_code ec;
    if (fs::equivalent(a, b, ec) && !ec)
        return true;
    return lower(utf8(resolvedPath(a))) == lower(utf8(resolvedPath(b)));
#else
    std::error_code ec;
    if (fs::equivalent(a, b, ec) && !ec)
        return true;
    return resolvedPath(a) == resolvedPath(b);
#endif
}
void checkDestination(const ExportBundle& bundle, const fs::path& relative) {
    auto target = bundle.destination / relative;
    for (const auto& file : bundle.protected_files)
        if (same(target, file))
            throw std::runtime_error("Export would overwrite a source asset.");
    auto resolved = resolvedPath(target).lexically_relative(resolvedPath(bundle.destination));
    if (resolved.empty() || resolved.is_absolute() || *resolved.begin() == "..")
        throw std::runtime_error("Export destination escapes its folder.");
    // Never follow symbolic links or replace directories through an output name.
    for (auto p = target; p != bundle.destination && !p.empty(); p = p.parent_path()) {
        std::error_code ec;
        if (fs::is_symlink(fs::symlink_status(p, ec)))
            throw std::runtime_error("Output contains a symbolic link.");
    }
    if (fs::exists(target) && !fs::is_regular_file(target))
        throw std::runtime_error("An output name is already used by a directory.");
}
} // namespace
ExtensionModule::ExtensionModule(const fs::path& file) {
#ifdef _WIN32
    handle = LoadLibraryExW(file.c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto query = handle ? reinterpret_cast<Cy3DGetExtensionFn>(
                              GetProcAddress(static_cast<HMODULE>(handle), CY3D_EXTENSION_ENTRY))
                        : nullptr;
#else
    handle = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto query = handle ? reinterpret_cast<Cy3DGetExtensionFn>(dlsym(handle, CY3D_EXTENSION_ENTRY)) : nullptr;
#endif
    api = query ? query() : nullptr;
    if (!api || api->api_version != CY3D_EXTENSION_VERSION || api->struct_size < sizeof(Cy3DExtension) ||
        !api->name || api->format_count > 256 || api->tool_count > 256 ||
        (api->format_count && (!api->formats || !api->export_scene)) ||
        (api->tool_count && (!api->tools || !api->create_tool || !api->destroy_tool || !api->draw_tool ||
                             !api->run_tool || !api->release_scene))) {
        unload(handle);
        handle = nullptr;
        throw std::runtime_error("Extension unavailable or incompatible: " + utf8(file.filename()));
    }
}
ExtensionModule::~ExtensionModule() {
    unload(handle);
}
Extensions::Extensions(const fs::path& directory) {
    std::error_code ec;
    for (const auto& file : fs::directory_iterator(directory, ec)) {
        if (file.path().extension() != ".cy3dx" || !file.is_regular_file())
            continue;
        std::ifstream input(file.path());
        std::string library, line;
        std::getline(input, library);
        if (!library.empty() && library.back() == '\r')
            library.pop_back();
        if (library.empty() || path(library).filename() != path(library))
            continue;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            std::stringstream split(line);
            std::vector<std::string> fields;
            for (std::string field; std::getline(split, field, '|');)
                fields.push_back(field);
            if ((fields.size() != 5 && fields.size() != 6) || !identifier(fields[1]))
                continue;
            ExtensionAction a;
            a.library = fs::absolute(directory / path(library));
            a.id = fields[1];
            a.name = fields[4];
            a.name_fr = fields.size() == 6 ? fields[5] : fields[4];
            if (fields[0] == "export" && identifier(fields[2])) {
                a.extension = fields[2];
                try {
                    a.capabilities = static_cast<uint32_t>(std::stoul(fields[3]));
                } catch (...) {
                    continue;
                }
                formats_.push_back(std::move(a));
            } else if (fields[0] == "tool" && fields.size() == 5) {
                a.name = fields[3];
                a.name_fr = fields[4];
                tools_.push_back(std::move(a));
            }
        }
    }
}
ExtensionInput::ExtensionInput(std::shared_ptr<Scene> s) : source(std::move(s)), scene(*source->data) {
    textures.resize(scene.texture_count);
    for (size_t i = 0; i < textures.size(); ++i) {
        const auto& image = source->images.at(i);
        textures[i] = {nullptr, image.rgba.data(), image.rgba.size(), static_cast<uint32_t>(image.width),
                       static_cast<uint32_t>(image.height)};
    }
    scene.memory_bytes = source->memory;
    scene.textures = textures.data();
    scene.dependencies = nullptr;
    scene.dependency_count = 0;
    scene.owner = nullptr;
}
Cy3DHost OperationControl::host() {
    return {
        CY3D_API_VERSION,
        sizeof(Cy3DHost),
        this,
        [](void* p) { return static_cast<OperationControl*>(p)->cancel.load() ? 1 : 0; },
        [](void* p, float f) { static_cast<OperationControl*>(p)->progress.store(std::clamp(f, 0.f, 1.f)); },
        2ull * 1024 * 1024 * 1024};
}
ExportBundle::~ExportBundle() {
    // staging is exclusively created by exportScene, never a user-supplied directory.
    std::error_code ec;
    if (!staging.empty())
        fs::remove_all(staging, ec);
}
std::vector<fs::path> ExportBundle::conflicts() const {
    std::vector<fs::path> result;
    for (const auto& f : files) {
        checkDestination(*this, f);
        if (fs::exists(destination / f))
            result.push_back(f);
    }
    return result;
}
void ExportBundle::commit(bool replace) {
    auto existing = conflicts();
    if (!replace && !existing.empty())
        throw std::runtime_error("Output files already exist.");
    auto backup = staging / ".cy3d-backup";
    if (fs::exists(backup))
        throw std::runtime_error("Invalid staging output.");
    fs::create_directory(backup);
    std::vector<fs::path> saved, installed;
    try {
        for (const auto& f : files) {
            checkDestination(*this, f);
            auto target = destination / f;
            if (fs::exists(target)) {
                if (!replace)
                    throw std::runtime_error("An output file appeared during export.");
                fs::create_directories((backup / f).parent_path());
                fs::rename(target, backup / f);
                saved.push_back(f);
            }
            fs::create_directories(target.parent_path());
            fs::rename(staging / f, target);
            installed.push_back(f);
        }
    } catch (...) {
        std::error_code ec;
        for (auto i = installed.rbegin(); i != installed.rend(); ++i)
            fs::remove(destination / *i, ec);
        for (auto i = saved.rbegin(); i != saved.rend(); ++i)
            fs::rename(backup / *i, destination / *i, ec);
        // If rollback cannot restore a file, retain the backup for manual recovery.
        for (const auto& f : saved)
            if (fs::exists(backup / f)) {
                auto recovery = staging;
                staging.clear();
                throw std::runtime_error("Export rollback needs recovery from: " + utf8(recovery));
            }
        throw;
    }
}
std::shared_ptr<ExportBundle> exportScene(const ExtensionAction& action, std::shared_ptr<Scene> source,
                                          const fs::path& output, const fs::path& sourceFile,
                                          OperationControl& control) {
    auto module = std::make_shared<ExtensionModule>(action.library);
    bool found = false;
    for (uint32_t i = 0; i < module->api->format_count; ++i)
        if (module->api->formats[i].id && action.id == module->api->formats[i].id)
            found = true;
    if (!found)
        throw std::runtime_error("Exporter is not declared by the loaded extension.");
    ExtensionInput input(source);
    auto host = control.host();
    auto bundle = std::make_shared<ExportBundle>();
    auto absolute = fs::absolute(output).lexically_normal();
    bundle->destination = absolute.parent_path();
    if (!fs::is_directory(bundle->destination) || absolute.filename().empty())
        throw std::runtime_error("Choose an existing output folder and a filename.");
    bundle->protected_files.push_back(sourceFile);
    for (const auto& dependency : source->dependencies)
        bundle->protected_files.push_back(dependency.file);
    checkDestination(*bundle, absolute.filename());
    for (uint64_t attempt = 0; attempt < 100; ++attempt) {
        auto candidate =
            bundle->destination /
            (".cy3d-export-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(attempt));
        if (fs::create_directory(candidate)) {
            bundle->staging = candidate;
            break;
        }
    }
    if (bundle->staging.empty())
        throw std::runtime_error("Cannot create export staging directory.");
    char error[2048]{};
    auto target = bundle->staging / absolute.filename();
    int code = module->api->export_scene(action.id.c_str(), &input.scene, utf8(target).c_str(), &host, error,
                                         sizeof(error));
    if (control.cancel)
        throw std::runtime_error("Operation cancelled.");
    if (code)
        throw std::runtime_error(error[0] ? error : "Export failed.");
    uint64_t size = 0;
    for (const auto& f : fs::recursive_directory_iterator(bundle->staging)) {
        if (fs::is_symlink(f.symlink_status()))
            throw std::runtime_error("Exporter created a symbolic link.");
        if (f.is_regular_file()) {
            size += f.file_size();
            bundle->files.push_back(f.path().lexically_relative(bundle->staging));
            if (size > host.max_output_bytes || bundle->files.size() > 10000)
                throw std::runtime_error("Export exceeds its output budget.");
        } else if (!f.is_directory())
            throw std::runtime_error("Invalid export output.");
    }
    if (!fs::is_regular_file(target) || !fs::file_size(target))
        throw std::runtime_error("Exporter did not create the requested model.");
    bundle->conflicts();
    control.progress = 1;
    return bundle;
}
std::shared_ptr<Scene> runTool(std::shared_ptr<ExtensionModule> module, const ExtensionAction& action,
                               const void* state, std::shared_ptr<Scene> input, OperationControl& control) {
    ExtensionInput view(input);
    auto host = control.host();
    char error[2048]{};
    Cy3DScene* output = nullptr;
    int code =
        module->api->run_tool(action.id.c_str(), state, &view.scene, &host, &output, error, sizeof(error));
    auto owned = std::shared_ptr<Cy3DScene>(output, [module](Cy3DScene* s) {
        if (s)
            module->api->release_scene(s);
    });
    if (control.cancel)
        throw std::runtime_error("Operation cancelled.");
    if (code)
        throw std::runtime_error(error[0] ? error : "Tool failed.");
    if (!output)
        return {};
    auto scene = std::make_shared<Scene>();
    scene->extension_data = owned;
    scene->data = output;
    prepareScene(*scene, {}, control.cancel, host.max_output_bytes);
    scene->reusable = false;
    scene->dependencies = input->dependencies;
    control.progress = 1;
    return scene;
}
} // namespace cy

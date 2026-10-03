// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "plugins.h"
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif
namespace cy {
Module::Module(const fs::path& file) {
#ifdef _WIN32
    handle = LoadLibraryExW(file.c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    auto query = handle ? reinterpret_cast<Cy3DGetPluginFn>(
                              GetProcAddress(static_cast<HMODULE>(handle), CY3D_PLUGIN_ENTRY))
                        : nullptr;
#else
    handle = dlopen(file.c_str(), RTLD_NOW | RTLD_LOCAL);
    auto query = handle ? reinterpret_cast<Cy3DGetPluginFn>(dlsym(handle, CY3D_PLUGIN_ENTRY)) : nullptr;
#endif
    api = query ? query() : nullptr;
    if (!api || api->api_version != CY3D_API_VERSION || api->struct_size < sizeof(Cy3DPlugin) || !api->load ||
        !api->release || !api->extensions || !api->name) {
#ifdef _WIN32
        if (handle)
            FreeLibrary(static_cast<HMODULE>(handle));
#else
        if (handle)
            dlclose(handle);
#endif
        handle = nullptr;
        throw std::runtime_error("Plugin indisponible ou ABI incompatible : " + utf8(file.filename()));
    }
}
Module::~Module() {
#ifdef _WIN32
    if (handle)
        FreeLibrary(static_cast<HMODULE>(handle));
#else
    if (handle)
        dlclose(handle);
#endif
}
Plugins::Plugins(const fs::path& directory) {
    std::error_code ec;
    for (auto& item : fs::directory_iterator(directory, ec)) {
        if (item.path().extension() != ".cy3d")
            continue;
        std::ifstream input(item.path());
        PluginEntry entry;
        std::string library;
        std::getline(input, entry.name);
        std::getline(input, library);
        std::getline(input, entry.extensions);
        std::string priority;
        if (std::getline(input, priority)) {
            try {
                entry.priority = std::stoi(priority);
            } catch (...) {
            }
        }
        for (auto* s : {&entry.name, &library, &entry.extensions})
            if (!s->empty() && s->back() == '\r')
                s->pop_back();
        // Manifests reference a sibling only, not arbitrary paths.
        if (library.empty() || path(library).filename() != path(library) || entry.extensions.empty())
            continue;
        entry.library = fs::absolute(directory / path(library));
        entry.extensions = ";" + lower(entry.extensions) + ";";
        entries_.push_back(std::move(entry));
    }
    std::sort(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.library < b.library;
    });
}
bool Plugins::supports(const fs::path& file) const {
    auto ext = ";" + extension(file) + ";";
    for (const auto& entry : entries_)
        if (entry.extensions.find(ext) != std::string::npos)
            return true;
    return false;
}
std::shared_ptr<Module> Plugins::forFile(const fs::path& file, const std::vector<Module*>& skipped) {
    std::lock_guard lock(mutex_);
    auto ext = ";" + extension(file) + ";";
    std::string error;
    for (auto& entry : entries_)
        if (entry.extensions.find(ext) != std::string::npos) {
            try {
                if (!entry.module)
                    entry.module = std::make_shared<Module>(entry.library);
                if (std::find(skipped.begin(), skipped.end(), entry.module.get()) != skipped.end())
                    continue;
                return entry.module;
            } catch (const std::exception& e) {
                entry.error = e.what();
                error = entry.error;
            }
        }
    throw std::runtime_error(error.empty() ? "Format non pris en charge : ." + extension(file) : error);
}
} // namespace cy

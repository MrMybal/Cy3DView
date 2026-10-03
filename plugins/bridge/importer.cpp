// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "cy3d_plugin.h"
#include "cy3d_process.hpp"
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#ifdef _WIN32
#include <winioctl.h>
#else
#include <dlfcn.h>
#endif
namespace {
namespace fs = std::filesystem;
fs::path native(const std::string& s) {
    return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
std::string read(const fs::path& file, size_t maximum = 4 * 1024 * 1024) {
    std::ifstream stream(file, std::ios::binary | std::ios::ate);
    if (!stream)
        return {};
    auto size = stream.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > maximum)
        throw std::runtime_error("Metadonnees de conversion trop grandes");
    std::string text(static_cast<size_t>(size), '\0');
    stream.seekg(0);
    stream.read(text.data(), size);
    return text;
}
rapidjson::Document json(const fs::path& file) {
    auto text = read(file);
    if (text.starts_with("\xef\xbb\xbf"))
        text.erase(0, 3);
    rapidjson::Document d;
    d.Parse(text.c_str());
    if (d.HasParseError())
        throw std::runtime_error("Metadonnees JSON invalides");
    return d;
}
std::string literal(const std::string& s) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    writer.String(s.c_str(), static_cast<rapidjson::SizeType>(s.size()));
    return buffer.GetString();
}
void write(const fs::path& file, const std::string& text) {
    std::ofstream output(file, std::ios::binary);
    output << text;
    if (!output)
        throw std::runtime_error("Cache de conversion inaccessible");
}
fs::path directory();
fs::path executable(const char* env, const std::vector<fs::path>& candidates) {
    if (auto value = std::getenv(env); value && *value) {
        auto file = native(value);
        if (!fs::is_regular_file(file))
            throw std::runtime_error(std::string(env) + " ne pointe pas vers un executable");
        return fs::absolute(file);
    }
    for (const auto& file : candidates) {
        std::error_code error;
        if (fs::is_regular_file(file, error))
            return file;
    }
    return {};
}
fs::path blender() {
    std::vector<fs::path> candidates;
#ifdef _WIN32
    if (auto programFiles = std::getenv("ProgramFiles")) {
        fs::path root = native(programFiles) / "Blender Foundation";
        std::error_code error;
        std::vector<fs::path> folders;
        for (auto& item : fs::directory_iterator(root, error))
            if (item.is_directory())
                folders.push_back(item.path());
        std::sort(folders.begin(), folders.end(), [](const auto& a, const auto& b) {
            auto version = [](const fs::path& p) {
                auto name = p.filename().string();
                auto position = name.find_first_of("0123456789");
                return position == std::string::npos ? 0. : std::strtod(name.c_str() + position, nullptr);
            };
            return version(a) > version(b);
        });
        for (auto& folder : folders)
            candidates.push_back(folder / "blender.exe");
    }
    wchar_t buffer[32768];
    DWORD n = SearchPathW(nullptr, L"blender.exe", nullptr, 32768, buffer, nullptr);
    if (n && n < 32768)
        candidates.emplace_back(buffer);
#else
    candidates = {"/Applications/Blender.app/Contents/MacOS/Blender", "/usr/bin/blender",
                  "/usr/local/bin/blender"};
#endif
    return executable("CY3D_BLENDER", candidates);
}
fs::path unreal(const fs::path& content, const fs::path& project) {
    std::vector<fs::path> candidates;
    if (content.parent_path().filename() == "Engine")
        candidates.push_back(content.parent_path() / "Binaries/Win64/UnrealEditor-Cmd.exe");
    std::string association;
    if (!project.empty()) {
        auto definition = json(project);
        if (definition.HasMember("EngineAssociation") && definition["EngineAssociation"].IsString())
            association = definition["EngineAssociation"].GetString();
    }
#ifdef _WIN32
    if (auto programData = std::getenv("ProgramData")) {
        auto manifest = native(programData) / "Epic/UnrealEngineLauncher/LauncherInstalled.dat";
        std::error_code ec;
        if (fs::exists(manifest, ec)) {
            auto installed = json(manifest);
            std::vector<std::pair<std::string, fs::path>> engines;
            if (installed.HasMember("InstallationList") && installed["InstallationList"].IsArray())
                for (const auto& item : installed["InstallationList"].GetArray())
                    if (item.HasMember("AppName") && item["AppName"].IsString() &&
                        item.HasMember("InstallLocation") && item["InstallLocation"].IsString()) {
                        std::string name = item["AppName"].GetString();
                        if (name.starts_with("UE_"))
                            engines.emplace_back(name, native(item["InstallLocation"].GetString()) /
                                                           "Engine/Binaries/Win64/UnrealEditor-Cmd.exe");
                    }
            std::sort(engines.begin(), engines.end(),
                      [](const auto& a, const auto& b) { return a.first > b.first; });
            for (const auto& engine : engines)
                if (!association.empty() && engine.first == "UE_" + association)
                    candidates.push_back(engine.second);
            if (association.empty())
                for (const auto& engine : engines)
                    candidates.push_back(engine.second);
        }
    }
#endif
    return executable("CY3D_UNREAL_EDITOR", candidates);
}
void mount(const fs::path& link, const fs::path& target) {
#ifdef _WIN32
    // NTFS junction creation does not require administrator/developer-mode privileges.
    fs::create_directory(link);
    auto substitute = L"\\??\\" + fs::absolute(target).wstring();
    auto display = fs::absolute(target).wstring();
    struct Header {
        DWORD tag;
        USHORT length, reserved, sub_offset, sub_length, print_offset, print_length;
    };
    size_t pathBytes = (substitute.size() + display.size() + 2) * sizeof(wchar_t);
    std::vector<uint8_t> bytes(sizeof(Header) + pathBytes);
    auto* header = reinterpret_cast<Header*>(bytes.data());
    header->tag = IO_REPARSE_TAG_MOUNT_POINT;
    header->length = static_cast<USHORT>(8 + pathBytes);
    header->sub_length = static_cast<USHORT>(substitute.size() * 2);
    header->print_offset = static_cast<USHORT>((substitute.size() + 1) * 2);
    header->print_length = static_cast<USHORT>(display.size() * 2);
    auto* paths = reinterpret_cast<wchar_t*>(bytes.data() + sizeof(Header));
    std::copy(substitute.begin(), substitute.end(), paths);
    std::copy(display.begin(), display.end(), paths + substitute.size() + 1);
    HANDLE file = CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    DWORD returned = 0;
    bool ok = file != INVALID_HANDLE_VALUE &&
              DeviceIoControl(file, FSCTL_SET_REPARSE_POINT, bytes.data(), static_cast<DWORD>(bytes.size()),
                              nullptr, 0, &returned, nullptr);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
    if (!ok)
        throw std::runtime_error("Impossible de monter le Content en lecture pour la conversion");
#else
    fs::create_directory_symlink(target, link);
#endif
}
struct Job {
    fs::path path;
    std::vector<fs::path> mounts;
    ~Job() {
        std::error_code error;
        for (const auto& link : mounts) {
            if (link.parent_path() != path / "Content")
                return;
#ifdef _WIN32
            if (!RemoveDirectoryW(link.c_str()))
                return;
#else
            if (!fs::remove(link, error) || error)
                return;
#endif
        }
        if (path.filename().string().starts_with("job-") &&
            path.parent_path().filename() == "Cy3DView-conversions")
            fs::remove_all(path, error);
    }
};
void mountContent(Job& job, const fs::path& content) {
    // The editor creates Developers/Collections on startup even with -NoSave.
    // Keep the Content root and these writable folders entirely in the temporary job.
    auto root = job.path / "Content";
    fs::create_directory(root);
    for (const auto& item : fs::directory_iterator(content)) {
        auto name = item.path().filename(), destination = root / name;
        auto text = name.string();
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (item.is_directory() && text != "developers" && text != "collections") {
            mount(destination, item.path());
            job.mounts.push_back(destination);
        } else if (item.is_directory())
            fs::copy(item.path(), destination, fs::copy_options::recursive);
        else if (item.is_regular_file())
            fs::copy_file(item.path(), destination);
    }
}
void replace(const fs::path& source, const fs::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Impossible d'enregistrer le cache converti");
#else
    fs::rename(source, destination);
#endif
}
std::pair<uintmax_t, int64_t> stamp(const fs::path& p) {
    std::error_code ec;
    auto time = fs::last_write_time(p, ec);
    if (ec)
        return {UINTMAX_MAX, 0};
    auto size = fs::file_size(p, ec);
    return {ec ? UINTMAX_MAX : size, static_cast<int64_t>(time.time_since_epoch().count())};
}
bool fresh(const fs::path& metadata, std::vector<std::string>& dependencies,
           std::vector<std::string>& warnings) {
    std::ifstream input(metadata);
    std::string magic;
    if (!std::getline(input, magic) || magic != "Cy3D-conversion-v3-ABI3")
        return false;
    std::string file;
    uintmax_t size;
    int64_t time;
    std::string tag;
    while (input >> tag) {
        if (tag == "file") {
            if (!(input >> std::quoted(file) >> size >> time) ||
                stamp(native(file)) != std::pair<uintmax_t, int64_t>{size, time})
                return false;
            dependencies.push_back(file);
        } else if (tag == "warning") {
            if (!(input >> std::quoted(file)))
                return false;
            warnings.push_back(file);
        } else
            return false;
    }
    return input.eof() && !dependencies.empty();
}
void trim(const fs::path& root, const fs::path& keep) {
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> files;
    uint64_t total = 0;
    for (auto& item : fs::directory_iterator(root, ec))
        if (item.is_regular_file() && item.path().extension() == ".glb") {
            total += item.file_size();
            if (item.path() != keep)
                files.emplace_back(item.last_write_time(), item.path());
        }
    std::sort(files.begin(), files.end());
    for (const auto& [time, file] : files) {
        (void)time;
        if (total <= 256ull * 1024 * 1024)
            break;
        auto bytes = fs::file_size(file, ec);
        if (!ec && fs::remove(file, ec)) {
            total -= bytes;
            auto sidecar = file;
            sidecar += ".deps";
            fs::remove(sidecar, ec);
        }
    }
}
struct Converted {
    void* library = nullptr;
    const Cy3DPlugin* plugin = nullptr;
    Cy3DScene* original = nullptr;
    Cy3DScene scene{};
    std::deque<std::string> strings;
    std::vector<const char*> dependencies, warnings;
    ~Converted() {
        if (original)
            plugin->release(original);
#ifdef _WIN32
        if (library)
            FreeLibrary(static_cast<HMODULE>(library));
#else
        if (library)
            dlclose(library);
#endif
    }
};
int load(const char* filename, const Cy3DHost* host, Cy3DScene** output, char* error, size_t size) {
    if (output)
        *output = nullptr;
    try {
        if (!filename || !host || !output || host->api_version != CY3D_API_VERSION)
            throw std::runtime_error("ABI invalide");
        if (host->is_cancelled && host->is_cancelled(host->context))
            throw std::runtime_error("Chargement annule");
        auto input = fs::absolute(native(filename));
        auto extension = input.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool blend = extension == ".blend";
        fs::path content, project;
        std::string package;
        fs::path tool;
        if (blend) {
            tool = blender();
            if (tool.empty())
                throw std::runtime_error("Blender installe est requis pour les .blend recents. Configurer "
                                         "CY3D_BLENDER si necessaire.");
        } else {
            for (auto parent = input.parent_path(); !parent.empty(); parent = parent.parent_path()) {
                if (parent.filename() == "Content") {
                    content = parent;
                    break;
                }
                if (parent == parent.parent_path())
                    break;
            }
            if (content.empty())
                throw std::runtime_error("Le .uasset doit rester dans le dossier Content de son projet ou de "
                                         "son moteur, avec ses dependances.");
            for (auto& item : fs::directory_iterator(content.parent_path()))
                if (item.path().extension() == ".uproject") {
                    project = item.path();
                    break;
                }
            bool engine = content.parent_path().filename() == "Engine";
            if (!engine && project.empty())
                throw std::runtime_error("Projet .uproject introuvable a cote de Content. Les packages de "
                                         "jeux cuits ne sont pas pris en charge par cette passerelle.");
            auto relative = fs::relative(input, content);
            relative.replace_extension();
            auto relativeText = relative.generic_u8string();
            package = (engine ? "/Engine/" : "/Game/") +
                      std::string(reinterpret_cast<const char*>(relativeText.data()), relativeText.size());
            tool = unreal(content, project);
            if (tool.empty())
                throw std::runtime_error("La version Unreal Editor du projet est requise. Configurer "
                                         "CY3D_UNREAL_EDITOR si necessaire.");
        }
        auto script = directory() / "scripts" / (blend ? "blender_export.py" : "unreal_export.py");
        if (!fs::exists(script))
            throw std::runtime_error("Script de conversion manquant dans plugins/scripts");
        std::string identity = cy::pathText(input) + "\n" + cy::pathText(tool) + "\n" + cy::pathText(script) +
                               "\n" + cy::pathText(directory() / CY3D_BRIDGE_LIBRARY);
        uint64_t hash = 1469598103934665603ull;
        for (unsigned char c : identity) {
            hash ^= c;
            hash *= 1099511628211ull;
        }
        std::ostringstream key;
        key << std::hex << hash;
        auto temporary = fs::temp_directory_path();
#ifdef _WIN32
        wchar_t expanded[32768];
        DWORD expandedSize = GetLongPathNameW(temporary.c_str(), expanded, 32768);
        if (expandedSize && expandedSize < 32768)
            temporary = expanded;
        HANDLE temporaryHandle =
            CreateFileW(temporary.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (temporaryHandle != INVALID_HANDLE_VALUE) {
            DWORD n = GetFinalPathNameByHandleW(temporaryHandle, expanded, 32768,
                                                FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            CloseHandle(temporaryHandle);
            if (n && n < 32768) {
                std::wstring resolved(expanded, n);
                if (resolved.starts_with(L"\\\\?\\"))
                    resolved.erase(0, 4);
                temporary = resolved;
            }
        }
#endif
        auto cache = temporary / "Cy3DView-conversions";
        fs::create_directories(cache);
        auto converted = cache / (key.str() + ".glb");
        auto metadata = converted;
        metadata += ".deps";
        std::vector<std::string> dependencies, conversionWarnings;
        bool hit = fs::is_regular_file(converted) && fresh(metadata, dependencies, conversionWarnings);
        if (!hit) {
            dependencies.clear();
            conversionWarnings.clear();
            static std::atomic<unsigned> sequence{0};
            auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                          std::to_string(sequence++);
            Job job{cache / ("job-" + unique)};
            fs::create_directory(job.path);
            auto target = job.path / "model.glb", deps = job.path / "dependencies.json",
                 failure = job.path / "error.txt", request = job.path / "request.json",
                 bootstrap = job.path / "export.py";
            rapidjson::Document d;
            d.SetObject();
            auto& a = d.GetAllocator();
            auto add = [&](const char* name, const std::string& value) {
                d.AddMember(rapidjson::Value(name, a), rapidjson::Value(value.c_str(), a), a);
            };
            add("source", cy::pathText(input));
            add("target", cy::pathText(target));
            add("dependencies", cy::pathText(deps));
            add("error", cy::pathText(failure));
            add("warnings", cy::pathText(job.path / "warnings.json"));
            add("package", package);
            rapidjson::Value roots(rapidjson::kObjectType);
            std::vector<std::string> arguments;
            if (blend)
                arguments = {
                    "--background", "--factory-startup",    "--disable-autoexec", "--python-exit-code", "1",
                    "--python",     cy::pathText(bootstrap)};
            else {
                write(job.path / "Preview.uproject",
                      "{\"FileVersion\":3,\"Plugins\":[{\"Name\":\"PythonScriptPlugin\",\"Enabled\":true},{"
                      "\"Name\":\"GLTFExporter\",\"Enabled\":true}]}");
                auto engineContent = tool.parent_path().parent_path().parent_path() / "Content";
                roots.AddMember("/Engine/", rapidjson::Value(cy::pathText(engineContent).c_str(), a), a);
                if (!package.starts_with("/Engine/")) {
                    mountContent(job, content);
                    roots.AddMember("/Game/", rapidjson::Value(cy::pathText(content).c_str(), a), a);
                }
                arguments = {cy::pathText(job.path / "Preview.uproject"),
                             "-run=pythonscript",
                             "-script=" + cy::pathText(bootstrap),
                             "-unattended",
                             "-nosplash",
                             "-NoSound",
                             "-NoSourceControl",
                             "-NoShaderCompile",
                             "-NoSave",
                             "-abslog=" + cy::pathText(job.path / "unreal.log"),
                             "-UserDir=" + cy::pathText(job.path / "User"),
                             "-ddc=NoShared"};
            }
            d.AddMember("roots", roots, a);
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            d.Accept(writer);
            write(request, buffer.GetString());
            write(bootstrap, "import runpy\nrunpy.run_path(" + literal(cy::pathText(script)) +
                                 ", init_globals={'REQUEST':" + literal(cy::pathText(request)) + "})\n");
            std::vector<std::string> environment = {
                "UE-LocalDataCachePath=" + cy::pathText(job.path / "DDC"), "UE-SharedDataCachePath=None",
                "BLENDER_USER_CONFIG=" + cy::pathText(job.path / "Blender/config"),
                "BLENDER_USER_SCRIPTS=" + cy::pathText(job.path / "Blender/scripts"),
                "BLENDER_USER_EXTENSIONS=" + cy::pathText(job.path / "Blender/extensions")};
            if (host->progress)
                host->progress(host->context, .05f);
            int code = cy::run(tool, arguments, job.path / "process.log", host, 240, environment);
            if (code || !fs::is_regular_file(target)) {
                auto message = read(failure, 8192);
                auto diagnostic = cache / (key.str() + ".failure.log");
                fs::copy_file(job.path / "process.log", diagnostic, fs::copy_options::overwrite_existing);
                throw std::runtime_error(
                    (message.empty()
                         ? (blend
                                ? std::string("Conversion Blender echouee")
                                : std::string(
                                      "Conversion Unreal echouee : projet/version ou dependances manquantes"))
                         : message) +
                    ". Journal : " + cy::pathText(diagnostic));
            }
            if (fs::file_size(target) > host->max_output_bytes)
                throw std::runtime_error("Modele converti trop volumineux");
            auto files = json(deps);
            if (!files.IsArray())
                throw std::runtime_error("Dependances de conversion invalides");
            for (const auto& file : files.GetArray())
                if (file.IsString())
                    dependencies.emplace_back(file.GetString());
            if (fs::is_regular_file(job.path / "warnings.json")) {
                auto messages = json(job.path / "warnings.json");
                if (!messages.IsArray())
                    throw std::runtime_error("Avertissements de conversion invalides");
                for (const auto& message : messages.GetArray())
                    if (message.IsString())
                        conversionWarnings.emplace_back(message.GetString());
            }
            dependencies.push_back(cy::pathText(input));
            dependencies.push_back(cy::pathText(tool));
            dependencies.push_back(cy::pathText(script));
            if (!project.empty())
                dependencies.push_back(cy::pathText(project));
            dependencies.push_back(cy::pathText(directory() / CY3D_BRIDGE_LIBRARY));
            std::sort(dependencies.begin(), dependencies.end());
            dependencies.erase(std::unique(dependencies.begin(), dependencies.end()), dependencies.end());
            replace(target, converted);
            dependencies.push_back(cy::pathText(converted));
            std::ostringstream stamps;
            stamps << "Cy3D-conversion-v3-ABI3\n";
            for (const auto& file : dependencies) {
                auto [bytes, time] = stamp(native(file));
                stamps << "file " << std::quoted(file) << ' ' << bytes << ' ' << time << '\n';
            }
            for (const auto& warning : conversionWarnings)
                stamps << "warning " << std::quoted(warning) << '\n';
            write(job.path / "metadata", stamps.str());
            replace(job.path / "metadata", metadata);
            trim(cache, converted);
        }
        if (host->progress)
            host->progress(host->context, .65f);
        auto owner = std::make_unique<Converted>();
        auto library = directory() / CY3D_ASSIMP_LIBRARY;
#ifdef _WIN32
        owner->library = LoadLibraryExW(library.c_str(), nullptr,
                                        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        auto entry = owner->library ? reinterpret_cast<Cy3DGetPluginFn>(GetProcAddress(
                                          static_cast<HMODULE>(owner->library), CY3D_PLUGIN_ENTRY))
                                    : nullptr;
#else
        owner->library = dlopen(library.c_str(), RTLD_NOW | RTLD_LOCAL);
        auto entry = owner->library
                         ? reinterpret_cast<Cy3DGetPluginFn>(dlsym(owner->library, CY3D_PLUGIN_ENTRY))
                         : nullptr;
#endif
        if (!entry)
            throw std::runtime_error("Plugin Assimp requis a cote de la passerelle");
        owner->plugin = entry();
        if (owner->plugin->api_version != CY3D_API_VERSION)
            throw std::runtime_error("Version du plugin Assimp incompatible");
        Cy3DHost delegate = *host;
        delegate.progress = nullptr;
        int code =
            owner->plugin->load(cy::pathText(converted).c_str(), &delegate, &owner->original, error, size);
        if (code)
            return code;
        owner->scene = *owner->original;
        owner->scene.owner = owner.get();
        for (const auto& file : dependencies) {
            owner->strings.push_back(file);
            owner->dependencies.push_back(owner->strings.back().c_str());
        }
        for (uint32_t i = 0; i < owner->original->warning_count; ++i)
            owner->warnings.push_back(owner->original->warnings[i]);
        for (const auto& warning : conversionWarnings) {
            owner->strings.push_back(warning);
            owner->warnings.push_back(owner->strings.back().c_str());
        }
        owner->strings.push_back(blend ? "Lecture via Blender : les materiaux proceduraux non exportables en "
                                         "glTF ne sont pas cuits automatiquement."
                                       : "Lecture via Unreal Editor : seuls les meshes de projet non cuits "
                                         "et leurs materiaux exportables en glTF sont pris en charge.");
        owner->warnings.push_back(owner->strings.back().c_str());
        owner->scene.dependencies = owner->dependencies.data();
        owner->scene.dependency_count = static_cast<uint32_t>(owner->dependencies.size());
        owner->scene.warnings = owner->warnings.data();
        owner->scene.warning_count = static_cast<uint32_t>(owner->warnings.size());
        *output = &owner.release()->scene;
        if (host->progress)
            host->progress(host->context, 1);
        return 0;
    } catch (const std::exception& e) {
        if (error && size)
            std::snprintf(error, size, "%s", e.what());
    } catch (...) {
        if (error && size)
            std::snprintf(error, size, "Erreur de conversion");
    }
    return host && host->is_cancelled && host->is_cancelled(host->context) ? -2 : -1;
}
fs::path directory() {
#ifdef _WIN32
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&load), &module);
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(module, path, 32768);
    if (!n || n >= 32768)
        throw std::runtime_error("Chemin de passerelle inconnu");
    return fs::path(path).parent_path();
#else
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(&load), &info))
        throw std::runtime_error("Chemin de passerelle inconnu");
    return fs::absolute(info.dli_fname).parent_path();
#endif
}
void release(Cy3DScene* scene) {
    delete static_cast<Converted*>(scene->owner);
}
} // namespace
extern "C" CY3D_EXPORT const Cy3DPlugin* Cy3D_GetPlugin() {
    static const Cy3DPlugin plugin{
        CY3D_API_VERSION, sizeof(Cy3DPlugin), "Blender et Unreal · passerelles", "1.0", "blend;uasset", load,
        release};
    return &plugin;
}

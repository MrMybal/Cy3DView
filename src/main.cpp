// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "renderer.h"
#include "branding.h"
#include "localization.h"
#include "ui_navigation.h"
#include "updater.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <chrono>
#include <future>
#include <iostream>
#include <fstream>
#include <unordered_map>
#ifdef _WIN32
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif
namespace cy {
fs::path executablePath(const char* arg) {
    (void)arg;
#ifdef _WIN32
    std::wstring value(32768, L'\0');
    auto size = GetModuleFileNameW(nullptr, value.data(), static_cast<DWORD>(value.size()));
    value.resize(size);
    return fs::path(value);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string value(size, '\0');
    _NSGetExecutablePath(value.data(), &size);
    return fs::weakly_canonical(value.c_str());
#else
    std::error_code ec;
    auto file = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::absolute(path(arg)) : file;
#endif
}
struct FileEntry {
    fs::path path;
    bool directory;
    std::string name, folded, label;
};
struct Folder {
    fs::path path;
    std::vector<FileEntry> entries;
    std::string error;
    std::vector<fs::path> models;
    std::unordered_map<fs::path, size_t> model_indices;
};
class App {
    GLFWwindow* window_;
    Plugins& plugins_;
    Loader loader_;
    Renderer renderer_;
    Branding branding_;
    fs::path executable_, requested_, current_, directory_;
    Localization localization_;
    Updater updater_{[] { glfwPostEmptyEvent(); }};
    bool updates_ = false;
    char update_token_[257]{};
    std::string update_error_;
    bool persist_language_ = true;
    const char* tr(const char* english) const { return localization_.text(english); }
    const char* label(const char* english) const { return localization_.label(english); }
    std::shared_ptr<Scene> scene_;
    std::shared_ptr<Scene> staged_scene_;
    fs::path staged_file_;
    bool staged_cached_ = false;
    std::chrono::steady_clock::time_point upload_start_;
    std::chrono::steady_clock::time_point load_start_;
    std::future<Folder> scan_;
    std::shared_ptr<std::atomic<bool>> scan_cancel_;
    Folder folder_;
    fs::path nextScan_;
    bool filter_dirty_ = true;
    std::vector<size_t> filtered_entries_;
    uint64_t generation_ = 0;
    bool loading_ = false, explorer_ = true, details_ = true, spin_ = false, fullscreen_ = false;
    bool about_ = false, save_ = false, fromCache_ = false;
    int oldX_ = 0, oldY_ = 0, oldW_ = 0, oldH_ = 0;
    char pathInput_[4096]{}, search_[256]{}, saveInput_[4096]{};
    std::string error_, dropped_;
    double uploadMs_ = 0, frameMs_ = 0;
    GLuint viewport_ = 0;
    int viewportW_ = 1, viewportH_ = 1;
    std::vector<fs::path> neighbors(const fs::path& file) const {
        std::vector<fs::path> result;
        auto it = folder_.model_indices.find(file);
        if (it != folder_.model_indices.end()) {
            auto index = it->second;
            if (index + 1 < folder_.models.size())
                result.push_back(folder_.models[index + 1]);
            if (index > 0)
                result.push_back(folder_.models[index - 1]);
        }
        return result;
    }
    void scan(const fs::path& directory) {
        directory_ = directory;
        snprintf(pathInput_, sizeof(pathInput_), "%s", utf8(directory_).c_str());
        if (scan_.valid()) {
            nextScan_ = directory;
            if (scan_cancel_)
                *scan_cancel_ = true;
            return;
        }
        nextScan_.clear();
        scan_cancel_ = std::make_shared<std::atomic<bool>>(false);
        auto cancel = scan_cancel_;
        scan_ = std::async(std::launch::async, [this, directory, cancel] {
            Folder folder;
            folder.path = directory;
            std::error_code ec;
            fs::directory_iterator iterator(directory, fs::directory_options::skip_permission_denied, ec),
                end;
            if (ec) {
                folder.error = "Impossible de lire ce dossier : " + ec.message();
                return folder;
            }
            for (; iterator != end; iterator.increment(ec)) {
                if (*cancel)
                    return folder;
                if (ec) {
                    folder.error = ec.message();
                    break;
                }
                std::error_code typeError;
                bool isDir = iterator->is_directory(typeError);
                if (isDir || (iterator->is_regular_file(typeError) && plugins_.supports(iterator->path()))) {
                    auto name = utf8(iterator->path().filename());
                    folder.entries.push_back(
                        {iterator->path(), isDir, name, lower(name), (isDir ? "+  " : "   ") + name});
                }
            }
            if (*cancel)
                return folder;
            std::sort(folder.entries.begin(), folder.entries.end(), [](const auto& a, const auto& b) {
                if (a.directory != b.directory)
                    return a.directory > b.directory;
                return a.folded == b.folded ? a.name < b.name : naturalLessFolded(a.folded, b.folded);
            });
            for (const auto& entry : folder.entries)
                if (!entry.directory) {
                    folder.model_indices.emplace(entry.path, folder.models.size());
                    folder.models.push_back(entry.path);
                }
            return folder;
        });
    }
    void open(const fs::path& file) {
        std::error_code ec;
        auto absolute = fs::absolute(file, ec).lexically_normal();
        if (ec) {
            error_ = ec.message();
            return;
        }
        if (fs::is_directory(absolute, ec)) {
            scan(absolute);
            return;
        }
        requested_ = absolute;
        loading_ = true;
        load_start_ = std::chrono::steady_clock::now();
        error_.clear();
        spin_ = false;
        renderer_.cancelUpload();
        staged_scene_.reset();
        generation_ = loader_.request(absolute, neighbors(absolute));
        if (directory_ != absolute.parent_path())
            scan(absolute.parent_path());
    }
    void step(int direction) {
        auto it = folder_.model_indices.find(loading_ ? requested_ : current_);
        if (folder_.models.empty())
            return;
        if (it == folder_.model_indices.end()) {
            open(folder_.models.front());
            return;
        }
        auto index = static_cast<ptrdiff_t>(it->second) + direction;
        if (index >= 0 && index < static_cast<ptrdiff_t>(folder_.models.size()))
            open(folder_.models[static_cast<size_t>(index)]);
    }
    void cancelLoad() {
        generation_ = loader_.cancel();
        renderer_.cancelUpload();
        staged_scene_.reset();
        loading_ = false;
        error_.clear();
    }
    void fit() {
        if (scene_)
            renderer_.camera.fit(scene_->data->bounds_min, scene_->data->bounds_max,
                                 static_cast<float>(viewportW_) / viewportH_);
    }
    void fullscreen() {
        if (fullscreen_) {
            glfwSetWindowMonitor(window_, nullptr, oldX_, oldY_, oldW_, oldH_, 0);
            fullscreen_ = false;
        } else {
            glfwGetWindowPos(window_, &oldX_, &oldY_);
            glfwGetWindowSize(window_, &oldW_, &oldH_);
            auto monitor = glfwGetPrimaryMonitor();
            auto mode = glfwGetVideoMode(monitor);
            glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            fullscreen_ = true;
        }
    }
    void chooseFile() {
#ifdef _WIN32
        wchar_t filename[32768]{};
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = glfwGetWin32Window(window_);
        dialog.lpstrFile = filename;
        dialog.nMaxFile = 32768;
        dialog.lpstrTitle =
            localization_.language == Language::French ? L"Ouvrir un modèle 3D" : L"Open a 3D model";
        std::wstring filter = localization_.language == Language::French ? L"Modèles 3D" : L"3D models";
        filter.push_back(0);
        std::string extensions;
        for (const auto& plugin : plugins_.entries()) {
            size_t start = 0;
            while (start < plugin.extensions.size()) {
                auto end = plugin.extensions.find(';', start);
                if (end == std::string::npos)
                    end = plugin.extensions.size();
                if (end > start)
                    extensions += "*." + plugin.extensions.substr(start, end - start) + ";";
                start = end + 1;
            }
        }
        if (!extensions.empty())
            extensions.pop_back();
        filter += path(extensions.empty() ? "*.*" : extensions).wstring();
        filter.push_back(0);
        filter += localization_.language == Language::French ? L"Tous les fichiers" : L"All files";
        filter.push_back(0);
        filter += L"*.*";
        filter.push_back(0);
        filter.push_back(0);
        dialog.lpstrFilter = filter.c_str();
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameW(&dialog))
            open(fs::path(filename));
#else
        explorer_ = true;
        ImGui::SetKeyboardFocusHere();
#endif
    }
    void explorer() {
        ImGui::TextColored({.2f, .85f, .77f, 1}, tr("EXPLORER"));
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##dossier", pathInput_, sizeof(pathInput_),
                             ImGuiInputTextFlags_EnterReturnsTrue))
            open(path(pathInput_));
        if (ImGui::Button(label("Parent folder"), {-1, 28}))
            scan(directory_.parent_path());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##filtre", tr("Search models..."), search_, sizeof(search_)))
            filter_dirty_ = true;
        ImGui::Separator();
        if (scan_.valid())
            ImGui::TextDisabled(tr("Reading folder..."));
        if (!folder_.error.empty())
            ImGui::TextWrapped("%s", localization_.diagnostic(folder_.error).c_str());
        if (ImGui::BeginChild("fichiers", {0, 0})) {
            if (filter_dirty_) {
                auto filter = lower(search_);
                filtered_entries_.clear();
                for (size_t i = 0; i < folder_.entries.size(); ++i)
                    if (filter.empty() || folder_.entries[i].folded.find(filter) != std::string::npos)
                        filtered_entries_.push_back(i);
                filter_dirty_ = false;
            }
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(filtered_entries_.size()), 26 + ImGui::GetStyle().ItemSpacing.y);
            while (clipper.Step())
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    size_t index = filtered_entries_[static_cast<size_t>(row)];
                    const auto& item = folder_.entries[index];
                    ImGui::PushID(static_cast<int>(index));
                    bool selected = item.path == (loading_ ? requested_ : current_);
                    if (item.directory)
                        ImGui::PushStyleColor(ImGuiCol_Text, {.57f, .67f, .71f, 1});
                    if (ImGui::Selectable(item.label.c_str(), selected, 0, {0, 26})) {
                        if (item.directory)
                            scan(item.path);
                        else
                            open(item.path);
                    }
                    if (item.directory)
                        ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", utf8(item.path).c_str());
                    ImGui::PopID();
                }
        }
        ImGui::EndChild();
    }
    void details() {
        ImGui::TextColored({.2f, .85f, .77f, 1}, tr("MODEL"));
        ImGui::Spacing();
        if (!scene_) {
            ImGui::TextDisabled(tr("No model open"));
            return;
        }
        auto& s = *scene_->data;
        ImGui::TextWrapped("%s", utf8(current_.filename()).c_str());
        ImGui::TextDisabled(".%s", extension(current_).c_str());
        ImGui::Separator();
        ImGui::Text(tr("Vertices  %llu"), static_cast<unsigned long long>(scene_->vertices));
        ImGui::Text("Triangles  %llu", static_cast<unsigned long long>(scene_->triangles));
        if (scene_->points)
            ImGui::Text("Points  %llu", static_cast<unsigned long long>(scene_->points));
        if (scene_->splats)
            ImGui::Text("Splats  %llu", static_cast<unsigned long long>(scene_->splats));
        ImGui::Text(tr("Meshes  %u"), s.mesh_count);
        ImGui::Text("Instances  %u", s.instance_count);
        ImGui::Text(tr("Materials  %u"), s.material_count);
        ImGui::Text("Textures  %u", s.texture_count);
        ImGui::Spacing();
        if (s.animation_count && renderer_.animations()) {
            ImGui::Separator();
            ImGui::TextDisabled("ANIMATIONS");
            auto animations = renderer_.animations();
            renderer_.animation_clip =
                std::clamp(renderer_.animation_clip, 0, static_cast<int>(animations->clips.size()) - 1);
            if (ImGui::BeginCombo("##clip", animations->clips[renderer_.animation_clip].name.c_str())) {
                for (int i = 0; i < static_cast<int>(animations->clips.size()); ++i)
                    if (ImGui::Selectable(animations->clips[i].name.c_str(), i == renderer_.animation_clip)) {
                        renderer_.animation_clip = i;
                        renderer_.animation_time = 0;
                    }
                ImGui::EndCombo();
            }
            ImGui::Checkbox(label("Play"), &renderer_.playing);
            ImGui::SameLine();
            ImGui::Checkbox(label("Loop"), &renderer_.animation_loop);
            float time = static_cast<float>(renderer_.animation_time);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##animationTime", &time, 0,
                                   static_cast<float>(animations->clips[renderer_.animation_clip].duration),
                                   tr("Time: %.2f s"))) {
                renderer_.animation_time = time;
                renderer_.playing = false;
            }
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##speed", &renderer_.animation_speed, .1f, 3, tr("Speed: %.2fx"));
        }
        if (s.node_count)
            ImGui::Checkbox(label("Show skeleton"), &renderer_.skeleton);
        if (scene_->points && !scene_->splats) {
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##pointSize", &renderer_.point_size, 1, 16, tr("Point size: %.1f"));
        }
        ImGui::Separator();
        ImGui::TextDisabled(tr("DIMENSIONS (file units)"));
        ImGui::Text("X  %.4g", s.bounds_max[0] - s.bounds_min[0]);
        ImGui::Text("Y  %.4g", s.bounds_max[1] - s.bounds_min[1]);
        ImGui::Text("Z  %.4g", s.bounds_max[2] - s.bounds_min[2]);
        ImGui::Separator();
        ImGui::TextDisabled(tr("DISPLAY"));
        ImGui::SetNextItemWidth(-1);
        const char* modes[]{tr("PBR materials"), tr("Clay"), tr("Normals")};
        ImGui::Combo("##mode", &renderer_.mode, modes, 3);
        ImGui::Checkbox(label("Wireframe (F)"), &renderer_.wire);
        ImGui::Checkbox("Textures (T)", &renderer_.textured);
        ImGui::Checkbox(label("Normal maps"), &renderer_.normal_maps);
        ImGui::Checkbox(label("Material occlusion"), &renderer_.occlusion);
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##exposure", &renderer_.exposure, -3, 3, tr("Exposure: %.1f EV"));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##environment", &renderer_.environment, 0, 3, tr("Studio light: %.2f"));
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderAngle("##lightRotation", &renderer_.light_rotation, -180, 180, tr("Rotation: %.0f deg"));
        ImGui::Checkbox(label("Grid (G)"), &renderer_.grid);
        ImGui::Checkbox(label("Rotate (Space)"), &spin_);
        if (ImGui::Button(label("Frame model (Home)"), {-1, 28}))
            fit();
        ImGui::Separator();
        ImGui::TextDisabled(tr("PERFORMANCE"));
        ImGui::Text("Import  %.1f ms", scene_->import_ms);
        ImGui::Text(tr("GPU upload  %.1f ms"), uploadMs_);
        ImGui::Text(tr("CPU render  %.2f ms"), frameMs_);
        ImGui::Text(tr("Scene  %.1f MiB"), scene_->memory / 1048576.);
        ImGui::Text(tr("GPU estimate  %.1f MiB"), renderer_.gpu_bytes / 1048576.);
        ImGui::Text(tr("CPU cache  %.1f / 256 MiB"), loader_.cache_usage.load() / 1048576.);
        ImGui::Text(tr("Render cache  %.1f / 256 MiB"), renderer_.gpuCacheBytes() / 1048576.);
        if (fromCache_)
            ImGui::TextColored({.2f, .85f, .77f, 1}, tr("Opened from cache"));
        if (renderer_.last_upload_cached)
            ImGui::TextColored({.2f, .85f, .77f, 1}, tr("GPU buffers reused"));
        for (const auto& warning : scene_->warnings) {
            auto translated = localization_.diagnostic(warning);
            ImGui::Spacing();
            ImGui::TextWrapped("%s", translated.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", translated.c_str());
        }
    }
    void popups() {
        if (updates_) {
            ImGui::OpenPopup(label("Updates"));
            updates_ = false;
        }
        ImGui::SetNextWindowSize({580, 0}, ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(label("Updates"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            auto update = updater_.snapshot();
            bool busy = update.phase == UpdatePhase::Checking || update.phase == UpdatePhase::Downloading;
            ImGui::Text(tr("Installed version: %s"), CY3D_VERSION);
            if (update.phase == UpdatePhase::Idle)
                ImGui::TextWrapped("%s", tr("Check GitHub for the latest stable version."));
            if (update.phase == UpdatePhase::Checking) {
                const char* frames[]{"|", "/", "-", "\\"};
                ImGui::Text("%s  %s", frames[static_cast<int>(ImGui::GetTime() * 8) % 4],
                            tr("Checking for updates..."));
            }
            if (update.phase == UpdatePhase::Current)
                ImGui::TextWrapped("%s", tr("You have the latest version."));
            if (update.phase == UpdatePhase::Available || update.phase == UpdatePhase::Downloading ||
                update.phase == UpdatePhase::Ready) {
                ImGui::Text(tr("New version: %s"), update.release.version.c_str());
                if (!update.release.notes.empty()) {
                    ImGui::BeginChild("release-notes", {0, 145}, true);
                    ImGui::TextWrapped("%s", update.release.notes.c_str());
                    ImGui::EndChild();
                }
#ifdef _WIN32
                if (update.phase == UpdatePhase::Available && ImGui::Button(label("Download update")))
                    updater_.download(update_token_);
                if (update.phase == UpdatePhase::Downloading) {
                    auto progress =
                        update.release.size ? static_cast<float>(update.received) / update.release.size : 0.f;
                    ImGui::ProgressBar(progress, {-1, 22});
                    ImGui::Text(tr("Downloading and verifying... %.1f / %.1f MiB"),
                                update.received / 1048576., update.release.size / 1048576.);
                }
                if (update.phase == UpdatePhase::Ready) {
                    ImGui::TextWrapped(
                        "%s", tr("Download verified. The app will close and the installer will open in the "
                                 "current folder. Preferences and additional plugins are kept."));
                    if (ImGui::Button(label("Close app and install"))) {
                        if (updater_.install(executable_.parent_path(),
                                             localization_.language == Language::French, update_error_))
                            glfwSetWindowShouldClose(window_, GLFW_TRUE);
                    }
                }
#else
                ImGui::TextWrapped("%s", tr("Download the package for your system from the release page."));
#endif
            }
            if (update.phase == UpdatePhase::Cancelled)
                ImGui::TextWrapped("%s", tr("Update cancelled."));
            if (!update.error.empty())
                ImGui::TextWrapped("%s", localization_.diagnostic(update.error).c_str());
            if (!update_error_.empty())
                ImGui::TextWrapped("%s", localization_.diagnostic(update_error_).c_str());
            if (ImGui::CollapsingHeader(label("Private repository access (optional)"))) {
                ImGui::TextWrapped(
                    "%s",
                    tr("Only needed while the repository is private. Use a GitHub token with Contents: read "
                       "access to Cy3DView. It stays in memory for this session and is never saved."));
                ImGui::BeginDisabled(busy);
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##github-token", update_token_, sizeof(update_token_),
                                 ImGuiInputTextFlags_Password);
                ImGui::EndDisabled();
            }
            ImGui::Separator();
            ImGui::BeginDisabled(busy);
            if (ImGui::Button(label("Check for updates"))) {
                update_error_.clear();
                updater_.check(CY3D_VERSION, update_token_);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button(label("GitHub releases")))
                Updater::openReleases();
            if (busy) {
                ImGui::SameLine();
                if (ImGui::Button(label("Cancel")))
                    updater_.cancel();
            }
            ImGui::SameLine();
            if (ImGui::Button(label("Close")))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (about_) {
            ImGui::OpenPopup(label("Formats and plugins"));
            about_ = false;
        }
        if (ImGui::BeginPopupModal(label("Formats and plugins"), nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Image(static_cast<ImTextureID>(branding_.texture()), {72, 72});
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::Text("Cy3DView " CY3D_VERSION);
            ImGui::TextDisabled(tr("3D model viewer"));
            ImGui::TextDisabled("Copyright (C) 2026 Cyberalien");
            ImGui::TextWrapped("%s", tr("GNU GPL v3.0. No warranty. See LICENSE for your rights."));
            ImGui::EndGroup();
            ImGui::Separator();
            for (const auto& plugin : plugins_.entries()) {
                ImGui::TextColored({.2f, .85f, .77f, 1}, "%s", localization_.diagnostic(plugin.name).c_str());
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 580);
                ImGui::TextWrapped("%s", plugin.extensions.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
            }
            ImGui::TextWrapped(
                tr("Available formats depend on installed plugins. Proprietary CAD formats, Blender and "
                   "Unreal require their respective software. USD support is experimental."));
            if (ImGui::Button(label("Close"), {120, 30}))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        if (save_) {
            ImGui::OpenPopup(label("Save screenshot"));
            save_ = false;
            snprintf(saveInput_, sizeof(saveInput_), "%s", utf8(directory_ / "capture.png").c_str());
        }
        if (ImGui::BeginPopupModal(label("Save screenshot"), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text(tr("PNG file path"));
            ImGui::SetNextItemWidth(550);
            ImGui::InputText("##capture", saveInput_, sizeof(saveInput_));
            if (ImGui::Button(label("Save"), {130, 30})) {
                std::error_code ec;
                if (fs::exists(path(saveInput_), ec))
                    ImGui::OpenPopup(label("Replace this file?"));
                else {
                    if (!renderer_.save(path(saveInput_)))
                        error_ = "Impossible d'enregistrer la capture";
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(label("Cancel"), {100, 30}))
                ImGui::CloseCurrentPopup();
            if (ImGui::BeginPopupModal(label("Replace this file?"), nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::Text(tr("This file already exists."));
                if (ImGui::Button(label("Replace"))) {
                    if (!renderer_.save(path(saveInput_)))
                        error_ = "Impossible d'enregistrer la capture";
                    ImGui::CloseCurrentPopup();
                    saveInput_[0] = 0;
                }
                ImGui::SameLine();
                if (ImGui::Button(label("Cancel")))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            if (!saveInput_[0])
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

  public:
    App(GLFWwindow* window, Plugins& plugins, const fs::path& executable, const Localization& localization,
        bool persistLanguage = true)
        : window_(window), plugins_(plugins), loader_(plugins, [] { glfwPostEmptyEvent(); }),
          branding_(window), executable_(executable), localization_(localization),
          persist_language_(persistLanguage) {
        glfwSetWindowUserPointer(window_, this);
        glfwSetDropCallback(window_, [](GLFWwindow* w, int count, const char** files) {
            if (count)
                static_cast<App*>(glfwGetWindowUserPointer(w))->dropped_ = files[0];
        });
        scan(fs::current_path());
    }
    ~App() {
        if (scan_cancel_)
            *scan_cancel_ = true;
    }
    void run(const fs::path& initial, const fs::path& screenshot = {}, bool captureLoading = false,
             bool showUpdates = false) {
        updates_ = showUpdates;
        if (!initial.empty())
            open(initial);
        auto last = std::chrono::steady_clock::now();
        int warmup = 4;
        int screenshotFrames = 0;
        auto screenshotDeadline = last + std::chrono::seconds(300);
        int loadingCaptures = 0;
        while (!glfwWindowShouldClose(window_)) {
            if (scan_.valid() && scan_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                auto result = scan_.get();
                if (nextScan_.empty()) {
                    folder_ = std::move(result);
                    filter_dirty_ = true;
                    loader_.prefetch(neighbors(loading_ ? requested_ : current_));
                } else {
                    auto next = nextScan_;
                    scan(next);
                }
            }
            if (!dropped_.empty()) {
                auto file = std::move(dropped_);
                dropped_.clear();
                open(path(file));
            }
            if (auto result = loader_.take(); result && result->generation == generation_) {
                error_ = result->error;
                if (result->scene) {
                    upload_start_ = std::chrono::steady_clock::now();
                    try {
                        renderer_.beginUpload(result->scene);
                        staged_scene_ = std::move(result->scene);
                        staged_file_ = result->file;
                        staged_cached_ = result->cached;
                    } catch (const std::exception& e) {
                        error_ = e.what();
                        loading_ = false;
                    }
                } else
                    loading_ = false;
            }
            if (staged_scene_) {
                try {
                    if (renderer_.advanceUpload()) {
                        scene_ = std::move(staged_scene_);
                        current_ = std::move(staged_file_);
                        fromCache_ = staged_cached_;
                        loading_ = false;
                        uploadMs_ = std::chrono::duration<double, std::milli>(
                                        std::chrono::steady_clock::now() - upload_start_)
                                        .count();
                        glfwSetWindowTitle(window_, ("Cy3DView  ·  " + utf8(current_.filename())).c_str());
                    }
                } catch (const std::exception& e) {
                    error_ = e.what();
                    staged_scene_.reset();
                    loading_ = false;
                }
            }
            auto now = std::chrono::steady_clock::now();
            float dt = static_cast<float>(std::chrono::duration<double>(now - last).count());
            last = now;
            if (spin_)
                renderer_.camera.yaw += std::min(dt, .1f) * .4f;
            renderer_.tick(std::min<double>(dt, .1));
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            auto& io = ImGui::GetIO();
            bool navigating = false;
            if (!io.WantTextInput && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
                    chooseFile();
                if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
                    save_ = true;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
                    step(-1);
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
                    step(1);
                if (ImGui::IsKeyPressed(ImGuiKey_Home))
                    fit();
                if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl && !io.KeyAlt && !io.KeySuper)
                    renderer_.wire = !renderer_.wire;
                if (ImGui::IsKeyPressed(ImGuiKey_G))
                    renderer_.grid = !renderer_.grid;
                if (ImGui::IsKeyPressed(ImGuiKey_T))
                    renderer_.textured = !renderer_.textured;
                if (ImGui::IsKeyPressed(ImGuiKey_Space))
                    spin_ = !spin_;
                if (ImGui::IsKeyPressed(ImGuiKey_F11))
                    fullscreen();
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    if (loading_)
                        cancelLoad();
                    else if (fullscreen_)
                        fullscreen();
                }
            }
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {14, 10});
            ImGui::Begin("Cy3DView", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
            ImGui::Image(static_cast<ImTextureID>(branding_.texture()), {28, 28});
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored({.76f, .59f, .94f, 1}, "CY3DVIEW");
            ImGui::SameLine();
            ImGui::TextDisabled(tr("  /  3D viewer"));
            ImGui::SameLine(320);
            if (ImGui::Button(label("Open"), {85, 28}))
                chooseFile();
            ImGui::SameLine();
            if (ImGui::Button("<", {30, 28}))
                step(-1);
            ImGui::SameLine();
            if (ImGui::Button(">", {30, 28}))
                step(1);
            ImGui::SameLine();
            if (ImGui::Button(label("Frame"), {85, 28}))
                fit();
            ImGui::SameLine();
            if (ImGui::Button(label("Screenshot"), {100, 28}))
                save_ = true;
            ImGui::SameLine();
            if (ImGui::Button("Plugins", {75, 28}))
                about_ = true;
            ImGui::SameLine();
            if (ImGui::Button(label("Updates"), {0, 28}))
                updates_ = true;
            ImGui::SameLine();
            ImGui::Checkbox(label("Folder"), &explorer_);
            ImGui::SameLine();
            ImGui::Checkbox(label("Info"), &details_);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(128);
            if (ImGui::BeginCombo("##language",
                                  localization_.language == Language::French ? "Français" : "English")) {
                for (const auto language : {Language::English, Language::French}) {
                    if (ImGui::Selectable(language == Language::French ? "Français" : "English",
                                          language == localization_.language)) {
                        localization_.language = language;
                        if (persist_language_ &&
                            !localization_.save(executable_.parent_path() / "Cy3DView.ini"))
                            error_ = "Could not save language preference.";
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", tr("Language"));
            ImGui::Separator();
            float bottom = 48;
            float available = ImGui::GetContentRegionAvail().y - bottom;
            if (explorer_) {
                ImGui::BeginChild("explorer", {260, available}, true);
                explorer();
                ImGui::EndChild();
                ImGui::SameLine();
            }
            float width = std::max(100.f, ImGui::GetContentRegionAvail().x -
                                              (details_ ? 250 + ImGui::GetStyle().ItemSpacing.x : 0));
            ImGui::BeginChild("viewport", {width, available}, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            auto area = ImGui::GetContentRegionAvail();
            auto position = ImGui::GetCursorScreenPos();
            viewportW_ = std::max(1, static_cast<int>(area.x * io.DisplayFramebufferScale.x));
            viewportH_ = std::max(1, static_cast<int>(area.y * io.DisplayFramebufferScale.y));
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("orbit", area,
                                   ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle |
                                       ImGuiButtonFlags_MouseButtonRight);
            bool hovered = ImGui::IsItemHovered() && !(loading_ && io.MousePos.y < position.y + 94);
            if (hovered && io.MouseWheel != 0)
                renderer_.camera.zoom(io.MouseWheel);
            if (ImGui::IsItemActive()) {
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    renderer_.camera.yaw -= io.MouseDelta.x * .008f;
                    renderer_.camera.pitch =
                        std::clamp(renderer_.camera.pitch + io.MouseDelta.y * .008f, -1.55f, 1.55f);
                } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                           ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                    renderer_.camera.pan(io.MouseDelta.x, io.MouseDelta.y, std::max(area.y, 1.f));
            }
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                fit();
            const auto navigation = readNavigationInput(
                scene_ && !loading_, glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0, ImGui::IsItemActive());
            navigating = navigation.apply(renderer_.camera, std::min(dt, .1f));
            auto renderStart = std::chrono::steady_clock::now();
            viewport_ = renderer_.draw(viewportW_, viewportH_);
            frameMs_ =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - renderStart)
                    .count();
            auto draw = ImGui::GetWindowDrawList();
            draw->AddImage(static_cast<ImTextureID>(viewport_), position,
                           {position.x + area.x, position.y + area.y}, {0, 1}, {1, 0});
            if (!scene_ && !loading_) {
                float logoSize = std::min(104.f, area.y * .22f);
                ImVec2 logoPosition{position.x + (area.x - logoSize) / 2,
                                    position.y + area.y * .42f - logoSize - 24};
                draw->AddImage(static_cast<ImTextureID>(branding_.texture()), logoPosition,
                               {logoPosition.x + logoSize, logoPosition.y + logoSize});
                const char* title = tr("Your models. Every angle.");
                auto size = ImGui::CalcTextSize(title);
                draw->AddText({position.x + (area.x - size.x) / 2, position.y + area.y * .42f},
                              IM_COL32(209, 228, 230, 255), title);
                const char* subtitle = tr("Drop a 3D file here, or click Open");
                size = ImGui::CalcTextSize(subtitle);
                draw->AddText({position.x + (area.x - size.x) / 2, position.y + area.y * .42f + 30},
                              IM_COL32(125, 148, 157, 255), subtitle);
                auto demo = executable_.parent_path() / "samples" / "studio.obj";
                if (fs::exists(demo)) {
                    ImGui::SetCursorPos({std::max(8.f, (area.x - 180) / 2), area.y * .42f + 68});
                    if (ImGui::Button(label("Open demo"), {180, 32}))
                        open(demo);
                }
            }
            if (loading_) {
                const float right = position.x + area.x;
                const float top = position.y;
                draw->AddRectFilled(position, {right, top + 94}, IM_COL32(28, 22, 43, 250), 8);
                draw->AddRect(position, {right, top + 94}, IM_COL32(145, 93, 204, 200), 8, 0, 1.5f);
                const double elapsed = std::chrono::duration<double>(now - load_start_).count();
                const float phase = static_cast<float>(std::fmod(ImGui::GetTime() * 4.5, 6.2831853));
                ImVec2 center{position.x + 26, top + 28};
                draw->AddCircle(center, 10, IM_COL32(68, 48, 88, 255), 24, 3);
                draw->PathArcTo(center, 10, phase, phase + 4.6f, 24);
                draw->PathStroke(IM_COL32(199, 146, 255, 255), 0, 3);
                draw->PushClipRect({position.x + 48, top + 8}, {right - 118, top + 40}, true);
                draw->AddText({position.x + 48, top + 17}, IM_COL32(241, 233, 255, 255),
                              (std::string(tr("Opening ")) + utf8(requested_.filename())).c_str());
                draw->PopClipRect();
                auto extension = lower(utf8(requested_.extension()));
                std::string stage = staged_scene_            ? tr("Preparing display")
                                    : extension == ".blend"  ? tr("Converting and reading with Blender")
                                    : extension == ".uasset" ? tr("Converting and reading with Unreal")
                                                             : tr("Reading model");
                stage += "  -  " + std::to_string(static_cast<unsigned long long>(elapsed)) + " s";
                if (staged_scene_)
                    stage +=
                        "  -  " + std::to_string(static_cast<int>(renderer_.uploadProgress() * 100)) + " %";
                draw->AddText({position.x + 16, top + 47}, IM_COL32(191, 172, 211, 255), stage.c_str());
                const ImVec2 bar{position.x + 16, top + 73};
                const float barWidth = std::max(1.f, area.x - 32);
                draw->AddRectFilled(bar, {bar.x + barWidth, bar.y + 10}, IM_COL32(57, 42, 73, 255), 5);
                if (staged_scene_) {
                    float filled = barWidth * std::clamp(renderer_.uploadProgress(), 0.f, 1.f);
                    draw->AddRectFilled(bar, {bar.x + filled, bar.y + 10}, IM_COL32(165, 106, 232, 255), 5);
                    draw->PushClipRect(bar, {bar.x + filled, bar.y + 10}, true);
                    float offset = static_cast<float>(std::fmod(ImGui::GetTime() * 64, 24));
                    for (float x = bar.x - 24 + offset; x < bar.x + filled; x += 24)
                        draw->AddLine({x, bar.y + 10}, {x + 10, bar.y}, IM_COL32(224, 187, 255, 160), 4);
                    draw->PopClipRect();
                } else {
                    float length = barWidth * .25f;
                    float travel = static_cast<float>(std::fmod(ImGui::GetTime() * .65, 1));
                    float x = bar.x - length + (barWidth + length) * travel;
                    draw->PushClipRect(bar, {bar.x + barWidth, bar.y + 10}, true);
                    draw->AddRectFilled({x, bar.y}, {x + length, bar.y + 10}, IM_COL32(184, 125, 248, 255),
                                        5);
                    draw->PopClipRect();
                }
                ImGui::SetCursorPos({std::max(0.f, area.x - 110), 13});
                if (ImGui::Button(label("Cancel"), {96, 30}))
                    cancelLoad();
            }
            if (!error_.empty()) {
                ImGui::SetCursorPos({15, loading_ ? 110.f : 15.f});
                ImGui::PushStyleColor(ImGuiCol_Text, {1, .6f, .45f, 1});
                ImGui::TextWrapped("%s", localization_.diagnostic(error_).c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndChild();
            if (details_) {
                ImGui::SameLine();
                ImGui::BeginChild("details", {250, available}, true);
                details();
                ImGui::EndChild();
            }
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextWrapped("%s", tr("Drag: orbit  |  Right drag: pan  |  Wheel: zoom  |  WASD / ZQSD: "
                                        "move  |  Shift: faster  |  Arrows: model  |  F11: fullscreen"));
            ImGui::PopStyleColor();
            popups();
            ImGui::End();
            ImGui::PopStyleVar(2);
            ImGui::Render();
            int framebufferW, framebufferH;
            glfwGetFramebufferSize(window_, &framebufferW, &framebufferH);
            glViewport(0, 0, framebufferW, framebufferH);
            glClearColor(.05f, .06f, .07f, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            if (!screenshot.empty()) {
                if (captureLoading && loading_ && loadingCaptures < 2 &&
                    std::chrono::duration<double>(now - load_start_).count() >= .15 + .4 * loadingCaptures) {
                    auto capture = screenshot.parent_path() / (screenshot.stem().string() + "-loading-" +
                                                               std::to_string(++loadingCaptures) + ".png");
                    if (!Renderer::saveWindow(capture, framebufferW, framebufferH))
                        throw std::runtime_error("Capture de chargement impossible");
                }
                if (!error_.empty())
                    throw std::runtime_error(error_);
                if (!loading_ && !scan_.valid() && ++screenshotFrames >= 8) {
                    if (!Renderer::saveWindow(screenshot, framebufferW, framebufferH))
                        throw std::runtime_error("Capture UI impossible");
                    return;
                }
                if (now > screenshotDeadline)
                    throw std::runtime_error("Delai de capture UI depasse");
            }
            glfwSwapBuffers(window_);
            bool active = navigating || spin_ || ImGui::IsAnyItemActive() || io.WantTextInput || loading_ ||
                          scan_.valid() || updater_.snapshot().phase == UpdatePhase::Checking ||
                          updater_.snapshot().phase == UpdatePhase::Downloading;
            if (warmup > 0) {
                --warmup;
                glfwPollEvents();
            } else if (navigating || spin_ || staged_scene_ ||
                       (renderer_.playing && renderer_.animations() &&
                        !renderer_.animations()->clips.empty()))
                glfwPollEvents();
            else if (active || !screenshot.empty())
                glfwWaitEventsTimeout(1. / 30);
            else
                glfwWaitEvents();
        }
    }
};
void style() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.WindowRounding = 6;
    s.ChildRounding = 6;
    s.FrameRounding = 4;
    s.PopupRounding = 6;
    s.FramePadding = {9, 6};
    s.ItemSpacing = {8, 6};
    s.Colors[ImGuiCol_WindowBg] = {.055f, .071f, .083f, 1};
    s.Colors[ImGuiCol_ChildBg] = {.068f, .087f, .102f, 1};
    s.Colors[ImGuiCol_Border] = {.16f, .22f, .25f, 1};
    s.Colors[ImGuiCol_Button] = {.105f, .18f, .20f, 1};
    s.Colors[ImGuiCol_ButtonHovered] = {.13f, .32f, .32f, 1};
    s.Colors[ImGuiCol_ButtonActive] = {.12f, .43f, .40f, 1};
    s.Colors[ImGuiCol_Header] = {.08f, .31f, .29f, 1};
    s.Colors[ImGuiCol_HeaderHovered] = {.13f, .26f, .28f, 1};
    s.Colors[ImGuiCol_CheckMark] = {.2f, .85f, .77f, 1};
    s.Colors[ImGuiCol_Text] = {.83f, .89f, .91f, 1};
    s.Colors[ImGuiCol_TextDisabled] = {.43f, .55f, .60f, 1};
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    for (const auto* font :
         {"C:/Windows/Fonts/segoeui.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
          "/System/Library/Fonts/Supplemental/Arial.ttf"})
        if (fs::exists(font)) {
            io.Fonts->AddFontFromFileTTF(font, 16);
            break;
        }
}
} // namespace cy
int main(int argc, char** argv) {
    using namespace cy;
    std::vector<std::string> args;
#ifdef _WIN32
    int count = 0;
    auto wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (wide) {
        for (int i = 1; i < count; ++i)
            args.push_back(utf8(fs::path(wide[i])));
        LocalFree(wide);
    }
#else
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
#endif
    (void)argc;
    auto executable = executablePath(argv[0]);
    Localization localization;
    // Smoke runs are deterministic and never read or change personal preferences.
    bool diagnosticRun = std::any_of(args.begin(), args.end(), [](const auto& arg) {
        return arg == "--smoke" || arg == "--ui-smoke" || arg == "--loading-smoke" ||
               arg == "--updates-smoke";
    });
    if (!diagnosticRun)
        localization.load(executable.parent_path() / "Cy3DView.ini");
#ifdef _WIN32
    // Keep the installation mutex until process exit, including GPU/DLL cleanup.
    if (!diagnosticRun)
        CreateMutexW(nullptr, FALSE, L"Local\\Cy3DView.App");
#endif
    for (size_t i = 0; i < args.size();) {
        if (args[i] != "--language") {
            ++i;
            continue;
        }
        if (i + 1 >= args.size() || (args[i + 1] != "en" && args[i + 1] != "fr")) {
            std::cerr << "--language en|fr\n";
            return 1;
        }
        localization.language = args[i + 1] == "fr" ? Language::French : Language::English;
        args.erase(args.begin() + i, args.begin() + i + 2);
    }
    Plugins plugins(executable.parent_path() / "plugins");
    bool smoke = !args.empty() && args[0] == "--smoke",
         loadingSmoke = !args.empty() && args[0] == "--loading-smoke";
    bool updatesSmoke = !args.empty() && args[0] == "--updates-smoke";
    bool uiSmoke = !args.empty() && (args[0] == "--ui-smoke" || loadingSmoke || updatesSmoke);
    glfwSetErrorCallback([](int, const char* error) { std::cerr << error << '\n'; });
    if (!glfwInit())
        return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    if (smoke || uiSmoke)
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window = glfwCreateWindow(1440, 900, "Cy3DView", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwSetWindowSizeLimits(window, 1100, 650, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    if (!gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    int status = 0;
    try {
        if (smoke) {
            if (args.size() != 3)
                throw std::runtime_error("--smoke modele sortie.png");
            std::atomic<bool> cancel{false};
            std::atomic<float> progress{0};
            auto scene = importScene(plugins, path(args[1]), cancel, progress);
            Renderer renderer;
            renderer.upload(scene);
            renderer.draw(1100, 760);
            glFinish();
            if (glGetError() != GL_NO_ERROR)
                throw std::runtime_error("Erreur GPU pendant le rendu");
            if (!renderer.save(path(args[2])))
                throw std::runtime_error("Capture impossible");
            std::cout << "GPU smoke OK: " << glGetString(GL_RENDERER) << '\n';
        } else {
            ImGui::CreateContext();
            style();
            ImGui_ImplGlfw_InitForOpenGL(window, true);
            ImGui_ImplOpenGL3_Init("#version 330 core");
            if (uiSmoke && args.size() != 3)
                throw std::runtime_error("--ui-smoke modele sortie.png");
            {
                App app(window, plugins, executable, localization, !diagnosticRun);
                app.run(args.empty() ? fs::path{} : path(args[uiSmoke ? 1 : 0]),
                        uiSmoke ? path(args[2]) : fs::path{}, loadingSmoke, updatesSmoke);
            }
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        status = 1;
#ifdef _WIN32
        if (!smoke && !uiSmoke) {
            auto message = path(localization.diagnostic(e.what())).wstring();
            MessageBoxW(nullptr, message.c_str(), L"Cy3DView", MB_ICONERROR);
        }
#endif
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return status;
}

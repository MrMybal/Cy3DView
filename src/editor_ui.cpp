// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "editor_ui.h"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif
namespace cy {
namespace {
void uvPreview(void*, const Cy3DScene* scene, uint32_t channel) {
    float size = std::min(340.f, ImGui::GetContentRegionAvail().x);
    size = std::max(80.f, size);
    auto pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##uv-preview", {size, size});
    auto* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(pos, {pos.x + size, pos.y + size}, IM_COL32(15, 18, 25, 255), 4);
    d->PushClipRect(pos, {pos.x + size, pos.y + size}, true);
    uint32_t triangles = 0;
    for (uint32_t i = 0; i < scene->mesh_count && triangles < 20000; ++i) {
        const auto& m = scene->meshes[i];
        if (m.topology != CY3D_TRIANGLES)
            continue;
        auto point = [&](uint32_t v) {
            const auto* uv = channel ? m.vertices[v].uv1 : m.vertices[v].uv;
            return ImVec2{pos.x + 8 + uv[0] * (size - 16), pos.y + 8 + (1 - uv[1]) * (size - 16)};
        };
        auto color = IM_COL32(130 + (i * 31) % 100, 90 + (i * 19) % 130, 210, 150);
        for (uint32_t j = 0; j + 2 < m.index_count && triangles < 20000; j += 3, ++triangles) {
            auto a = point(m.indices[j]), b = point(m.indices[j + 1]), c = point(m.indices[j + 2]);
            if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y) ||
                !std::isfinite(c.x) || !std::isfinite(c.y))
                continue;
            d->AddTriangleFilled(a, b, c, IM_COL32(145, 100, 210, 35));
            d->AddTriangle(a, b, c, color);
        }
    }
    d->PopClipRect();
    d->AddRect(pos, {pos.x + size, pos.y + size}, IM_COL32(120, 110, 145, 255));
}
Cy3DToolUI toolUI(Language language) {
    return {sizeof(Cy3DToolUI),
            language == Language::French ? 1u : 0u,
            nullptr,
            [](void*, const char* s) { ImGui::TextWrapped("%s", s); },
            [](void*, const char* label, int* value, int low, int high) {
                ImGui::SliderInt(label, value, low, high);
            },
            [](void*, const char* label, float* value, float low, float high) {
                ImGui::SliderFloat(label, value, low, high);
            },
            [](void*, const char* label, int* value) {
                bool b = *value != 0;
                if (ImGui::Checkbox(label, &b))
                    *value = b ? 1 : 0;
            },
            [](void*, const char* label, int* value, const char* const* choices, uint32_t count) {
                if (!count)
                    return;
                *value = std::clamp(*value, 0, static_cast<int>(count) - 1);
                if (ImGui::BeginCombo(label, choices[*value])) {
                    for (uint32_t i = 0; i < count; ++i)
                        if (ImGui::Selectable(choices[i], *value == static_cast<int>(i)))
                            *value = static_cast<int>(i);
                    ImGui::EndCombo();
                }
            },
            uvPreview};
}
} // namespace
EditorUI::~EditorUI() {
    if (control_)
        control_->cancel = true;
    if (job_.valid())
        job_.wait();
}
void EditorUI::reset() {
    ++generation_;
    if (control_)
        control_->cancel = true;
    edited_.reset();
    expected_.reset();
    undo_.reset();
    bundle_.reset();
    dirty_ = false;
    message_.clear();
    error_.clear();
}
std::shared_ptr<Scene> EditorUI::take(const std::shared_ptr<Scene>& current) {
    if (job_.valid() && job_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        auto result = job_.get();
        if (result.generation == generation_ && result.source == current) {
            error_ = std::move(result.error);
            if (result.edited) {
                edited_ = std::move(result.edited);
                expected_ = current;
                undo_ = current;
                undo_dirty_ = dirty_;
                dirty_ = true;
                message_.clear();
            }
            if (result.bundle) {
                bundle_ = std::move(result.bundle);
                try {
                    if (bundle_->conflicts().empty())
                        finishExport(false);
                } catch (const std::exception& e) {
                    error_ = e.what();
                    bundle_.reset();
                }
            }
        }
    }
    if (edited_ && expected_ == current) {
        expected_.reset();
        return std::exchange(edited_, {});
    }
    return {};
}
void EditorUI::selectTool(int i) {
    try {
        const auto& tool = extensions_.tools().at(static_cast<size_t>(i));
        auto next = std::make_shared<ToolState>();
        next->module = std::make_shared<ExtensionModule>(tool.library);
        bool found = false;
        for (uint32_t t = 0; t < next->module->api->tool_count; ++t)
            if (next->module->api->tools[t].id && tool.id == next->module->api->tools[t].id)
                found = true;
        if (!found)
            throw std::runtime_error("Tool is not declared by the loaded extension.");
        next->data = next->module->api->create_tool(tool.id.c_str());
        if (!next->data)
            throw std::runtime_error("Tool initialization failed.");
        state_ = std::move(next);
        tool_ = i;
        tool_open_ = true;
        error_.clear();
    } catch (const std::exception& e) {
        error_ = e.what();
    }
}
void EditorUI::beginExport(const std::shared_ptr<Scene>& source, const fs::path& sourceFile) {
    auto action = extensions_.formats().at(static_cast<size_t>(format_));
    auto output = path(output_);
    if (extension(output) != action.extension)
        output.replace_extension("." + action.extension);
    control_ = std::make_shared<OperationControl>();
    error_.clear();
    message_.clear();
    auto control = control_;
    auto generation = generation_;
    auto wake = wake_;
    job_ = std::async(std::launch::async, [source, sourceFile, action, output, control, generation, wake] {
        JobResult result;
        result.generation = generation;
        result.source = source;
        try {
            result.bundle = exportScene(action, source, output, sourceFile, *control);
        } catch (const std::exception& e) {
            result.error = e.what();
        }
        wake();
        return result;
    });
}
void EditorUI::beginTool(const std::shared_ptr<Scene>& source) {
    auto action = extensions_.tools().at(static_cast<size_t>(tool_));
    auto state = state_;
    control_ = std::make_shared<OperationControl>();
    error_.clear();
    message_.clear();
    auto control = control_;
    auto generation = generation_;
    auto wake = wake_;
    job_ = std::async(std::launch::async, [source, action, state, control, generation, wake] {
        JobResult result;
        result.generation = generation;
        result.source = source;
        try {
            result.edited = runTool(state->module, action, state->data, source, *control);
        } catch (const std::exception& e) {
            result.error = e.what();
        }
        wake();
        return result;
    });
}
void EditorUI::finishExport(bool replace) {
    try {
        bundle_->commit(replace);
        message_ = utf8(bundle_->destination);
        bundle_.reset();
    } catch (const std::exception& e) {
        error_ = e.what();
        bundle_.reset();
    }
}
void EditorUI::toolbar(const std::shared_ptr<Scene>& scene, const fs::path& file, bool blocked,
                       Language language) {
    bool fr = language == Language::French;
    auto t = [&](const char* en, const char* french) { return fr ? french : en; };
    ImGui::BeginDisabled(!scene || blocked || busy() || bundle_);
    if (ImGui::Button(t("Export###export", "Exporter###export"), {0, 28})) {
        export_popup_ = true;
        auto target = file.parent_path() / (utf8(file.stem()) + "_export.glb");
        std::snprintf(output_, sizeof(output_), "%s", utf8(target).c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button(t("Tools###tools", "Outils###tools"), {0, 28}))
        ImGui::OpenPopup("extension-tools");
    if (ImGui::BeginPopup("extension-tools")) {
        for (size_t i = 0; i < extensions_.tools().size(); ++i) {
            const auto& a = extensions_.tools()[i];
            if (ImGui::MenuItem((fr ? a.name_fr : a.name).c_str()))
                selectTool(static_cast<int>(i));
        }
        if (extensions_.tools().empty())
            ImGui::TextDisabled("%s", t("No tool plugins", "Aucun plugin d'outil"));
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!undo_);
    if (ImGui::Button(t("Undo###undo", "Annuler###undo"), {0, 28})) {
        edited_ = std::exchange(undo_, {});
        expected_ = scene;
        dirty_ = undo_dirty_;
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (dirty_) {
        ImGui::SameLine();
        ImGui::TextColored({.8f, .65f, 1, 1}, "%s", t("Modified copy", "Copie modifiée"));
    }
}
void EditorUI::draw(const std::shared_ptr<Scene>& scene, const fs::path& file, bool blocked,
                    Language language, void* nativeWindow) {
    bool fr = language == Language::French;
    auto t = [&](const char* en, const char* french) { return fr ? french : en; };
    if (busy()) {
        ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x * .5f, 110}, ImGuiCond_Always, {.5f, 0});
        ImGui::SetNextWindowSize({420, 0});
        ImGui::Begin(t("Working###operation", "Traitement###operation"), nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings);
        float f = control_->progress;
        ImGui::ProgressBar(f, {300, 18});
        ImGui::SameLine();
        if (ImGui::Button(t("Cancel", "Annuler")))
            control_->cancel = true;
        auto p = ImGui::GetCursorScreenPos();
        auto* d = ImGui::GetWindowDrawList();
        float x = p.x + static_cast<float>(std::fmod(ImGui::GetTime() * 120, 360));
        d->AddRectFilled({x, p.y}, {x + 30, p.y + 3}, IM_COL32(190, 130, 250, 255));
        ImGui::Dummy({380, 6});
        ImGui::End();
    }
    if (export_popup_) {
        ImGui::OpenPopup("Export###export-dialog");
        export_popup_ = false;
    }
    ImGui::SetNextWindowSize({620, 0}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(t("Export###export-dialog", "Exporter###export-dialog"), nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", t("Export the scene loaded in memory. The source file is preserved.",
                                   "Exporte la scène chargée en mémoire. Le fichier source est conservé."));
        if (extensions_.formats().empty())
            ImGui::TextWrapped("%s", t("No exporter plugins installed.", "Aucun plugin d'export installé."));
        else {
            const auto& selected = extensions_.formats()[static_cast<size_t>(format_)];
            ImGui::SetNextItemWidth(570);
            if (ImGui::BeginCombo(t("Format", "Format"), (fr ? selected.name_fr : selected.name).c_str())) {
                for (size_t i = 0; i < extensions_.formats().size(); ++i)
                    if (ImGui::Selectable(
                            (fr ? extensions_.formats()[i].name_fr : extensions_.formats()[i].name).c_str(),
                            format_ == static_cast<int>(i))) {
                        format_ = static_cast<int>(i);
                        auto target = path(output_);
                        target.replace_extension("." + extensions_.formats()[i].extension);
                        std::snprintf(output_, sizeof(output_), "%s", utf8(target).c_str());
                    }
                ImGui::EndCombo();
            }
            auto flags = extensions_.formats()[static_cast<size_t>(format_)].capabilities;
            if (!(flags & CY3D_EXPORT_MATERIALS))
                ImGui::TextWrapped("%s", t("This format exports geometry without materials or textures.",
                                           "Ce format exporte la géométrie sans matériaux ni textures."));
            else if (!(flags & CY3D_EXPORT_PBR))
                ImGui::TextWrapped("%s", t("Materials are simplified. Metallic, roughness and occlusion maps "
                                           "are omitted; use GLB for PBR.",
                                           "Les matériaux sont simplifiés. Les textures métal, rugosité et "
                                           "occlusion sont omises ; utiliser GLB pour le PBR."));
            if (!(flags & CY3D_EXPORT_ANIMATION))
                ImGui::TextWrapped(
                    "%s", t("Animations and skeletons are omitted; the model uses its rest pose.",
                            "Les animations et squelettes sont omis ; le modèle utilise sa pose de repos."));
            if (!(flags & CY3D_EXPORT_UV))
                ImGui::TextWrapped("%s", t("UV coordinates are omitted.", "Les coordonnées UV sont omises."));
            if (extensions_.formats()[static_cast<size_t>(format_)].extension == "ply")
                ImGui::TextWrapped(
                    "%s", t("PLY preserves UV0. UV1 is omitted.", "Le PLY conserve UV0. UV1 est omis."));
            if (extensions_.formats()[static_cast<size_t>(format_)].extension == "fbx")
                ImGui::TextWrapped(
                    "%s", t("FBX export is experimental. STEP interpolation becomes linear; verify "
                            "animations and material appearance in the destination software.",
                            "L'export FBX est expérimental. L'interpolation STEP devient linéaire ; vérifier "
                            "les animations et l'apparence des matériaux dans le logiciel de destination."));
            if (scene && !scene->warnings.empty()) {
                ImGui::TextColored({1, .65f, .4f, 1}, "%s",
                                   t("Import warnings also apply to the exported copy:",
                                     "Les avertissements d'import concernent aussi la copie exportée :"));
                ImGui::BeginChild("export-warnings", {570, 90}, true);
                for (const auto& warning : scene->warnings)
                    ImGui::TextWrapped("%s", warning.c_str());
                ImGui::EndChild();
            }
            ImGui::SetNextItemWidth(570);
            ImGui::InputText("##output-path", output_, sizeof(output_));
#ifdef _WIN32
            if (ImGui::Button(t("Choose file...", "Choisir le fichier..."))) {
                auto value = path(output_).wstring();
                wchar_t filename[32768]{};
                std::copy_n(value.c_str(), std::min(value.size(), size_t(32767)), filename);
                std::wstring filter = path(selected.name).wstring();
                filter.push_back(0);
                filter += L"*." + path(selected.extension).wstring();
                filter.push_back(0);
                filter.push_back(0);
                OPENFILENAMEW d{};
                d.lStructSize = sizeof(d);
                d.hwndOwner = static_cast<HWND>(nativeWindow);
                d.lpstrFile = filename;
                d.nMaxFile = 32768;
                d.lpstrFilter = filter.c_str();
                d.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetSaveFileNameW(&d))
                    std::snprintf(output_, sizeof(output_), "%s", utf8(fs::path(filename)).c_str());
            }
#else
            (void)nativeWindow;
#endif
            ImGui::BeginDisabled(!scene || blocked || busy());
            if (ImGui::Button(t("Export", "Exporter"), {140, 30})) {
                beginExport(scene, file);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
        }
        if (ImGui::Button(t("Close", "Fermer"), {100, 30}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (tool_open_ && state_) {
        const auto& action = extensions_.tools()[static_cast<size_t>(tool_)];
        auto title = (fr ? action.name_fr : action.name) + "###tool-editor";
        ImGui::SetNextWindowSize({400, 600}, ImGuiCond_Appearing);
        ImGui::SetNextWindowPos({ImGui::GetIO().DisplaySize.x - 425, 120}, ImGuiCond_Appearing);
        if (ImGui::Begin(title.c_str(), &tool_open_, ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::BeginDisabled(busy() || blocked || !scene);
            if (!busy()) {
                auto ui = toolUI(language);
                state_->module->api->draw_tool(action.id.c_str(), state_->data, &ui,
                                               scene ? scene->data : nullptr);
            } else
                ImGui::TextWrapped("%s", t("Calculating UV atlas...", "Calcul de l'atlas UV..."));
            if (ImGui::Button(t("Apply to copy", "Appliquer à la copie"), {220, 32}))
                beginTool(scene);
            ImGui::EndDisabled();
        }
        ImGui::End();
    }
    if (bundle_ && !ImGui::IsPopupOpen("Replace files###export-conflicts"))
        ImGui::OpenPopup("Replace files###export-conflicts");
    if (ImGui::BeginPopupModal(
            t("Replace files###export-conflicts", "Remplacer les fichiers###export-conflicts"), nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", t("These output files already exist. Replace them?",
                                   "Ces fichiers de sortie existent déjà. Les remplacer ?"));
        ImGui::BeginChild("conflicts", {560, 180}, true);
        try {
            for (const auto& f : bundle_->conflicts())
                ImGui::TextWrapped("%s", utf8(f).c_str());
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        ImGui::EndChild();
        if (ImGui::Button(t("Replace", "Remplacer"), {140, 30})) {
            finishExport(true);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(t("Cancel", "Annuler"), {140, 30})) {
            bundle_.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!message_.empty() || !error_.empty()) {
        ImGui::SetNextWindowSize({540, 0}, ImGuiCond_Appearing);
        bool open = true;
        if (ImGui::Begin(t("Export and tools###operation-result", "Export et outils###operation-result"),
                         &open, ImGuiWindowFlags_NoSavedSettings)) {
            if (!error_.empty())
                ImGui::TextWrapped("%s", error_.c_str());
            else {
                ImGui::TextWrapped("%s", t("Export completed:", "Export terminé :"));
                ImGui::TextWrapped("%s", message_.c_str());
            }
            if (ImGui::Button(t("Close", "Fermer")))
                open = false;
        }
        ImGui::End();
        if (!open) {
            message_.clear();
            error_.clear();
        }
    }
}
} // namespace cy

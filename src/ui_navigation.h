// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "navigation.h"
#include <imgui.h>

namespace cy {
inline NavigationInput readNavigationInput(bool sceneReady, bool focused, bool viewportActive) {
    const auto& io = ImGui::GetIO();
    NavigationInput input;
    input.w = ImGui::IsKeyDown(ImGuiKey_W);
    input.a = ImGui::IsKeyDown(ImGuiKey_A);
    input.s = ImGui::IsKeyDown(ImGuiKey_S);
    input.d = ImGui::IsKeyDown(ImGuiKey_D);
    input.z = ImGui::IsKeyDown(ImGuiKey_Z);
    input.q = ImGui::IsKeyDown(ImGuiKey_Q);
    input.boost = io.KeyShift;
    input.focused = focused;
    input.blocked = !sceneReady || io.WantTextInput || io.KeyCtrl || io.KeyAlt || io.KeySuper ||
                    (ImGui::IsAnyItemActive() && !viewportActive) ||
                    ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    return input;
}
} // namespace cy

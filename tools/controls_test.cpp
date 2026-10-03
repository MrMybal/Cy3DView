// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "localization.h"
#include "ui_navigation.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <stdexcept>
#include <vector>
using namespace cy;
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
bool close(Vec3 a, Vec3 b) {
    return length(a - b) < 2e-4f;
}
Camera camera() {
    Camera result;
    result.yaw = result.pitch = 0;
    result.distance = 10;
    return result;
}
void movement() {
    NavigationInput forward;
    forward.w = true;
    auto a = camera(), b = camera();
    const auto initialEye = a.eye();
    require(forward.apply(a, 1), "Held W must request continuous rendering");
    require(close(a.target, {0, 0, -1.5f}), "W must move toward camera forward");
    require(close(a.eye() - initialEye, a.target), "Eye and target must move together");
    NavigationInput azerty;
    azerty.z = true;
    azerty.apply(b, 1);
    require(close(a.target, b.target), "W and Z must behave identically");
    forward.z = true;
    b = camera();
    forward.apply(b, 1);
    require(close(a.target, b.target), "Alias keys must not double movement speed");
    NavigationInput left;
    left.a = true;
    a = camera();
    left.apply(a, 1);
    left.a = false;
    left.q = true;
    b = camera();
    left.apply(b, 1);
    require(close(a.target, {-1.5f, 0, 0}) && close(a.target, b.target), "A and Q must move left");
    forward.s = true;
    require(!forward.apply(b, 1), "Opposite keys must cancel and stop continuous rendering");
    forward.s = false;
    forward.d = true;
    b = camera();
    forward.apply(b, 1);
    require(std::abs(length(b.target) - 1.5f) < 1e-5f, "Diagonal movement must be normalized");
    forward.d = false;
    forward.boost = true;
    b = camera();
    forward.apply(b, 1);
    require(close(b.target, {0, 0, -6}), "Shift must accelerate by four");
    forward.boost = false;
    a = camera();
    b = camera();
    for (int frame = 0; frame < 60; ++frame)
        forward.apply(a, 1.f / 60);
    for (int frame = 0; frame < 144; ++frame)
        forward.apply(b, 1.f / 144);
    require(close(a.target, b.target), "Movement must be independent of frame rate");
    b = camera();
    b.yaw = 1.5707963268f;
    forward.apply(b, 1);
    require(close(b.target, {-1.5f, 0, 0}), "Movement must follow camera yaw");
    b = camera();
    b.pitch = .5f;
    forward.apply(b, 1);
    require(b.target.y < 0, "Forward must follow camera pitch");
    forward.focused = false;
    require(!forward.apply(b, 1), "Unfocused window must not move");
    forward.focused = true;
    forward.blocked = true;
    require(!forward.apply(b, 1), "UI input must block movement");
    require(a.nearPlane() < .01f, "Navigating inside models needs a close clipping plane");
    const float minimum[]{-1, -1, -1}, maximum[]{1, 1, 1};
    a.fit(minimum, maximum);
    require(close(a.target, {}) && !a.navigated, "Home must restore original framing");
    require(!NavigationInput{}.apply(a, 1), "Released keys must stop continuous rendering");
}
std::vector<std::string> formats(const char* text) {
    static const std::regex format("%[-+ #0]*[0-9]*(?:\\.[0-9]+)?(?:ll|l|z)?[a-zA-Z%]");
    std::string value(text);
    std::vector<std::string> result;
    for (auto it = std::sregex_iterator(value.begin(), value.end(), format); it != std::sregex_iterator();
         ++it)
        result.push_back(it->str());
    return result;
}
void languages(const std::filesystem::path& directory) {
    Localization language;
    struct Pair {
        const char *english, *french;
    };
    const Pair catalog[]{
#include "translations.inc"
    };
    std::set<std::string> englishKeys;
    for (const auto& entry : catalog) {
        require(englishKeys.insert(entry.english).second, "Duplicate translation key");
        require(formats(entry.english) == formats(entry.french), "Translation changes printf placeholders");
        language.language = Language::English;
        require(std::strcmp(language.text(entry.english), entry.english) == 0,
                "English source must remain intact");
        const auto id = ImGui::GetID(language.label(entry.english));
        language.language = Language::French;
        require(std::strcmp(language.text(entry.english), entry.french) == 0,
                "French catalog must be complete");
        require(ImGui::GetID(language.label(entry.english)) == id,
                "Changing language must preserve widget IDs");
    }
    const auto settings = directory / "controls-settings.ini";
    language.load(settings.parent_path() / "controls-no-settings.ini");
    require(language.language == Language::English, "First launch must default to English");
    language.language = Language::French;
    require(language.save(settings), "Cannot save French preference");
    Localization restored;
    restored.load(settings);
    require(restored.language == Language::French, "Language preference must survive restart");
    {
        std::ofstream file(settings);
        file << "[Cy3DView]\r\nlanguage=unknown\r\n";
    }
    restored.load(settings);
    require(restored.language == Language::English, "Unknown language must fall back to English");
    language.language = Language::English;
    require(language.diagnostic("Format non pris en charge : .abc") == "Unsupported format: .abc",
            "Error translation must preserve extensions");
    require(language.diagnostic("Texture absente, non lisible ou trop grande : D:/Modèles/a.png") ==
                "Texture missing, unreadable or too large: D:/Modèles/a.png",
            "Do not translate asset paths");
    require(language.diagnostic("Conversion Blender echouee. Journal : D:/logs/a.log") ==
                "Blender conversion failed. Log: D:/logs/a.log",
            "Conversion errors must preserve log paths");
    language.language = Language::French;
    require(language.diagnostic("Unexpected third-party diagnostic") == "Unexpected third-party diagnostic",
            "Unknown plugin messages must remain verbatim");
    require(language.text("Custom plugin") == std::string("Custom plugin"),
            "Unknown labels must have a fallback");
    std::filesystem::remove(settings);
}
char search[64]{};
NavigationInput frame(bool focusText = false, bool modal = false, bool closeModal = false,
                      bool sceneReady = true, bool focused = true) {
    ImGui::NewFrame();
    ImGui::Begin("controls");
    if (focusText)
        ImGui::SetKeyboardFocusHere();
    ImGui::InputText("Search", search, sizeof(search));
    if (modal)
        ImGui::OpenPopup("dialog");
    if (ImGui::BeginPopupModal("dialog")) {
        ImGui::TextUnformatted("Dialog");
        if (closeModal)
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::InvisibleButton("viewport", {200, 120});
    auto input = readNavigationInput(sceneReady, focused, ImGui::IsItemActive());
    ImGui::End();
    ImGui::Render();
    return input;
}
void inputProtection() {
    auto& io = ImGui::GetIO();
    frame();
    io.AddKeyEvent(ImGuiKey_W, true);
    auto input = frame();
    require(input.direction().z == 1, "ImGui W event must move forward");
    require(frame().direction().z == 1, "Held key must remain active next frame");
    io.AddKeyEvent(ImGuiKey_W, false);
    frame();
    io.AddKeyEvent(ImGuiKey_Z, true);
    io.AddKeyEvent(ImGuiKey_Q, true);
    require(close(frame().direction(), {-1, 0, 1}), "ImGui Z/Q events must support AZERTY");
    io.AddKeyEvent(ImGuiKey_Z, false);
    io.AddKeyEvent(ImGuiKey_Q, false);
    frame();
    io.AddKeyEvent(ImGuiKey_W, true);
    frame();
    require(frame(false, false, false, false).direction().z == 0,
            "No model/loading must block keyboard movement");
    require(frame(false, false, false, true, false).direction().z == 0,
            "Losing window focus must block held keys");
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    require(frame().direction().z == 0, "Ctrl shortcuts must not move the camera");
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    require(frame(false, true).direction().z == 0, "Open modal must block held keys");
    frame(false, false, true);
    frame(true);
    frame();
    io.AddInputCharactersUTF8("wasdzq");
    input = frame();
    require(input.direction().z == 0 && std::strstr(search, "wasdzq"),
            "Typing movement keys must edit text without moving");
    io.AddKeyEvent(ImGuiKey_W, false);
    require(frame().direction().z == 0, "Releasing W must stop movement");
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1100, 650};
    io.IniFilename = nullptr;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    int status = 0;
    try {
        movement();
        inputProtection();
        ImGui::NewFrame();
        ImGui::Begin("languages");
        languages(std::filesystem::path(argv[1]));
        ImGui::End();
        ImGui::Render();
        std::cout << "Camera navigation, UI input protection and language preferences OK\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        status = 1;
    }
    ImGui::DestroyContext();
    return status;
}

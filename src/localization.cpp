// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "localization.h"
#include <fstream>
#include <unordered_map>

namespace cy {
namespace {
struct Translation {
    const char *english, *french;
    std::string englishLabel, frenchLabel;
    Translation(const char* en, const char* fr)
        : english(en), french(fr), englishLabel(std::string(en) + "###" + en),
          frenchLabel(std::string(fr) + "###" + en) {}
};
const Translation catalog[]{
#include "translations.inc"
};
const auto& dictionary() {
    static const auto lookup = [] {
        std::unordered_map<std::string_view, const Translation*> result;
        for (const auto& item : catalog) {
            result.emplace(item.english, &item);
            result.emplace(item.french, &item);
        }
        return result;
    }();
    return lookup;
}
} // namespace
const char* Localization::text(const char* english) const {
    auto item = dictionary().find(english);
    if (item == dictionary().end())
        return english;
    return language == Language::French ? item->second->french : item->second->english;
}
const char* Localization::label(const char* english) const {
    auto item = dictionary().find(english);
    if (item == dictionary().end())
        return english;
    return language == Language::French ? item->second->frenchLabel.c_str()
                                        : item->second->englishLabel.c_str();
}
std::string Localization::diagnostic(std::string_view message) const {
    if (auto found = dictionary().find(message); found != dictionary().end())
        return text(found->second->english);
    // Only translate known wrappers: filenames and third-party diagnostics stay intact.
    for (const auto* prefix :
         {"Impossible de lire ce dossier : ", "Texture absente, non lisible ou trop grande : ",
          "Plugin indisponible ou ABI incompatible : ", "Format non pris en charge : ."})
        if (message.starts_with(prefix))
            return std::string(text(prefix)) + std::string(message.substr(std::string_view(prefix).size()));
    constexpr std::string_view log = ". Journal : ";
    if (auto at = message.find(log); at != std::string_view::npos)
        return diagnostic(message.substr(0, at)) + text(". Log: ") +
               std::string(message.substr(at + log.size()));
    constexpr std::string_view executable = " ne pointe pas vers un executable";
    if (message.ends_with(executable))
        return std::string(message.substr(0, message.size() - executable.size())) +
               text(" does not point to an executable");
    return std::string(message);
}
void Localization::load(const std::filesystem::path& file) {
    language = Language::English;
    std::ifstream input(file);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line == "language=fr")
            language = Language::French;
        else if (line.starts_with("language="))
            language = Language::English;
    }
}
bool Localization::save(const std::filesystem::path& file) const {
    std::ofstream output(file, std::ios::trunc);
    output << "[Cy3DView]\nlanguage=" << (language == Language::French ? "fr" : "en") << '\n';
    output.close();
    return !output.fail();
}
} // namespace cy

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include <filesystem>
#include <string>
#include <algorithm>
#include <cctype>
#include <string_view>
namespace cy {
namespace fs = std::filesystem;
inline std::string utf8(const fs::path& p) {
    auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
inline fs::path path(const std::string& s) {
    return fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}
inline std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
inline std::string extension(const fs::path& p) {
    auto s = lower(utf8(p.extension()));
    return s.empty() ? s : s.substr(1);
}
inline bool naturalLessFolded(std::string_view x, std::string_view y) {
    size_t i = 0, j = 0;
    while (i < x.size() && j < y.size()) {
        if (std::isdigit(static_cast<unsigned char>(x[i])) &&
            std::isdigit(static_cast<unsigned char>(y[j]))) {
            size_t e = i, f = j;
            while (e < x.size() && std::isdigit(static_cast<unsigned char>(x[e])))
                ++e;
            while (f < y.size() && std::isdigit(static_cast<unsigned char>(y[f])))
                ++f;
            size_t u = i, v = j;
            while (u < e && x[u] == '0')
                ++u;
            while (v < f && y[v] == '0')
                ++v;
            if (e - u != f - v)
                return e - u < f - v;
            auto c = x.compare(u, e - u, y, v, f - v);
            if (c)
                return c < 0;
            if (e - i != f - j)
                return e - i < f - j;
            i = e;
            j = f;
        } else {
            if (x[i] != y[j])
                return x[i] < y[j];
            ++i;
            ++j;
        }
    }
    return i == x.size() && j != y.size();
}
inline bool naturalLess(const fs::path& a, const fs::path& b) {
    auto x = lower(utf8(a.filename())), y = lower(utf8(b.filename()));
    return x == y ? utf8(a) < utf8(b) : naturalLessFolded(x, y);
}
} // namespace cy

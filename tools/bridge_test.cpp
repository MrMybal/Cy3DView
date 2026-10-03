// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "loader.h"
#include <iostream>
#include <stdexcept>
using namespace cy;
int main(int argc, char** argv) {
    try {
        if (argc < 3)
            return 1;
        Plugins plugins(fs::absolute(path(argv[1])));
        auto file = fs::absolute(path(argv[2]));
        std::atomic<bool> cancel = false;
        std::atomic<float> progress = 0;
        std::vector<fs::path> contentsBefore, contentsAfter;
        auto content = file.parent_path();
        while (content.filename() != "Content" && content != content.parent_path())
            content = content.parent_path();
        if (content.filename() == "Content")
            for (const auto& entry : fs::recursive_directory_iterator(content))
                contentsBefore.push_back(entry.path());
        auto before = fs::last_write_time(file);
        auto size = fs::file_size(file);
        auto first = importScene(plugins, file, cancel, progress);
        auto second = importScene(plugins, file, cancel, progress);
        if (content.filename() == "Content") {
            for (const auto& entry : fs::recursive_directory_iterator(content))
                contentsAfter.push_back(entry.path());
            std::sort(contentsBefore.begin(), contentsBefore.end());
            std::sort(contentsAfter.begin(), contentsAfter.end());
            if (contentsBefore != contentsAfter)
                throw std::runtime_error("Source Content folders modified");
        }
        if (!first->triangles || first->triangles != second->triangles || first->dependencies.size() < 3 ||
            second->import_ms > 2000)
            throw std::runtime_error("Bridge geometry, dependencies or converted cache missing");
        if (fs::last_write_time(file) != before || fs::file_size(file) != size)
            throw std::runtime_error("Input file modified");
        if (file.extension() == ".blend" &&
            (!first->animation || first->animation->clips.empty() || !first->data->meshes[0].skin))
            throw std::runtime_error("Blender animation or skin lost");
        cancel = true;
        bool rejected = false;
        try {
            importScene(plugins, file, cancel, progress);
        } catch (...) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Bridge ignored cancellation");
        std::cout << utf8(file) << ": first " << first->import_ms << " ms, converted cache "
                  << second->import_ms << " ms, " << first->triangles << " triangles\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

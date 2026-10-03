// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "extensions.h"
#include "localization.h"
namespace cy {
class EditorUI {
    Extensions extensions_;
    std::function<void()> wake_;
    struct ToolState {
        std::shared_ptr<ExtensionModule> module;
        void* data = nullptr;
        ~ToolState() {
            if (data)
                module->api->destroy_tool(data);
        }
    };
    struct JobResult {
        uint64_t generation = 0;
        std::shared_ptr<Scene> source, edited;
        std::shared_ptr<ExportBundle> bundle;
        std::string error;
    };
    std::shared_ptr<ToolState> state_;
    std::shared_ptr<OperationControl> control_;
    std::future<JobResult> job_;
    std::shared_ptr<ExportBundle> bundle_;
    std::shared_ptr<Scene> edited_, expected_, undo_;
    bool dirty_ = false, undo_dirty_ = false;
    bool export_popup_ = false, tool_open_ = false;
    int format_ = 0, tool_ = -1;
    uint64_t generation_ = 0;
    char output_[4096]{};
    std::string message_, error_;
    void selectTool(int);
    void beginExport(const std::shared_ptr<Scene>&, const fs::path&);
    void beginTool(const std::shared_ptr<Scene>&);
    void finishExport(bool replace);

  public:
    EditorUI(const fs::path& folder, std::function<void()> wake)
        : extensions_(folder), wake_(std::move(wake)) {}
    ~EditorUI();
    bool busy() const { return job_.valid(); }
    bool dirty() const { return dirty_; }
    const Extensions& extensions() const { return extensions_; }
    void reset();
    std::shared_ptr<Scene> take(const std::shared_ptr<Scene>&);
    void toolbar(const std::shared_ptr<Scene>&, const fs::path&, bool blocked, Language);
    void draw(const std::shared_ptr<Scene>&, const fs::path&, bool blocked, Language, void* nativeWindow);
    void showToolForSmoke(const std::shared_ptr<Scene>& source) {
        selectTool(0);
        if (state_)
            beginTool(source);
    }
};
} // namespace cy

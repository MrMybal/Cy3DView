// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include <atomic>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace cy {
struct Release {
    std::string version, notes, asset_url, sha256;
    uint64_t size = 0;
};
// Accept only stable vMAJOR.MINOR.PATCH releases from this application's repository.
int compareVersions(std::string_view a, std::string_view b);
Release parseRelease(std::string_view json, bool windows);
bool updateHostAllowed(std::string_view url);
bool verifyUpdateFile(const std::filesystem::path& file, uint64_t size, std::string_view sha256);
enum class UpdatePhase { Idle, Checking, Current, Available, Downloading, Ready, Failed, Cancelled };
struct UpdateSnapshot {
    UpdatePhase phase = UpdatePhase::Idle;
    Release release;
    std::string error;
    uint64_t received = 0;
};
class Updater {
    mutable std::mutex mutex_;
    UpdateSnapshot state_;
    std::jthread worker_;
    std::atomic<bool> cancel_{false};
    std::filesystem::path installer_, temporary_;
    std::function<void()> wake_;
    void publish(UpdateSnapshot state);

  public:
    explicit Updater(std::function<void()> wake = {}) : wake_(std::move(wake)) {}
    ~Updater();
    UpdateSnapshot snapshot() const;
    void check(std::string version, std::string token = {});
    void download(std::string token = {});
    void cancel() { cancel_ = true; }
    // Launches an interactive installer. The caller closes the app only on success.
    bool install(const std::filesystem::path& appDirectory, bool french, std::string& error);
    static bool openReleases();
};
} // namespace cy

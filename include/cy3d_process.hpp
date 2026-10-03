// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#pragma once
#include "cy3d_plugin.h"
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
extern char** environ;
#endif
namespace cy {
inline std::string pathText(const std::filesystem::path& p) {
    auto s = p.u8string();
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}
#ifdef _WIN32
inline std::wstring wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                nullptr, 0);
    if (!n && !text.empty())
        throw std::runtime_error("UTF-8 invalide");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), out.data(),
                        n);
    return out;
}
inline std::wstring quote(const std::wstring& s) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : s) {
        if (c == L'\\') {
            ++slashes;
            continue;
        }
        out.append(c == L'"' ? slashes * 2 + 1 : slashes, L'\\');
        out += c;
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    out += L'"';
    return out;
}
#endif
inline int run(const std::filesystem::path& executable, const std::vector<std::string>& args,
               const std::filesystem::path& log, const Cy3DHost* host, int timeout = 240,
               const std::vector<std::string>& environment = {}) {
    auto cancelled = [&] { return host->is_cancelled && host->is_cancelled(host->context); };
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Journal de conversion inaccessible");
    std::wstring command = quote(executable.wstring());
    for (const auto& arg : args)
        command += L" " + quote(wide(arg));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = startup.hStdError = output;
    startup.hStdInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    std::vector<std::wstring> entries;
    auto inherited = GetEnvironmentStringsW();
    if (inherited) {
        for (auto p = inherited; *p; p += wcslen(p) + 1)
            entries.emplace_back(p);
        FreeEnvironmentStringsW(inherited);
    }
    for (const auto& value : environment) {
        auto entry = wide(value);
        auto equal = entry.find(L'=');
        auto name = entry.substr(0, equal + 1);
        std::erase_if(
            entries, [&](const auto& old) { return _wcsnicmp(old.c_str(), name.c_str(), name.size()) == 0; });
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& a, const auto& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    std::vector<wchar_t> block;
    for (const auto& entry : entries) {
        block.insert(block.end(), entry.begin(), entry.end());
        block.push_back(0);
    }
    block.push_back(0);
    PROCESS_INFORMATION process{};
    BOOL ok = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, block.data(),
                             log.parent_path().c_str(), &startup, &process);
    CloseHandle(output);
    if (startup.hStdInput != INVALID_HANDLE_VALUE)
        CloseHandle(startup.hStdInput);
    if (!ok)
        throw std::runtime_error("Impossible de lancer le convertisseur installe");
    CloseHandle(process.hThread);
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        if (!AssignProcessToJobObject(job, process.hProcess)) {
            CloseHandle(job);
            job = nullptr;
        }
    }
    bool stop = false;
    while (WaitForSingleObject(process.hProcess, 20) == WAIT_TIMEOUT)
        if (cancelled() || std::chrono::steady_clock::now() > deadline) {
            stop = true;
            if (job)
                TerminateJobObject(job, 1);
            else
                TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 5000);
            break;
        }
    DWORD code = 1;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    if (job)
        CloseHandle(job);
#else
    auto name = pathText(executable);
    std::vector<std::string> storage{name};
    storage.insert(storage.end(), args.begin(), args.end());
    std::vector<char*> arguments;
    for (auto& a : storage)
        arguments.push_back(a.data());
    arguments.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                                     0600);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    std::vector<std::string> entries;
    for (auto p = environ; *p; ++p)
        entries.emplace_back(*p);
    for (const auto& value : environment) {
        auto key = value.substr(0, value.find('=') + 1);
        std::erase_if(entries, [&](const auto& old) { return old.starts_with(key); });
        entries.push_back(value);
    }
    std::vector<char*> env;
    for (auto& entry : entries)
        env.push_back(entry.data());
    env.push_back(nullptr);
    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
    posix_spawnattr_setpgroup(&attributes, 0);
    pid_t process;
    int spawn = posix_spawn(&process, name.c_str(), &actions, &attributes, arguments.data(), env.data());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (spawn)
        throw std::runtime_error("Impossible de lancer le convertisseur installe");
    int status = 0;
    bool stop = false;
    while (waitpid(process, &status, WNOHANG) == 0) {
        if (cancelled() || std::chrono::steady_clock::now() > deadline) {
            stop = true;
            kill(-process, SIGKILL);
            waitpid(process, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    int code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
    if (stop)
        throw std::runtime_error(cancelled() ? "Chargement annule"
                                             : "Conversion trop longue (limite de 4 minutes)");
    return static_cast<int>(code);
}
} // namespace cy

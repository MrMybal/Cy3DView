// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "updater.h"
#include <rapidjson/document.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <shellapi.h>
#else
#include <curl/curl.h>
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace cy {
namespace {
constexpr auto api = "https://api.github.com/repos/MrMybal/Cy3DView/";
constexpr auto page = "https://github.com/MrMybal/Cy3DView/releases";
constexpr uint64_t maxInstaller = 512ull * 1024 * 1024;
std::array<unsigned, 3> version(std::string_view text) {
    if (text.starts_with('v'))
        text.remove_prefix(1);
    std::array<unsigned, 3> parts{};
    for (size_t i = 0; i < parts.size(); ++i) {
        auto dot = text.find('.');
        auto component = text.substr(0, dot);
        auto [end, ec] = std::from_chars(component.data(), component.data() + component.size(), parts[i]);
        if (component.empty() || ec != std::errc{} || end != component.data() + component.size() ||
            (component.size() > 1 && component.front() == '0') || (i < 2 && dot == std::string_view::npos) ||
            (i == 2 && dot != std::string_view::npos))
            throw std::runtime_error("Invalid release version.");
        if (i < 2)
            text.remove_prefix(dot + 1);
    }
    return parts;
}
std::string field(const rapidjson::Value& value, const char* key) {
    if (!value.IsObject() || !value.HasMember(key) || !value[key].IsString())
        throw std::runtime_error("Invalid release information.");
    return {value[key].GetString(), value[key].GetStringLength()};
}
using Sink = std::function<void(const char*, size_t)>;
struct Response {
    unsigned status = 0;
    std::string location;
};
#ifdef _WIN32
std::wstring wide(std::string_view text) {
    auto n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                                 nullptr, 0);
    if (!n)
        throw std::runtime_error("Invalid release information.");
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        result.data(), n);
    return result;
}
struct HttpHandle {
    HINTERNET value{};
    ~HttpHandle() {
        if (value)
            WinHttpCloseHandle(value);
    }
};
Response request(const std::string& url, const std::string& accept, const std::string& token,
                 const Sink& sink, const std::atomic<bool>& cancel) {
    auto address = wide(url);
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(address.c_str(), 0, 0, &parts))
        throw std::runtime_error("Invalid update address.");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring object(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        object.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    HttpHandle session{WinHttpOpen(L"Cy3DView/" CY3D_VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value)
        throw std::runtime_error("Could not connect to GitHub.");
    WinHttpSetTimeouts(session.value, 10000, 10000, 15000, 15000);
    HttpHandle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};
    HttpHandle query{connection.value ? WinHttpOpenRequest(connection.value, L"GET", object.c_str(), nullptr,
                                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                           WINHTTP_FLAG_SECURE)
                                      : nullptr};
    if (!query.value)
        throw std::runtime_error("Could not connect to GitHub.");
    DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(query.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)))
        throw std::runtime_error("Could not connect to GitHub.");
    auto headers =
        wide("Accept: " + accept + "\r\nX-GitHub-Api-Version: 2022-11-28\r\n" +
             (url.starts_with(api) && !token.empty() ? "Authorization: Bearer " + token + "\r\n" : ""));
    if (cancel)
        throw std::runtime_error("Update cancelled.");
    if (!WinHttpSendRequest(query.value, headers.c_str(), static_cast<DWORD>(headers.size()),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(query.value, nullptr))
        throw std::runtime_error("Could not connect to GitHub.");
    DWORD status{}, length = sizeof(status);
    if (!WinHttpQueryHeaders(query.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
                             &status, &length, nullptr))
        throw std::runtime_error("Could not connect to GitHub.");
    Response response{status, {}};
    if (status >= 300 && status < 400) {
        std::array<wchar_t, 8192> location{};
        length = static_cast<DWORD>(location.size() * sizeof(wchar_t));
        if (WinHttpQueryHeaders(query.value, WINHTTP_QUERY_LOCATION, nullptr, location.data(), &length,
                                nullptr)) {
            auto n = WideCharToMultiByte(CP_UTF8, 0, location.data(), -1, nullptr, 0, nullptr, nullptr);
            response.location.resize(n ? n - 1 : 0);
            if (n > 1) {
                std::vector<char> buffer(n);
                WideCharToMultiByte(CP_UTF8, 0, location.data(), -1, buffer.data(), n, nullptr, nullptr);
                response.location.assign(buffer.data(), n - 1);
            }
        }
    }
    if (status == 200) {
        std::array<char, 65536> buffer{};
        for (;;) {
            if (cancel)
                throw std::runtime_error("Update cancelled.");
            DWORD got{};
            if (!WinHttpReadData(query.value, buffer.data(), static_cast<DWORD>(buffer.size()), &got))
                throw std::runtime_error("Download interrupted. Please try again.");
            if (!got)
                break;
            sink(buffer.data(), got);
        }
    }
    return response;
}
#else
Response request(const std::string& url, const std::string& accept, const std::string& token,
                 const Sink& sink, const std::atomic<bool>& cancel) {
    static const auto initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK)
        throw std::runtime_error("Could not connect to GitHub.");
    struct Curl {
        CURL* value = curl_easy_init();
        ~Curl() {
            if (value)
                curl_easy_cleanup(value);
        }
    } curl;
    struct Headers {
        curl_slist* value{};
        ~Headers() { curl_slist_free_all(value); }
        void add(const std::string& text) { value = curl_slist_append(value, text.c_str()); }
    } headers;
    if (!curl.value)
        throw std::runtime_error("Could not connect to GitHub.");
    headers.add("Accept: " + accept);
    headers.add("X-GitHub-Api-Version: 2022-11-28");
    if (url.starts_with(api) && !token.empty())
        headers.add("Authorization: Bearer " + token);
    Response response;
    struct Data {
        const Sink& sink;
        const std::atomic<bool>& cancel;
        Response& response;
        std::exception_ptr error;
    } data{sink, cancel, response, {}};
    curl_easy_setopt(curl.value, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.value, CURLOPT_USERAGENT, "Cy3DView/" CY3D_VERSION);
    curl_easy_setopt(curl.value, CURLOPT_HTTPHEADER, headers.value);
    curl_easy_setopt(curl.value, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl.value, CURLOPT_TIMEOUT, 45L);
    curl_easy_setopt(curl.value, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.value, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(
        curl.value, CURLOPT_WRITEFUNCTION,
        +[](char* buffer, size_t size, size_t count, void* context) -> size_t {
            auto& d = *static_cast<Data*>(context);
            auto bytes = size * count;
            if (d.cancel)
                return 0;
            try {
                if (d.response.status == 200)
                    d.sink(buffer, bytes);
            } catch (...) {
                d.error = std::current_exception();
                return 0;
            }
            return bytes;
        });
    curl_easy_setopt(curl.value, CURLOPT_HEADERDATA, &data);
    curl_easy_setopt(
        curl.value, CURLOPT_HEADERFUNCTION,
        +[](char* buffer, size_t size, size_t count, void* context) -> size_t {
            auto& d = *static_cast<Data*>(context);
            auto bytes = size * count;
            std::string_view line(buffer, bytes);
            if (line.starts_with("HTTP/")) {
                auto at = line.find(' ');
                unsigned value{};
                if (at != std::string_view::npos)
                    std::from_chars(line.data() + at + 1, line.data() + line.size(), value);
                d.response.status = value;
            } else if (line.size() > 10 &&
                       (line.starts_with("location: ") || line.starts_with("Location: "))) {
                line.remove_prefix(10);
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                    line.remove_suffix(1);
                d.response.location = line;
            }
            return bytes;
        });
    auto result = curl_easy_perform(curl.value);
    if (data.error)
        std::rethrow_exception(data.error);
    if (cancel)
        throw std::runtime_error("Update cancelled.");
    if (result != CURLE_OK)
        throw std::runtime_error("Could not connect to GitHub.");
    return response;
}
#endif
void get(std::string url, const std::string& accept, const std::string& token, const Sink& sink,
         const std::atomic<bool>& cancel) {
    if (token.size() > 256 ||
        token.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") !=
            std::string::npos)
        throw std::runtime_error("Invalid GitHub access token.");
    for (int redirect = 0; redirect < 6; ++redirect) {
        if (!updateHostAllowed(url))
            throw std::runtime_error("Invalid update address.");
        auto response = request(url, accept, token, sink, cancel);
        if (response.status == 200)
            return;
        if ((response.status == 301 || response.status == 302 || response.status == 303 ||
             response.status == 307 || response.status == 308) &&
            !response.location.empty()) {
            url = std::move(response.location);
            continue;
        }
        if (response.status == 404)
            throw std::runtime_error(
                "No accessible release. A private repository needs a GitHub access token.");
        if (response.status == 401 || response.status == 403)
            throw std::runtime_error("GitHub access denied or request limit reached.");
        throw std::runtime_error("Could not connect to GitHub.");
    }
    throw std::runtime_error("Too many download redirects.");
}
#ifdef _WIN32
std::string digest(const std::filesystem::path& file) {
    struct Hash {
        BCRYPT_ALG_HANDLE algorithm{};
        BCRYPT_HASH_HANDLE hash{};
        ~Hash() {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } handle;
    if (BCryptOpenAlgorithmProvider(&handle.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(handle.algorithm, &handle.hash, nullptr, 0, nullptr, 0, 0) < 0)
        throw std::runtime_error("Could not verify the download.");
    std::ifstream input(file, std::ios::binary);
    std::array<char, 65536> bytes{};
    if (!input)
        throw std::runtime_error("Could not verify the download.");
    while (input) {
        input.read(bytes.data(), bytes.size());
        if (BCryptHashData(handle.hash, reinterpret_cast<PUCHAR>(bytes.data()),
                           static_cast<ULONG>(input.gcount()), 0) < 0)
            throw std::runtime_error("Could not verify the download.");
    }
    if (!input.eof())
        throw std::runtime_error("Could not verify the download.");
    std::array<unsigned char, 32> hash{};
    if (BCryptFinishHash(handle.hash, hash.data(), static_cast<ULONG>(hash.size()), 0) < 0)
        throw std::runtime_error("Could not verify the download.");
    std::string result;
    for (auto byte : hash) {
        constexpr auto hex = "0123456789abcdef";
        result += hex[byte >> 4];
        result += hex[byte & 15];
    }
    return result;
}
#endif
} // namespace
int compareVersions(std::string_view a, std::string_view b) {
    auto left = version(a), right = version(b);
    return left < right ? -1 : left > right ? 1 : 0;
}
bool updateHostAllowed(std::string_view url) {
    if (!url.starts_with("https://") || url.find_first_of("\r\n\\ \t") != std::string_view::npos ||
        std::any_of(url.begin(), url.end(), [](unsigned char c) { return c < 32 || c == 127; }))
        return false;
    url.remove_prefix(8);
    auto host = url.substr(0, url.find('/'));
    return host == "api.github.com" || host == "github.com" ||
           host == "release-assets.githubusercontent.com" || host == "objects.githubusercontent.com";
}
bool verifyUpdateFile(const std::filesystem::path& file, uint64_t size, std::string_view sha256) {
#ifdef _WIN32
    std::error_code error;
    if (std::filesystem::file_size(file, error) != size || error)
        return false;
    return digest(file) == sha256;
#else
    (void)file;
    (void)size;
    (void)sha256;
    return false; // Only Windows uses automatic installer downloads.
#endif
}
Release parseRelease(std::string_view json, bool windows) {
    if (json.size() > 2 * 1024 * 1024)
        throw std::runtime_error("Invalid release information.");
    rapidjson::Document doc;
    doc.Parse(json.data(), json.size());
    if (doc.HasParseError() || !doc.IsObject() || !doc.HasMember("draft") || !doc["draft"].IsBool() ||
        doc["draft"].GetBool() || !doc.HasMember("prerelease") || !doc["prerelease"].IsBool() ||
        doc["prerelease"].GetBool())
        throw std::runtime_error("Invalid release information.");
    Release release;
    auto tag = field(doc, "tag_name");
    (void)version(tag);
    release.version = tag.starts_with('v') ? tag.substr(1) : tag;
    if (doc.HasMember("body") && doc["body"].IsString())
        release.notes = field(doc, "body");
    if (!windows)
        return release;
    auto name = "Cy3DView-" + release.version + "-win64-setup.exe";
    if (!doc.HasMember("assets") || !doc["assets"].IsArray() || doc["assets"].Size() > 200)
        throw std::runtime_error("No Windows installer in this release.");
    for (const auto& asset : doc["assets"].GetArray()) {
        if (!asset.IsObject() || !asset.HasMember("name") || !asset["name"].IsString() ||
            field(asset, "name") != name)
            continue;
        auto url = field(asset, "url");
        auto hash = field(asset, "digest");
        auto prefix = std::string(api) + "releases/assets/";
        auto id = std::string_view(url).substr(std::min(prefix.size(), url.size()));
        if (!url.starts_with(prefix) || id.empty() ||
            id.find_first_not_of("0123456789") != std::string_view::npos || !hash.starts_with("sha256:") ||
            hash.size() != 71 || hash.substr(7).find_first_not_of("0123456789abcdef") != std::string::npos ||
            !asset.HasMember("size") || !asset["size"].IsUint64() || !asset["size"].GetUint64() ||
            asset["size"].GetUint64() > maxInstaller)
            throw std::runtime_error("Invalid release information.");
        release.asset_url = url;
        release.sha256 = hash.substr(7);
        release.size = asset["size"].GetUint64();
        return release;
    }
    throw std::runtime_error("No Windows installer in this release.");
}
UpdateSnapshot Updater::snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
}
void Updater::publish(UpdateSnapshot state) {
    {
        std::lock_guard lock(mutex_);
        state_ = std::move(state);
    }
    if (wake_)
        wake_();
}
Updater::~Updater() {
    cancel_ = true;
    if (worker_.joinable())
        worker_.join();
    // Remove only our two known temporary files, never recursively delete a directory.
    std::error_code ec;
    if (!installer_.empty())
        std::filesystem::remove(installer_, ec);
    if (!temporary_.empty())
        std::filesystem::remove(temporary_, ec);
}
void Updater::check(std::string current, std::string token) {
    auto phase = snapshot().phase;
    if (phase == UpdatePhase::Checking || phase == UpdatePhase::Downloading)
        return;
    if (worker_.joinable())
        worker_.join();
    cancel_ = false;
    publish({UpdatePhase::Checking, {}, {}, 0});
    worker_ = std::jthread([this, current = std::move(current), token = std::move(token)] {
        try {
            std::string body;
            get(
                std::string(api) + "releases/latest", "application/vnd.github+json", token,
                [&](const char* data, size_t size) {
                    if (body.size() + size > 2 * 1024 * 1024)
                        throw std::runtime_error("Invalid release information.");
                    body.append(data, size);
                },
                cancel_);
            // Old portable releases may not have an installer. Still report up-to-date correctly.
            auto release = parseRelease(body, false);
            auto newer = compareVersions(release.version, current) > 0;
#ifdef _WIN32
            if (newer)
                release = parseRelease(body, true);
#endif
            publish({newer ? UpdatePhase::Available : UpdatePhase::Current, std::move(release), {}, 0});
        } catch (const std::exception& e) {
            publish({cancel_ ? UpdatePhase::Cancelled : UpdatePhase::Failed, {}, e.what(), 0});
        }
    });
}
void Updater::download(std::string token) {
#ifdef _WIN32
    auto state = snapshot();
    if (state.phase != UpdatePhase::Available)
        return;
    if (worker_.joinable())
        worker_.join();
    cancel_ = false;
    std::error_code cleanup;
    if (!installer_.empty())
        std::filesystem::remove(installer_, cleanup);
    if (!temporary_.empty())
        std::filesystem::remove(temporary_, cleanup);
    installer_.clear();
    temporary_.clear();
    state.phase = UpdatePhase::Downloading;
    state.received = 0;
    publish(state);
    worker_ = std::jthread([this, state, token = std::move(token)]() mutable {
        try {
            wchar_t temp[MAX_PATH], file[MAX_PATH];
            if (!GetTempPathW(MAX_PATH, temp) || !GetTempFileNameW(temp, L"Cy3", 0, file))
                throw std::runtime_error("Could not save the download.");
            // Unique file created by Windows, no shared predictable updater name.
            temporary_ = file;
            installer_ = temporary_;
            installer_ += L".exe";
            std::ofstream output(temporary_, std::ios::binary | std::ios::trunc);
            if (!output)
                throw std::runtime_error("Could not save the download.");
            auto notify = std::chrono::steady_clock::now();
            get(
                state.release.asset_url, "application/octet-stream", token,
                [&](const char* bytes, size_t size) {
                    if (state.received + size > state.release.size)
                        throw std::runtime_error("Download size does not match.");
                    output.write(bytes, size);
                    if (!output)
                        throw std::runtime_error("Could not save the download.");
                    state.received += size;
                    auto now = std::chrono::steady_clock::now();
                    if (now - notify > std::chrono::milliseconds(80)) {
                        publish(state);
                        notify = now;
                    }
                },
                cancel_);
            output.close();
            if (!output || state.received != state.release.size)
                throw std::runtime_error("Download size does not match.");
            if (!verifyUpdateFile(temporary_, state.release.size, state.release.sha256))
                throw std::runtime_error("Download verification failed. Please try again.");
            if (cancel_)
                throw std::runtime_error("Update cancelled.");
            std::filesystem::rename(temporary_, installer_);
            state.phase = UpdatePhase::Ready;
            publish(state);
        } catch (const std::exception& e) {
            std::error_code ec;
            std::filesystem::remove(temporary_, ec);
            std::filesystem::remove(installer_, ec);
            state.phase = cancel_ ? UpdatePhase::Cancelled : UpdatePhase::Failed;
            state.error = e.what();
            publish(state);
        }
    });
#else
    (void)token;
    if (snapshot().phase == UpdatePhase::Available)
        openReleases();
#endif
}
bool Updater::install(const std::filesystem::path& directory, bool french, std::string& error) {
#ifdef _WIN32
    if (snapshot().phase != UpdatePhase::Ready)
        return false;
    if (worker_.joinable())
        worker_.join();
    try {
        auto release = snapshot().release;
        if (!verifyUpdateFile(installer_, release.size, release.sha256))
            throw std::runtime_error("Download verification failed. Please try again.");
        auto target = directory.wstring();
        if (target.find_first_of(L"\"\r\n") != std::wstring::npos)
            throw std::runtime_error("Invalid installation folder.");
        auto args = L"\"" + installer_.wstring() + L"\" /SP- /DIR=\"" + target + L"\" /LANG=" +
                    (french ? L"french" : L"english") + L" /WAITPID=" +
                    std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW start{sizeof(start)};
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(installer_.c_str(), args.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            directory.c_str(), &start, &process))
            throw std::runtime_error("Could not start the installer.");
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        // Inno copies its executable into its own temporary directory, then owns cleanup.
        // Do not remove the source while its bootstrap process is starting.
        installer_.clear();
        temporary_.clear();
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
#else
    (void)directory;
    (void)french;
    (void)error;
    return openReleases();
#endif
}
bool Updater::openReleases() {
#ifdef _WIN32
    return reinterpret_cast<INT_PTR>(
               ShellExecuteW(nullptr, L"open", wide(page).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#else
#ifdef __APPLE__
    const char* command = "/usr/bin/open";
#else
    const char* command = "xdg-open";
#endif
    char* args[]{const_cast<char*>(command), const_cast<char*>(page), nullptr};
    pid_t pid{};
    if (posix_spawnp(&pid, command, nullptr, nullptr, args, environ))
        return false;
    std::thread([pid] {
        int status{};
        waitpid(pid, &status, 0);
    }).detach();
    return true;
#endif
}
} // namespace cy

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Cyberalien
#include "updater.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string metadata(std::string tag = "v0.5.0",
                     std::string url = "https://api.github.com/repos/MrMybal/Cy3DView/releases/assets/12",
                     std::string digest = "sha256:" + std::string(64, 'a'), std::string size = "123") {
    return "{\"tag_name\":\"" + tag +
           "\",\"draft\":false,\"prerelease\":false,\"body\":\"Notes\\nMore\",\"assets\":[{\"name\":"
           "\"Cy3DView-0.5.0-win64-setup.exe\",\"url\":\"" +
           url + "\",\"digest\":\"" + digest + "\",\"size\":" + size + "}]}";
}
template <class F> void rejects(F action) {
    bool rejected = false;
    try {
        action();
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Unsafe metadata was accepted");
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc >= 3 && std::string_view(argv[1]) == "--github") {
            // Test-only environment input; never included in app configuration or diagnostics.
            auto token = std::getenv("CY3D_TEST_GITHUB_TOKEN");
            cy::Updater updater;
            updater.check(argv[2], token ? token : "");
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
            bool downloading = false;
            for (;;) {
                auto state = updater.snapshot();
                if (state.phase == cy::UpdatePhase::Failed)
                    throw std::runtime_error(state.error);
                if (state.phase == cy::UpdatePhase::Current) {
                    std::cout << "GitHub check: up to date " << state.release.version << '\n';
                    break;
                }
                if (state.phase == cy::UpdatePhase::Available && !downloading) {
                    std::cout << "GitHub check: available " << state.release.version << '\n';
                    if (argc > 3 && std::string_view(argv[3]) == "--download") {
                        updater.download(token ? token : "");
                        downloading = true;
                    } else
                        break;
                }
                if (state.phase == cy::UpdatePhase::Ready) {
                    std::cout << "Installer download and SHA-256 verification passed\n";
                    break;
                }
                if (std::chrono::steady_clock::now() > deadline)
                    throw std::runtime_error("GitHub test timed out");
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return 0;
        }
        require(cy::compareVersions("v1.10.0", "1.9.3") > 0, "Version compared lexicographically");
        require(cy::compareVersions("0.5.0", "v0.5.0") == 0, "Tag prefix mismatch");
        require(cy::compareVersions("0.4.4", "0.5.0") < 0, "Downgrade comparison failed");
        for (auto tag :
             {"", "v0.5", "0.5.0-rc1", "1.2.3.4", "1.2.03", "1.-2.3", "999999999999.1.1", "1.2.3garbage"})
            rejects([&] { cy::compareVersions(tag, "0.5.0"); });
        auto release = cy::parseRelease(metadata(), true);
        require(release.version == "0.5.0" && release.size == 123 && release.notes == "Notes\nMore",
                "Release parsing failed");
        for (auto url :
             {"http://github.com/file", "https://github.com.evil.org/file",
              "https://github.com@evil.org/file", "https://api.github.com:444/file",
              "https://github.com\\evil.org/file", "file:///setup.exe", "https://github.com/file\r\nheader"})
            require(!cy::updateHostAllowed(url), "Untrusted URL accepted");
        require(cy::updateHostAllowed("https://release-assets.githubusercontent.com/file?signature=abc"),
                "GitHub redirect rejected");
        rejects([] { cy::parseRelease("{}", true); });
        rejects([] { cy::parseRelease(metadata("v0.5.0-rc1"), true); });
        rejects([] {
            cy::parseRelease(metadata("v0.5.0", "https://api.github.com/repos/other/app/releases/assets/12"),
                             true);
        });
        rejects([] { cy::parseRelease(metadata("v0.5.0", "https://evil.org/setup.exe"), true); });
        rejects([] {
            cy::parseRelease(
                metadata("v0.5.0",
                         "https://api.github.com/repos/MrMybal/Cy3DView/releases/assets/12?redirect=evil"),
                true);
        });
        rejects([] {
            cy::parseRelease(
                metadata("v0.5.0", "https://api.github.com/repos/MrMybal/Cy3DView/releases/assets/12", ""),
                true);
        });
        rejects([] {
            cy::parseRelease(metadata("v0.5.0",
                                      "https://api.github.com/repos/MrMybal/Cy3DView/releases/assets/12",
                                      "sha256:" + std::string(64, 'z')),
                             true);
        });
        for (auto size : {"0", "-1", "536870913", "\"123\""})
            rejects([&] {
                cy::parseRelease(metadata("v0.5.0",
                                          "https://api.github.com/repos/MrMybal/Cy3DView/releases/assets/12",
                                          "sha256:" + std::string(64, 'a'), size),
                                 true);
            });
        for (auto flag : {"draft", "prerelease"}) {
            auto text = metadata();
            auto at = text.find(std::string("\"") + flag + "\":false");
            text.replace(at + std::string(flag).size() + 3, 5, "true");
            rejects([&] { cy::parseRelease(text, true); });
        }
        cy::Updater updater;
        updater.download();
        require(updater.snapshot().phase == cy::UpdatePhase::Idle,
                "Unexpected update action without release");
#ifdef _WIN32
        const auto hashFile =
            std::filesystem::temp_directory_path() /
            ("Cy3DView-hash-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tmp");
        {
            std::ofstream file(hashFile, std::ios::binary);
            file << "abc";
        }
        constexpr auto expected = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        require(cy::verifyUpdateFile(hashFile, 3, expected), "Known SHA-256 vector failed");
        require(!cy::verifyUpdateFile(hashFile, 4, expected), "Truncated file accepted");
        {
            std::ofstream file(hashFile, std::ios::binary);
            file << "abd";
        }
        require(!cy::verifyUpdateFile(hashFile, 3, expected), "Corrupted file accepted");
        std::filesystem::remove(hashFile);
#endif
        std::cout << "Update versions, release metadata, size, hashes and URL constraints passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

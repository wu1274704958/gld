#pragma once

// Pluggable file system. All asset loaders read through IFileSystem instead of
// calling std file APIs directly, so a tightly-controlled OS can swap in a
// custom implementation (asset packs, sandboxed API, Android AAssetManager...).
// read_* are called from worker threads -> implementations must be safe for
// concurrent reads.

#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <algorithm>

#include "../EcsWorld.hpp"
#include "../App.hpp"

namespace gld::ecs {

    struct FileSystemEntry {
        std::string name;
        bool is_directory = false;
        bool is_regular_file = false;
    };

    struct IFileSystem {
        virtual bool exists(const std::string& path) const = 0;
        virtual std::optional<std::vector<std::byte>> read_bytes(const std::string& path) const = 0;
        virtual std::optional<std::string> read_text(const std::string& path) const = 0;
        virtual std::vector<FileSystemEntry> list(const std::string&) const { return {}; }
        virtual std::string resolve(const std::string& path) const { return path; }
        virtual ~IFileSystem() = default;
    };

    // Default: std::filesystem + ifstream, relative to a root directory.
    class StdFileSystem : public IFileSystem {
    public:
        explicit StdFileSystem(std::filesystem::path root = ".") : root_(std::move(root)) {}

        std::string resolve(const std::string& path) const override {
            return (root_ / path).string();
        }
        bool exists(const std::string& path) const override {
            return std::filesystem::exists(resolve(path));
        }
        std::optional<std::vector<std::byte>> read_bytes(const std::string& path) const override {
            std::ifstream f(resolve(path), std::ios::binary | std::ios::ate);
            if (!f) return std::nullopt;
            auto size = static_cast<std::streamsize>(f.tellg());
            f.seekg(0);
            std::vector<std::byte> buf(static_cast<size_t>(size));
            if (size > 0 && !f.read(reinterpret_cast<char*>(buf.data()), size)) return std::nullopt;
            return buf;
        }
        std::optional<std::string> read_text(const std::string& path) const override {
            std::ifstream f(resolve(path), std::ios::binary);
            if (!f) return std::nullopt;
            return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        }
        std::vector<FileSystemEntry> list(const std::string& path) const override {
            std::vector<FileSystemEntry> result;
            std::error_code ec;
            const auto dir = std::filesystem::path(resolve(path));
            for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                const auto& entry = *it;
                result.push_back({
                    entry.path().filename().string(),
                    entry.is_directory(ec),
                    entry.is_regular_file(ec)
                });
                ec.clear();
            }
            std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
                return a.name < b.name;
            });
            return result;
        }
    private:
        std::filesystem::path root_;
    };

    // The active file system is stored as a resource (shared_ptr<IFileSystem>).
    inline void FileSystemPlugin(App& app, std::shared_ptr<IFileSystem> fs) {
        app.world.resource_or_add<std::shared_ptr<IFileSystem>>(std::move(fs));
    }
}

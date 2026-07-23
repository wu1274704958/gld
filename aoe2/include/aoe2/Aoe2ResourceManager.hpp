#pragma once

#include "Aoe2Assets.hpp"
#include <optional>

namespace gld::ecs::aoe2 {

struct UnitRecord {
    std::string id;
    std::string manifest_path;
    std::vector<std::string> animations;
    std::vector<std::string> missing_animations;
    bool complete = false;
    int warning_count = 0;
};

class Aoe2ResourceManager {
public:
    Aoe2ResourceManager() = default;
    Aoe2ResourceManager(AssetServer& server, std::string root)
        : server_(&server), root_(std::move(root)) {}

    void refresh();
    const std::vector<UnitRecord>& list_units() const { return units_; }
    const UnitRecord* find(const std::string& id) const;
    bool contains(const std::string& id) const { return find(id) != nullptr; }
    Handle<Aoe2UnitAppearance> load(const std::string& id);
    const std::string& root() const { return root_; }

private:
    AssetServer* server_ = nullptr;
    std::string root_ = "aoe2de_cache";
    std::vector<UnitRecord> units_;
};

} // namespace gld::ecs::aoe2

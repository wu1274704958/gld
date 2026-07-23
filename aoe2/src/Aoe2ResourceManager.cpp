#include <aoe2/Aoe2ResourceManager.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace gld::ecs::aoe2 {

void Aoe2ResourceManager::refresh() {
    units_.clear();
    if (!server_ || !server_->fs) return;
    std::unordered_set<std::string> ids;
    for (const auto& entry : server_->fs->list(root_)) {
        if (!entry.is_directory) continue;
        const std::string manifest_path =
            (std::filesystem::path(root_) / entry.name / "manifest.json").generic_string();
        const auto text = server_->fs->read_text(manifest_path);
        if (!text) continue;
        try {
            const auto j = nlohmann::json::parse(*text);
            if (j.at("schema_version").get<int>() != 2 ||
                j.at("kind").get<std::string>() != "aoe2de_unit") continue;
            UnitRecord record;
            record.id = j.at("id").get<std::string>();
            if (!ids.insert(record.id).second) {
                std::fprintf(stderr, "[aoe2] duplicate unit id ignored: %s\n", record.id.c_str());
                continue;
            }
            record.manifest_path = manifest_path;
            record.missing_animations = j.value("missing_animations", std::vector<std::string>{});
            for (auto it = j.at("animations").begin(); it != j.at("animations").end(); ++it)
                if (it.value().value("status", std::string{}) == "exported") record.animations.push_back(it.key());
            std::sort(record.animations.begin(), record.animations.end());
            if (j.contains("summary")) {
                record.complete = j.at("summary").value("complete", false);
                record.warning_count = j.at("summary").value("warning_count", 0);
            }
            units_.push_back(std::move(record));
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[aoe2] invalid manifest %s: %s\n", manifest_path.c_str(), e.what());
        }
    }
    std::sort(units_.begin(), units_.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
}

const UnitRecord* Aoe2ResourceManager::find(const std::string& id) const {
    const auto it = std::lower_bound(units_.begin(), units_.end(), id,
        [](const UnitRecord& record, const std::string& value) { return record.id < value; });
    return it != units_.end() && it->id == id ? &*it : nullptr;
}

Handle<Aoe2UnitAppearance> Aoe2ResourceManager::load(const std::string& id) {
    const auto* record = find(id);
    return record && server_ ? server_->load(Aoe2UnitAppearanceDesc(record->manifest_path))
                             : Handle<Aoe2UnitAppearance>{};
}

} // namespace gld::ecs::aoe2

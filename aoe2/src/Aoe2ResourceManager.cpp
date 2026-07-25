#include <aoe2/Aoe2ResourceManager.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <unordered_set>
#include <nlohmann/json.hpp>

namespace gld::ecs::aoe2 {
namespace {
template<class Record>
void parse_common_record(Record& record, const nlohmann::json& source,
                         const std::string& manifest_path) {
    record.schema_version = source.at("schema_version").get<int>();
    record.id = source.at("id").get<std::string>();
    record.manifest_path = manifest_path;
    for (auto it = source.at("animations").begin();
         it != source.at("animations").end(); ++it) {
        if (it.value().value("status", std::string{}) == "exported")
            record.animations.push_back(it.key());
    }
    std::sort(record.animations.begin(), record.animations.end());
    if (source.contains("summary")) {
        record.complete = source.at("summary").value("complete", false);
        record.warning_count = source.at("summary").value("warning_count", 0);
    }
}
} // namespace

void Aoe2ResourceManager::refresh() {
    units_.clear();
    graphics_.clear();
    if (!server_ || !server_->fs) return;
    std::unordered_set<std::string> unit_ids;
    std::unordered_set<std::string> graphic_ids;

    const auto scan = [&](const std::string& directory,
                          const std::optional<std::string>& expected_kind,
                          bool legacy) {
        for (const auto& entry : server_->fs->list(directory)) {
            if (!entry.is_directory) continue;
            const std::string manifest_path =
                (std::filesystem::path(directory) / entry.name / "manifest.json")
                    .generic_string();
            const auto text = server_->fs->read_text(manifest_path);
            if (!text) continue;
            try {
                const auto source = nlohmann::json::parse(*text);
                const std::string kind = source.at("kind").get<std::string>();
                if (expected_kind && kind != *expected_kind) continue;
                const int schema = source.at("schema_version").get<int>();
                if (kind == "aoe2de_unit") {
                    if (schema != 2 && schema != 3) continue;
                    UnitRecord record;
                    parse_common_record(record, source, manifest_path);
                    record.missing_animations = source.value(
                        "missing_animations", std::vector<std::string>{});
                    if (schema == 3) {
                        const auto& dat = source.at("dat");
                        record.metadata_available = true;
                        record.civ_id = dat.at("civ_id").get<int>();
                        record.unit_id = dat.at("unit_id").get<int>();
                        record.mapping_source =
                            dat.at("mapping_source").get<std::string>();
                        if (record.civ_id < 0 || record.unit_id < 0)
                            throw std::runtime_error(
                                "invalid negative DAT civ/unit id");
                    }
                    if (!unit_ids.insert(record.id).second) {
                        std::fprintf(stderr,
                            "[aoe2] duplicate unit id ignored%s: %s\n",
                            legacy ? " from legacy cache" : "", record.id.c_str());
                        continue;
                    }
                    units_.push_back(std::move(record));
                } else if (kind == "aoe2de_graphics") {
                    if (schema != 2 || legacy) continue;
                    GraphicRecord record;
                    parse_common_record(record, source, manifest_path);
                    if (!graphic_ids.insert(record.id).second) {
                        std::fprintf(stderr,
                            "[aoe2] duplicate graphic id ignored: %s\n",
                            record.id.c_str());
                        continue;
                    }
                    graphics_.push_back(std::move(record));
                }
            } catch (const std::exception& error) {
                std::fprintf(stderr, "[aoe2] invalid manifest %s: %s\n",
                             manifest_path.c_str(), error.what());
            }
        }
    };

    scan((std::filesystem::path(root_) / "units").generic_string(),
         std::string("aoe2de_unit"), false);
    scan((std::filesystem::path(root_) / "graphics").generic_string(),
         std::string("aoe2de_graphics"), false);
    scan(root_, std::nullopt, true);

    std::sort(units_.begin(), units_.end(),
        [](const auto& a, const auto& b) { return a.id < b.id; });
    std::sort(graphics_.begin(), graphics_.end(),
        [](const auto& a, const auto& b) { return a.id < b.id; });
}

const UnitRecord* Aoe2ResourceManager::find(const std::string& id) const {
    const auto it = std::lower_bound(units_.begin(), units_.end(), id,
        [](const UnitRecord& record, const std::string& value) {
            return record.id < value;
        });
    return it != units_.end() && it->id == id ? &*it : nullptr;
}

const GraphicRecord* Aoe2ResourceManager::find_graphic(const std::string& id) const {
    const auto it = std::lower_bound(graphics_.begin(), graphics_.end(), id,
        [](const GraphicRecord& record, const std::string& value) {
            return record.id < value;
        });
    return it != graphics_.end() && it->id == id ? &*it : nullptr;
}

Handle<Aoe2UnitAppearance> Aoe2ResourceManager::load(const std::string& id) {
    const auto* record = find(id);
    return record && server_
        ? server_->load(Aoe2UnitAppearanceDesc(record->manifest_path))
        : Handle<Aoe2UnitAppearance>{};
}

Handle<Aoe2UnitAppearance> Aoe2ResourceManager::load_graphic(
    const std::string& id) {
    const auto* record = find_graphic(id);
    return record && server_
        ? server_->load(Aoe2UnitAppearanceDesc(record->manifest_path))
        : Handle<Aoe2UnitAppearance>{};
}

} // namespace gld::ecs::aoe2

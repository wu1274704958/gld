#include <aoe_gpu_motion/AoeGpuMotion.hpp>

#include <glad/glad.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <ecs/PerformanceMonitoring.hpp>
#include <ecs/assets/AssetServer.hpp>

namespace gld::ecs::aoe {
namespace {

constexpr float PixelsPerWorldUnit = 16.f;
constexpr std::uint16_t FirstStaticId = 0xF000u;
constexpr std::uint16_t WallId = 0xF000u;
constexpr std::uint16_t TreeId = 0xF001u;
constexpr std::uint16_t GenericStaticId = 0xF002u;
constexpr std::uint32_t InvalidIndex = 0xFFFFFFFFu;
constexpr std::uint32_t MaximumUnitHandle = 0xEFFFu;
constexpr int DependencyPasses = 32;
constexpr int ReservationRounds = 4;

std::string environment_value(const char* name) {
#if defined(_MSC_VER)
    char* raw = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&raw, &length, name) != 0 || !raw) return {};
    std::string value(raw);
    std::free(raw);
    return value;
#else
    const char* raw = std::getenv(name);
    return raw ? std::string(raw) : std::string{};
#endif
}

struct GpuUnitData {
    float position_radii[4];
    float intent_dt[4];
    std::uint32_t identity_wait[4];
};
struct GpuProposal { float values[4]; std::uint32_t meta[4]; };
struct GpuDecision { float velocity[4]; std::uint32_t identity[4]; };
static_assert(sizeof(GpuUnitData) == 48);
static_assert(sizeof(GpuProposal) == 32);
static_assert(sizeof(GpuDecision) == 32);

enum class Pass : std::size_t {
    ClearCurrent, FillUnits, BuildField, Propose, ClearReservation,
    Reserve, Validate, Propagate, PrepareNext, Commit, WriteDecisions, Count
};

const char* pass_file(Pass pass) {
    switch (pass) {
    case Pass::ClearCurrent: return "clear_current.comp";
    case Pass::FillUnits: return "fill_units.comp";
    case Pass::BuildField: return "build_field.comp";
    case Pass::Propose: return "propose.comp";
    case Pass::ClearReservation: return "clear_reservation.comp";
    case Pass::Reserve: return "reserve.comp";
    case Pass::Validate: return "validate.comp";
    case Pass::Propagate: return "propagate.comp";
    case Pass::PrepareNext: return "prepare_next.comp";
    case Pass::Commit: return "commit.comp";
    case Pass::WriteDecisions: return "write_decisions.comp";
    default: return "";
    }
}

std::uint32_t crc32(const unsigned char* data, std::size_t size) {
    std::uint32_t result = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < size; ++i) {
        result ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            result = (result >> 1u) ^ (0xEDB88320u &
                static_cast<std::uint32_t>(-static_cast<int>(result & 1u)));
    }
    return ~result;
}
void append_be32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value >> 24u));
    out.push_back(static_cast<unsigned char>(value >> 16u));
    out.push_back(static_cast<unsigned char>(value >> 8u));
    out.push_back(static_cast<unsigned char>(value));
}
void append_chunk(std::vector<unsigned char>& png, const char type[4],
                  const std::vector<unsigned char>& payload) {
    append_be32(png, static_cast<std::uint32_t>(payload.size()));
    const std::size_t crc_start = png.size();
    png.insert(png.end(), type, type + 4);
    png.insert(png.end(), payload.begin(), payload.end());
    append_be32(png, crc32(png.data() + crc_start, png.size() - crc_start));
}
bool write_rgba_png(const std::filesystem::path& path, int width, int height,
                    const std::vector<unsigned char>& rgba) {
    if (width <= 0 || height <= 0 ||
        rgba.size() != static_cast<std::size_t>(width) * height * 4u) return false;
    std::vector<unsigned char> scanlines;
    scanlines.reserve((static_cast<std::size_t>(width) * 4u + 1u) * height);
    for (int y = height - 1; y >= 0; --y) {
        scanlines.push_back(0); // PNG filter: None
        const auto begin = rgba.begin() + static_cast<std::size_t>(y) * width * 4u;
        scanlines.insert(scanlines.end(), begin, begin + width * 4u);
    }
    std::vector<unsigned char> zlib{0x78, 0x01};
    std::size_t offset = 0;
    while (offset < scanlines.size()) {
        const std::size_t count = std::min<std::size_t>(65535, scanlines.size() - offset);
        zlib.push_back(offset + count == scanlines.size() ? 1 : 0);
        zlib.push_back(static_cast<unsigned char>(count));
        zlib.push_back(static_cast<unsigned char>(count >> 8u));
        const std::uint16_t inverse = static_cast<std::uint16_t>(~count);
        zlib.push_back(static_cast<unsigned char>(inverse));
        zlib.push_back(static_cast<unsigned char>(inverse >> 8u));
        zlib.insert(zlib.end(), scanlines.begin() + offset,
                    scanlines.begin() + offset + count);
        offset += count;
    }
    std::uint32_t a = 1, b = 0;
    for (unsigned char byte : scanlines) { a = (a + byte) % 65521u; b = (b + a) % 65521u; }
    append_be32(zlib, (b << 16u) | a);
    std::vector<unsigned char> png{137,80,78,71,13,10,26,10};
    std::vector<unsigned char> ihdr;
    append_be32(ihdr, static_cast<std::uint32_t>(width));
    append_be32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
    append_chunk(png, "IHDR", ihdr);
    append_chunk(png, "IDAT", zlib);
    append_chunk(png, "IEND", {});
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(png.data()),
                 static_cast<std::streamsize>(png.size()));
    return stream.good();
}

struct DumpJob {
    std::filesystem::path root;
    std::uint64_t tick = 0;
    int width = 0;
    int height = 0;
    std::vector<std::uint16_t> pixels;
};

void write_dump(const DumpJob& job) {
    std::error_code ec;
    std::filesystem::create_directories(job.root, ec);
    const std::string prefix = "tick_" + std::to_string(job.tick);
    {
        std::ofstream raw(job.root / (prefix + ".rg16ui.bin"), std::ios::binary);
        raw.write(reinterpret_cast<const char*>(job.pixels.data()),
            static_cast<std::streamsize>(job.pixels.size() * sizeof(std::uint16_t)));
    }
    std::vector<unsigned char> occupancy(
        static_cast<std::size_t>(job.width) * job.height * 4u, 255u);
    std::vector<unsigned char> vectors(occupancy.size(), 255u);
    for (std::size_t i = 0; i < job.pixels.size() / 2u; ++i) {
        const std::uint16_t owner = job.pixels[i * 2u];
        const std::uint16_t packed = job.pixels[i * 2u + 1u];
        unsigned char* oc = occupancy.data() + i * 4u;
        if (owner == 0) oc[0] = oc[1] = oc[2] = 0;
        else if (owner >= FirstStaticId) oc[0] = oc[1] = oc[2] = 180;
        else {
            std::uint32_t hash = owner * 2654435761u;
            oc[0] = static_cast<unsigned char>(64u + (hash & 127u));
            oc[1] = static_cast<unsigned char>(64u + ((hash >> 8u) & 127u));
            oc[2] = static_cast<unsigned char>(64u + ((hash >> 16u) & 127u));
        }
        unsigned char* vc = vectors.data() + i * 4u;
        const float angle = static_cast<float>(packed & 255u) * 6.28318530718f / 255.f;
        const float speed = static_cast<float>(packed >> 8u) / 255.f;
        vc[0] = static_cast<unsigned char>((std::cos(angle) * .5f + .5f) * 255.f);
        vc[1] = static_cast<unsigned char>((std::sin(angle) * .5f + .5f) * 255.f);
        vc[2] = static_cast<unsigned char>(speed * 255.f);
    }
    write_rgba_png(job.root / (prefix + "_occupancy.png"),
                   job.width, job.height, occupancy);
    write_rgba_png(job.root / (prefix + "_vector.png"),
                   job.width, job.height, vectors);
}

class DumpWriter {
public:
    DumpWriter() = default;
    ~DumpWriter() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        condition_.notify_all();
        if (worker_.joinable()) worker_.join();
    }
    void push(DumpJob job) {
        {
            std::lock_guard lock(mutex_);
            if (!worker_.joinable()) worker_ = std::thread([this] { run(); });
            jobs_.push(std::move(job));
        }
        condition_.notify_one();
    }
private:
    void run() {
        for (;;) {
            DumpJob job;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty()) { if (stopping_) return; else continue; }
                job = std::move(jobs_.front()); jobs_.pop();
            }
            write_dump(job);
        }
    }
    std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<DumpJob> jobs_;
    bool stopping_ = false;
    std::thread worker_;
};

struct HandleRecord { std::uint16_t handle = 0; std::uint16_t generation = 0; };
struct DumpSlot {
    GLuint pbo = 0;
    GLsync fence = nullptr;
    std::uint64_t tick = 0;
    int width = 0;
    int height = 0;
    bool pending = false;
};

class GpuMotionRuntime {
public:
    explicit GpuMotionRuntime(std::string shader_root)
        : shader_root_(std::move(shader_root)) {}
    ~GpuMotionRuntime() { shutdown(nullptr); }

    void shutdown(EcsWorld* world) {
        if (released_) return;
        released_ = true;
        if (initialized_ && glDeleteBuffers && glDeleteTextures) {
            for (auto& slot : dumps_) consume_dump(slot, true);
            if (unit_buffer_) glDeleteBuffers(1, &unit_buffer_);
            if (proposal_buffer_) glDeleteBuffers(1, &proposal_buffer_);
            if (decision_buffer_) glDeleteBuffers(1, &decision_buffer_);
            if (handle_map_buffer_) glDeleteBuffers(1, &handle_map_buffer_);
            glDeleteTextures(2, state_images_.data());
            if (shared_field_) glDeleteTextures(1, &shared_field_);
            if (reservation_) glDeleteTextures(1, &reservation_);
            for (auto& slot : dumps_) if (slot.pbo) glDeleteBuffers(1, &slot.pbo);
        }
        unit_buffer_ = proposal_buffer_ = decision_buffer_ = handle_map_buffer_ = 0;
        state_images_ = {0, 0}; shared_field_ = reservation_ = 0;
        for (auto& slot : dumps_) { slot.pbo = 0; slot.pending = false; }
        for (auto& value : programs_) value = {};
        if (world)
            if (auto* manager = world->try_resource<AssetManager>())
                manager->store<ComputeProgram>().gc(0.0);
    }

    bool initialize(EcsWorld& world, std::string& error) {
        if (initialized_) return available_;
        initialized_ = true;
        if (!GLAD_GL_VERSION_4_3 || !glDispatchCompute || !glBindImageTexture ||
            !glMemoryBarrier) {
            error = "OpenGL 4.3 compute/image load-store is unavailable";
            return false;
        }
        auto* server = world.try_resource<AssetServer>();
        if (!server) { error = "AssetServer is unavailable"; return false; }
        for (std::size_t i = 0; i < programs_.size(); ++i) {
            const auto pass = static_cast<Pass>(i);
            programs_[i] = server->load_sync(ComputeProgramDesc(
                shader_root_ + "/" + pass_file(pass)),
                AssetLoadOptions{.gc_policy = GcPolicyId::RefCountZero});
            if (!programs_[i].valid()) {
                error = std::string("failed to load compute shader: ") + pass_file(pass);
                return false;
            }
        }
        glGenBuffers(1, &unit_buffer_);
        glGenBuffers(1, &proposal_buffer_);
        glGenBuffers(1, &decision_buffer_);
        glGenBuffers(1, &handle_map_buffer_);
        handle_generations_.resize(FirstStaticId, 0);
        handle_to_index_.resize(FirstStaticId, InvalidIndex);
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        const auto dump_path = environment_value("GLD_AOE_GPU_MAP_DUMP");
        if (!dump_path.empty()) {
            dump_root_ = std::filesystem::path(dump_path);
            std::array<GLuint, 3> pbos{};
            glGenBuffers(static_cast<GLsizei>(pbos.size()), pbos.data());
            for (std::size_t i = 0; i < pbos.size(); ++i) dumps_[i].pbo = pbos[i];
        }
#endif
        available_ = true;
        return true;
    }

    bool tick(EcsWorld& world, std::uint64_t tick, std::string& error) {
        auto& public_diag = world.resource_or_add<AoeGpuMotionDiagnostics>();
        if (!initialize(world, error)) {
            public_diag.available = false;
            public_diag.unavailable_reason = error;
            return false;
        }
        auto* map = world.try_resource<AoeLogicMap>();
        if (!map || !map->valid()) { error = "AoeLogicMap is unavailable"; return false; }
        // 后端只把本 tick 自己产生的 GL error 视为失败，避免替其它 render pass 背锅。
        while (glGetError() != GL_NO_ERROR) {}
        const auto total_started = std::chrono::steady_clock::now();
        if (!ensure_map(*map, error)) return false;
        if (!gather_units(world, tick, error)) return false;
        const auto upload_started = std::chrono::steady_clock::now();
        upload_buffers();
        const auto dispatch_started = std::chrono::steady_clock::now();
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        download_ms_ = 0.0;
#endif
        dispatch_all();
        const auto readback_started = std::chrono::steady_clock::now();
        read_decisions(world, tick);
        std::swap(current_index_, next_index_); // 完整提交与同 tick 回读后才能交换。
        queue_dump(tick);

        public_diag.available = true;
        public_diag.unavailable_reason.clear();
        public_diag.map_width_pixels = width_;
        public_diag.map_height_pixels = height_;
        public_diag.active_units = active_unit_count_;
        public_diag.authoritative_corrections = world.resource_or_add<
            AoeGlobalMotionPlannerDiagnostics>().authoritative_corrections;
        ++public_diag.fixed_ticks;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        public_diag.upload_ms = std::chrono::duration<double, std::milli>(
            dispatch_started - upload_started).count();
        public_diag.dispatch_ms = std::max(0.0,
            std::chrono::duration<double, std::milli>(
                readback_started - dispatch_started).count() - download_ms_);
        public_diag.readback_ms = download_ms_ +
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - readback_started).count();
        public_diag.last_tick_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - total_started).count();
#else
        (void)total_started; (void)upload_started; (void)dispatch_started;
        (void)readback_started;
#endif
        const GLenum gl_error = glGetError();
        if (gl_error != GL_NO_ERROR) {
            error = "OpenGL error after GPU motion solve: " + std::to_string(gl_error);
            available_ = false;
            public_diag.available = false;
            public_diag.unavailable_reason = error;
            return false;
        }
        return true;
    }

private:
    ComputeProgram& program(Pass pass) { return *programs_[static_cast<std::size_t>(pass)]; }
    void use(Pass pass) {
        program(pass).use();
        const GLuint id = program(pass).id();
        if (const GLint location = glGetUniformLocation(id, "image_size"); location >= 0)
            glUniform2i(location, static_cast<GLint>(width_), static_cast<GLint>(height_));
        if (const GLint location = glGetUniformLocation(id, "unit_count"); location >= 0)
            glUniform1ui(location, static_cast<GLuint>(units_.size()));
        if (const GLint location = glGetUniformLocation(id, "map_origin"); location >= 0)
            glUniform2f(location, map_origin_.x, map_origin_.y);
        if (const GLint location = glGetUniformLocation(id, "pixels_per_world_unit"); location >= 0)
            glUniform1f(location, PixelsPerWorldUnit);
        if (const GLint location = glGetUniformLocation(id, "candidate_round"); location >= 0)
            glUniform1ui(location, candidate_round_);
    }
    void dispatch_pixels(Pass pass) {
        use(pass); glDispatchCompute((width_ + 15u) / 16u, (height_ + 15u) / 16u, 1);
    }
    void dispatch_units(Pass pass) {
        use(pass); if (!units_.empty()) glDispatchCompute(
            (static_cast<GLuint>(units_.size()) + 63u) / 64u, 1, 1);
    }

    bool ensure_map(const AoeLogicMap& map, std::string&) {
        const std::uint32_t width = static_cast<std::uint32_t>(std::ceil(
            map.width() * map.tile_size() * PixelsPerWorldUnit));
        const std::uint32_t height = static_cast<std::uint32_t>(std::ceil(
            map.height() * map.tile_size() * PixelsPerWorldUnit));
        const bool recreate = width != width_ || height != height_ ||
                              map.origin() != map_origin_;
        if (recreate) {
            for (auto& slot : dumps_) consume_dump(slot, true);
            width_ = width; height_ = height; map_origin_ = map.origin();
            glDeleteTextures(2, state_images_.data());
            if (shared_field_) glDeleteTextures(1, &shared_field_);
            if (reservation_) glDeleteTextures(1, &reservation_);
            glGenTextures(2, state_images_.data());
            for (GLuint texture : state_images_) {
                glBindTexture(GL_TEXTURE_2D, texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16UI, width_, height_, 0,
                             GL_RG_INTEGER, GL_UNSIGNED_SHORT, nullptr);
            }
            glGenTextures(1, &shared_field_);
            glBindTexture(GL_TEXTURE_2D, shared_field_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            const int levels = 1 + static_cast<int>(std::floor(std::log2(
                static_cast<float>(std::max(width_, height_)))));
            glTexStorage2D(GL_TEXTURE_2D, levels, GL_RGBA16F, width_, height_);
            glGenTextures(1, &reservation_);
            glBindTexture(GL_TEXTURE_2D, reservation_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width_, height_, 0,
                         GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
            current_index_ = 0; next_index_ = 1; static_revision_ = 0;
        }
        if (static_revision_ != map.static_revision()) upload_static(map);
        return true;
    }

    void upload_static(const AoeLogicMap& map) {
        std::vector<std::uint16_t> pixels(
            static_cast<std::size_t>(width_) * height_ * 2u, 0);
        map.visit_static_obstacles([&](AoeObstacleId, const AoeStaticObstacleDesc& obstacle) {
            std::uint16_t id = GenericStaticId;
            std::string source = obstacle.source_id;
            std::transform(source.begin(), source.end(), source.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (source.find("tree") != std::string::npos) id = TreeId;
            else if (source.find("wall") != std::string::npos) id = WallId;
            glm::vec2 half = obstacle.shape == AoeStaticObstacleShape::Circle
                ? glm::vec2(obstacle.radius) : obstacle.half_extents;
            glm::ivec2 lo = glm::max(glm::ivec2(glm::floor(
                (obstacle.center - half - map_origin_) * PixelsPerWorldUnit)), glm::ivec2(0));
            glm::ivec2 hi = glm::min(glm::ivec2(glm::ceil(
                (obstacle.center + half - map_origin_) * PixelsPerWorldUnit)),
                glm::ivec2(width_ - 1u, height_ - 1u));
            for (int y = lo.y; y <= hi.y; ++y) for (int x = lo.x; x <= hi.x; ++x) {
                const glm::vec2 point = map_origin_ +
                    (glm::vec2(x, y) + glm::vec2(.5f)) / PixelsPerWorldUnit;
                bool inside = obstacle.shape == AoeStaticObstacleShape::Circle
                    ? glm::dot(point - obstacle.center, point - obstacle.center) <=
                        obstacle.radius * obstacle.radius
                    : glm::all(glm::lessThanEqual(glm::abs(point - obstacle.center), obstacle.half_extents));
                if (inside) pixels[(static_cast<std::size_t>(y) * width_ + x) * 2u] = id;
            }
        });
        for (GLuint texture : state_images_) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_,
                            GL_RG_INTEGER, GL_UNSIGNED_SHORT, pixels.data());
        }
        static_revision_ = map.static_revision();
    }

    HandleRecord acquire_handle(std::uint64_t instance_id) {
        if (const auto it = handles_.find(instance_id); it != handles_.end()) return it->second;
        std::uint16_t handle = 0;
        if (!free_handles_.empty()) { handle = free_handles_.back(); free_handles_.pop_back(); }
        else if (next_handle_ <= MaximumUnitHandle) handle = static_cast<std::uint16_t>(next_handle_++);
        if (!handle) return {};
        std::uint16_t generation = ++handle_generations_[handle];
        if (!generation) generation = ++handle_generations_[handle];
        return handles_.emplace(instance_id, HandleRecord{handle, generation}).first->second;
    }

    bool gather_units(EcsWorld& world, std::uint64_t tick, std::string& error) {
        units_.clear(); entities_.clear(); live_instances_.clear();
        active_unit_count_ = 0;
        std::fill(handle_to_index_.begin(), handle_to_index_.end(), InvalidIndex);
        auto& reg = world.reg();
        auto& index = world.resource_or_add<AoeUnitFlowIndex>();
        auto& diag = world.resource_or_add<AoeGameplayDiagnostics>();
        index.records.clear(); index.candidates.clear(); index.selected.clear(); index.maximum_reach = 0.f;
        for (auto entity : reg.view<AoeGlobalMotionDecision>())
            reg.get<AoeGlobalMotionDecision>(entity).valid = false;
        const float dt = static_cast<float>(world.resource<AoeGameplaySettings>().fixed_dt);
        for (auto entity : reg.view<AoePosition, AoeCollider,
                                    AoeGameplayIdentity, AoeTeam>(
                 entt::exclude<AoePooledUnit, AoeRecyclePending>)) {
            const auto* intent = reg.try_get<AoeMovementIntent>(entity);
            const bool active = intent && intent->valid &&
                intent->produced_tick == tick &&
                glm::length(intent->velocity) > .00001f;
            const auto instance = reg.get<AoeGameplayIdentity>(entity).instance_id;
            const HandleRecord handle = acquire_handle(instance);
            if (!handle.handle) { error = "GPU unit handle space (0x0001-0xEFFF) exhausted"; return false; }
            live_instances_.insert(instance);
            const auto& position = reg.get<AoePosition>(entity);
            const auto& collider = reg.get<AoeCollider>(entity);
            const auto& state = reg.get_or_emplace<AoeGlobalMotionState>(entity);
            GpuUnitData unit{};
            unit.position_radii[0] = position.value.x; unit.position_radii[1] = position.value.y;
            unit.position_radii[2] = collider.radius_x; unit.position_radii[3] = collider.radius_y;
            const glm::vec2 velocity = active ? intent->velocity : glm::vec2{0.f};
            unit.intent_dt[0] = velocity.x; unit.intent_dt[1] = velocity.y;
            unit.intent_dt[2] = dt; unit.intent_dt[3] = glm::length(velocity);
            unit.identity_wait[0] = handle.handle; unit.identity_wait[1] = handle.generation;
            unit.identity_wait[2] = state.wait_ticks;
            unit.identity_wait[3] = active ? 1u : 0u;
            handle_to_index_[handle.handle] = static_cast<std::uint32_t>(units_.size());
            units_.push_back(unit); entities_.push_back(entity);
            if (active) {
                ++active_unit_count_;
                entt::entity squad = entt::null;
                if (auto* member = reg.try_get<AoeSquadMember>(entity)) squad = member->squad;
                index.records.push_back({entity, instance, squad, reg.get<AoeTeam>(entity).id,
                    intent->kind, position.value, {collider.radius_x, collider.radius_y}, velocity});
                index.maximum_reach = std::max(index.maximum_reach,
                    std::max(collider.radius_x, collider.radius_y) + glm::length(velocity));
                reg.emplace_or_replace<AoeGlobalMotionDecision>(entity,
                    AoeGlobalMotionDecision{.velocity = velocity,
                        .produced_tick = tick, .valid = true});
                if (intent->locally_infeasible) ++diag.flow_infeasible_assignments;
            }
        }
        for (auto it = handles_.begin(); it != handles_.end();) {
            if (!live_instances_.contains(it->first)) {
                free_handles_.push_back(it->second.handle); it = handles_.erase(it);
            } else ++it;
        }
        diag.flow_active_intents += active_unit_count_;
        return true;
    }

    void upload_buffers() {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, unit_buffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, units_.size() * sizeof(GpuUnitData),
                     units_.empty() ? nullptr : units_.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, proposal_buffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, units_.size() * sizeof(GpuProposal), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, decision_buffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, units_.size() * sizeof(GpuDecision), nullptr, GL_STREAM_READ);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, handle_map_buffer_);
        glBufferData(GL_SHADER_STORAGE_BUFFER, handle_to_index_.size() * sizeof(std::uint32_t),
                     handle_to_index_.data(), GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, unit_buffer_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, proposal_buffer_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, decision_buffer_);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, handle_map_buffer_);
    }

    void dispatch_all() {
        glBindImageTexture(0, state_images_[current_index_], 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_RG16UI);
        glBindImageTexture(1, shared_field_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
        glBindImageTexture(2, reservation_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(3, state_images_[next_index_], 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_RG16UI);
        dispatch_pixels(Pass::ClearCurrent);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        dispatch_units(Pass::FillUnits);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        dispatch_pixels(Pass::BuildField);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, shared_field_);
        glGenerateMipmap(GL_TEXTURE_2D);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);
        for (int round = 0; round < ReservationRounds; ++round) {
            candidate_round_ = static_cast<std::uint32_t>(round);
            dispatch_units(Pass::Propose);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            dispatch_pixels(Pass::ClearReservation);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            dispatch_units(Pass::Reserve);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            dispatch_units(Pass::Validate);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            // Direct round 的原始意向依赖必须在尝试其它方向前求解；否则下一轮
            // propose 会覆盖本可通过“阻挡者先移动”解决的队列提案。
            if (round == 0) {
                for (int i = 0; i < DependencyPasses; ++i) {
                    dispatch_units(Pass::Propagate);
                    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                }
            }
        }
        dispatch_pixels(Pass::PrepareNext);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        dispatch_units(Pass::Commit);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        dispatch_units(Pass::WriteDecisions);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
    }

    void download_decisions() {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        const auto started = std::chrono::steady_clock::now();
#endif
        decisions_.resize(units_.size());
        if (!decisions_.empty()) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, decision_buffer_);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                decisions_.size() * sizeof(GpuDecision), decisions_.data());
        }
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        download_ms_ += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
#endif
    }

    void read_decisions(EcsWorld& world, std::uint64_t tick) {
        download_decisions();
        auto& reg = world.reg();
        auto& gameplay_diag = world.resource_or_add<AoeGameplayDiagnostics>();
        for (std::size_t i = 0; i < decisions_.size(); ++i) {
            const auto& gpu = decisions_[i];
            if (gpu.identity[0] != units_[i].identity_wait[0] ||
                gpu.identity[1] != units_[i].identity_wait[1]) continue;
            if (units_[i].identity_wait[3] == 0u) continue;
            auto& decision = reg.get<AoeGlobalMotionDecision>(entities_[i]);
            decision.velocity = {gpu.velocity[0], gpu.velocity[1]};
            decision.produced_tick = tick; decision.valid = true;
            auto& state = reg.get_or_emplace<AoeGlobalMotionState>(entities_[i]);
            const bool waiting = gpu.identity[2] != 2u && units_[i].intent_dt[3] > .00001f;
            if (waiting) {
                decision.wait_ticks = state.wait_ticks;
                decision.mode = state.wait_ticks >= 45u
                    ? AoeGlobalMotionMode::Backing : AoeGlobalMotionMode::Yielding;
                decision.reason = state.wait_ticks >= 45u
                    ? AoeMotionDecisionReason::DeadlockEscape
                    : AoeMotionDecisionReason::SideBlocked;
                ++gameplay_diag.flow_wait_ticks;
                if (decision.mode == AoeGlobalMotionMode::Backing) ++gameplay_diag.flow_backing;
                else ++gameplay_diag.flow_yielding;
            } else {
                decision.wait_ticks = state.wait_ticks;
                decision.mode = AoeGlobalMotionMode::Clear;
            }
            state.mode = decision.mode;
        }
    }

    void consume_dump(DumpSlot& slot, bool wait) {
        if (!slot.pending) return;
        GLenum status = glClientWaitSync(slot.fence,
            wait ? GL_SYNC_FLUSH_COMMANDS_BIT : 0, wait ? 1000000000ull : 0ull);
        if (!wait && status == GL_TIMEOUT_EXPIRED) return;
        while (wait && status == GL_TIMEOUT_EXPIRED)
            status = glClientWaitSync(slot.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        glDeleteSync(slot.fence); slot.fence = nullptr;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
        const std::size_t bytes = static_cast<std::size_t>(slot.width) * slot.height * 4u;
        void* mapped = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT);
        if (mapped) {
            DumpJob job{dump_root_, slot.tick, slot.width, slot.height,
                std::vector<std::uint16_t>(bytes / 2u)};
            std::memcpy(job.pixels.data(), mapped, bytes);
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            dump_writer_.push(std::move(job));
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0); slot.pending = false;
    }
    void queue_dump(std::uint64_t tick) {
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        if (dump_root_.empty()) return;
        for (auto& slot : dumps_) consume_dump(slot, false);
        DumpSlot& slot = dumps_[dump_cursor_++ % dumps_.size()];
        consume_dump(slot, true); // 环满时等待，绝不静默丢 fixed tick。
        const std::size_t bytes = static_cast<std::size_t>(width_) * height_ * 4u;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
        glBindTexture(GL_TEXTURE_2D, state_images_[current_index_]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RG_INTEGER, GL_UNSIGNED_SHORT, nullptr);
        slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        slot.tick = tick; slot.width = width_; slot.height = height_; slot.pending = true;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
#else
        (void)tick;
#endif
    }

    std::string shader_root_;
    bool initialized_ = false;
    bool available_ = false;
    bool released_ = false;
    std::array<Handle<ComputeProgram>, static_cast<std::size_t>(Pass::Count)> programs_;
    std::array<GLuint, 2> state_images_{0, 0};
    GLuint shared_field_ = 0, reservation_ = 0;
    GLuint unit_buffer_ = 0, proposal_buffer_ = 0, decision_buffer_ = 0,
           handle_map_buffer_ = 0;
    std::uint32_t width_ = 0, height_ = 0;
    std::uint32_t active_unit_count_ = 0;
    glm::vec2 map_origin_{std::numeric_limits<float>::max()};
    std::uint64_t static_revision_ = 0;
    int current_index_ = 0, next_index_ = 1;
    std::uint32_t candidate_round_ = 0;
    std::uint32_t next_handle_ = 1;
    std::unordered_map<std::uint64_t, HandleRecord> handles_;
    std::vector<std::uint16_t> free_handles_, handle_generations_;
    std::vector<std::uint32_t> handle_to_index_;
    std::unordered_set<std::uint64_t> live_instances_;
    std::vector<GpuUnitData> units_;
    std::vector<GpuDecision> decisions_;
    std::vector<entt::entity> entities_;
    std::filesystem::path dump_root_;
    std::array<DumpSlot, 3> dumps_{};
    std::size_t dump_cursor_ = 0;
    DumpWriter dump_writer_;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
    double download_ms_ = 0.0;
#endif
};

} // namespace

void AoeGpuMotionPlugin::operator()(App& app) const {
    auto runtime = std::make_shared<GpuMotionRuntime>(shader_root);
    auto& planners = app.world.resource_or_add<AoeGlobalMotionPlannerRegistry>();
    planners.bind("gpu_image",
        [runtime](EcsWorld& world, std::uint64_t tick, std::string& error) {
            return runtime->tick(world, tick, error);
        });
    app.world.resource_or_add<AoeGpuMotionDiagnostics>();
    app.add_system(Stage::Shutdown, [runtime](EcsWorld& world) {
        runtime->shutdown(&world);
        if (auto* planners = world.try_resource<AoeGlobalMotionPlannerRegistry>())
            planners->erase("gpu_image");
    });
}

} // namespace gld::ecs::aoe

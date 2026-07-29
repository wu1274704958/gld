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
#include <vector>

#include <ecs/PerformanceMonitoring.hpp>
#include <ecs/assets/AssetServer.hpp>

namespace gld::ecs::aoe {
namespace {

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

struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool empty() const { return width <= 0 || height <= 0; }
};

PixelRect unite(PixelRect a, PixelRect b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    const int x0 = std::min(a.x, b.x);
    const int y0 = std::min(a.y, b.y);
    const int x1 = std::max(a.x + a.width, b.x + b.width);
    const int y1 = std::max(a.y + a.height, b.y + b.height);
    return {x0, y0, x1 - x0, y1 - y0};
}

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

struct HandleRecord {
    std::uint16_t handle = 0;
    std::uint16_t generation = 0;
};
struct GpuMotionHandleBinding {
    std::uint64_t instance_id = 0;
    std::uint64_t last_seen_tick = 0;
    std::uint16_t handle = 0;
    std::uint16_t generation = 0;
};
struct DumpSlot {
    GLuint pbo = 0;
    GLsync fence = nullptr;
    std::uint64_t tick = 0;
    int width = 0;
    int height = 0;
    bool pending = false;
};

struct MotionBufferSlot {
    GLuint unit = 0;
    GLuint proposal = 0;
    GLuint decision = 0;
    GLuint handle_map = 0;
    GLuint readback = 0;
    GLsync fence = nullptr;
    std::size_t unit_capacity = 0;
    std::size_t proposal_capacity = 0;
    std::size_t decision_capacity = 0;
    std::size_t handle_capacity = 0;
    std::size_t readback_capacity = 0;
    std::uint64_t tick = 0;
    bool pending = false;
    std::vector<GpuUnitData> units;
    std::vector<entt::entity> entities;
    std::vector<std::uint64_t> instances;
};

struct UniformLocations {
    GLint image_size = -1;
    GLint unit_count = -1;
    GLint map_origin = -1;
    GLint pixels_per_world_unit = -1;
    GLint candidate_round = -1;
    GLint dispatch_origin = -1;
    GLint dispatch_size = -1;
};

class GpuMotionRuntime {
public:
    explicit GpuMotionRuntime(std::string shader_root,
                              float pixels_per_world_unit,
                              double result_fence_budget_ms,
                              std::uint32_t solve_interval_ticks)
        : shader_root_(std::move(shader_root)),
          pixels_per_world_unit_(pixels_per_world_unit),
          result_fence_budget_ms_(result_fence_budget_ms),
          solve_interval_ticks_(solve_interval_ticks) {}
    ~GpuMotionRuntime() { shutdown(nullptr); }

    void shutdown(EcsWorld* world) {
        if (released_) return;
        released_ = true;
        if (initialized_ && glDeleteBuffers && glDeleteTextures) {
            for (auto& slot : dumps_) consume_dump(slot, true);
            for (auto& slot : motion_slots_) {
                if (slot.fence) glDeleteSync(slot.fence);
                const std::array buffers{slot.unit, slot.proposal,
                    slot.decision, slot.handle_map, slot.readback};
                glDeleteBuffers(static_cast<GLsizei>(buffers.size()),
                                buffers.data());
                slot = {};
            }
            glDeleteTextures(2, state_images_.data());
            if (shared_field_) glDeleteTextures(1, &shared_field_);
            if (reservation_) glDeleteTextures(1, &reservation_);
            for (auto& slot : dumps_) if (slot.pbo) glDeleteBuffers(1, &slot.pbo);
        }
        state_images_ = {0, 0}; shared_field_ = reservation_ = 0;
        for (auto& slot : dumps_) { slot.pbo = 0; slot.pending = false; }
        for (auto& value : programs_) value = {};
        if (world) {
            auto& reg = world->reg();
            std::vector<entt::entity> bindings;
            for (const auto entity : reg.view<GpuMotionHandleBinding>())
                bindings.push_back(entity);
            for (const auto entity : bindings)
                if (reg.valid(entity))
                    reg.remove<GpuMotionHandleBinding>(entity);
            if (auto* manager = world->try_resource<AssetManager>())
                manager->store<ComputeProgram>().gc(0.0);
        }
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
            const GLuint id = programs_[i]->id();
            uniforms_[i] = {
                glGetUniformLocation(id, "image_size"),
                glGetUniformLocation(id, "unit_count"),
                glGetUniformLocation(id, "map_origin"),
                glGetUniformLocation(id, "pixels_per_world_unit"),
                glGetUniformLocation(id, "candidate_round"),
                glGetUniformLocation(id, "dispatch_origin"),
                glGetUniformLocation(id, "dispatch_size")};
        }
        for (auto& slot : motion_slots_) {
            std::array<GLuint, 5> buffers{};
            glGenBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
            slot.unit = buffers[0];
            slot.proposal = buffers[1];
            slot.decision = buffers[2];
            slot.handle_map = buffers[3];
            slot.readback = buffers[4];
        }
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
        const bool solve_demanded = evaluate_solve_requirement(world, tick);
        solve_required_ = solve_demanded &&
            tick % solve_interval_ticks_ == 0u;
        public_diag.solve_required = solve_required_;
        if (solve_required_) ++public_diag.solve_required_ticks;
        if (solve_required_) {
            if (!gather_units(world, tick, error)) return false;
        } else {
            gather_raw_motion(world, tick);
        }
        const auto readback_started = std::chrono::steady_clock::now();
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        download_ms_ = 0.0;
#endif
        if (tick > 0)
            consume_motion_result(
                motion_slots_[(tick - 1u) % motion_slots_.size()],
                world, tick, public_diag);
        bool gpu_in_flight = false;
        for (auto& pending : motion_slots_) {
            retire_motion_slot(pending, false);
            gpu_in_flight = gpu_in_flight || pending.pending;
        }
        auto& slot = motion_slots_[tick % motion_slots_.size()];
        const auto upload_started = std::chrono::steady_clock::now();
        const auto dispatch_started = std::chrono::steady_clock::now();
        if (solve_required_ && !gpu_in_flight) {
            upload_buffers(slot);
            dispatch_all();
            queue_motion_result(slot, tick);
            ++public_diag.submissions_queued;
            // OpenGL command ordering guarantees the newly queued current/next
            // image work completes before the next dispatch that consumes it;
            // CPU readback no longer needs to serialize this swap.
            std::swap(current_index_, next_index_);
            queue_dump(tick);
        } else if (solve_required_) {
            // Catch-up fixed ticks can occur back-to-back with no GPU time in
            // between. Do not build an unbounded queue of results that would
            // already be stale when read; this tick keeps the raw intent and
            // still passes through authoritative CPU motion safety.
            ++public_diag.async_submissions_skipped;
        }

        public_diag.available = true;
        public_diag.unavailable_reason.clear();
        public_diag.map_width_pixels = width_;
        public_diag.map_height_pixels = height_;
        public_diag.active_width_pixels =
            static_cast<std::uint32_t>(std::max(0, active_rect_.width));
        public_diag.active_height_pixels =
            static_cast<std::uint32_t>(std::max(0, active_rect_.height));
        public_diag.active_units = active_unit_count_;
        public_diag.authoritative_corrections = world.resource_or_add<
            AoeGlobalMotionPlannerDiagnostics>().authoritative_corrections;
        ++public_diag.fixed_ticks;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        public_diag.upload_ms = std::chrono::duration<double, std::milli>(
            dispatch_started - upload_started).count();
        public_diag.dispatch_ms = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - dispatch_started).count();
        public_diag.readback_ms = download_ms_;
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
        const auto& location = uniforms_[static_cast<std::size_t>(pass)];
        if (location.image_size >= 0)
            glUniform2i(location.image_size, static_cast<GLint>(width_),
                        static_cast<GLint>(height_));
        if (location.unit_count >= 0)
            glUniform1ui(location.unit_count,
                         static_cast<GLuint>(units_.size()));
        if (location.map_origin >= 0)
            glUniform2f(location.map_origin, map_origin_.x, map_origin_.y);
        if (location.pixels_per_world_unit >= 0)
            glUniform1f(location.pixels_per_world_unit,
                        pixels_per_world_unit_);
        if (location.candidate_round >= 0)
            glUniform1ui(location.candidate_round, candidate_round_);
        if (location.dispatch_origin >= 0)
            glUniform2i(location.dispatch_origin, dispatch_rect_.x,
                        dispatch_rect_.y);
        if (location.dispatch_size >= 0)
            glUniform2i(location.dispatch_size, dispatch_rect_.width,
                        dispatch_rect_.height);
    }
    void dispatch_pixels(Pass pass, PixelRect rect) {
        if (rect.empty()) return;
        dispatch_rect_ = rect;
        use(pass);
        glDispatchCompute((static_cast<GLuint>(rect.width) + 15u) / 16u,
                          (static_cast<GLuint>(rect.height) + 15u) / 16u, 1);
    }
    void dispatch_units(Pass pass) {
        use(pass); if (!units_.empty()) glDispatchCompute(
            (static_cast<GLuint>(units_.size()) + 63u) / 64u, 1, 1);
    }

    bool ensure_map(const AoeLogicMap& map, std::string&) {
        const std::uint32_t width = static_cast<std::uint32_t>(std::ceil(
            map.width() * map.tile_size() * pixels_per_world_unit_));
        const std::uint32_t height = static_cast<std::uint32_t>(std::ceil(
            map.height() * map.tile_size() * pixels_per_world_unit_));
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
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, width_, height_);
            glGenTextures(1, &reservation_);
            glBindTexture(GL_TEXTURE_2D, reservation_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, width_, height_, 0,
                         GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
            current_index_ = 0; next_index_ = 1; static_revision_ = 0;
            state_dirty_ = {};
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
                (obstacle.center - half - map_origin_) * pixels_per_world_unit_)), glm::ivec2(0));
            glm::ivec2 hi = glm::min(glm::ivec2(glm::ceil(
                (obstacle.center + half - map_origin_) * pixels_per_world_unit_)),
                glm::ivec2(width_ - 1u, height_ - 1u));
            for (int y = lo.y; y <= hi.y; ++y) for (int x = lo.x; x <= hi.x; ++x) {
                const glm::vec2 point = map_origin_ +
                    (glm::vec2(x, y) + glm::vec2(.5f)) / pixels_per_world_unit_;
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

    HandleRecord allocate_handle() {
        std::uint16_t handle = 0;
        if (!free_handles_.empty()) { handle = free_handles_.back(); free_handles_.pop_back(); }
        else if (next_handle_ <= MaximumUnitHandle) handle = static_cast<std::uint16_t>(next_handle_++);
        if (!handle) return {};
        std::uint16_t generation = ++handle_generations_[handle];
        if (!generation) generation = ++handle_generations_[handle];
        return {handle, generation};
    }

    bool gather_units(EcsWorld& world, std::uint64_t tick, std::string& error) {
        units_.clear(); entities_.clear(); instance_ids_.clear();
        active_unit_count_ = 0;
        for (const auto handle : mapped_handles_)
            handle_to_index_[handle] = InvalidIndex;
        mapped_handles_.clear();
        auto& reg = world.reg();
        auto& index = world.resource_or_add<AoeUnitFlowIndex>();
        auto& diag = world.resource_or_add<AoeGameplayDiagnostics>();
        index.records.clear(); index.candidates.clear(); index.selected.clear(); index.maximum_reach = 0.f;
        const float dt = static_cast<float>(world.resource<AoeGameplaySettings>().fixed_dt);
        const auto* snapshot = world.try_resource<AoeGameplayTickSnapshot>();
        if (!snapshot || snapshot->tick != tick) {
            error = "fixed-tick unit snapshot is unavailable";
            return false;
        }
        for (const auto& captured : snapshot->units) {
            const auto entity = captured.entity;
            if (!reg.valid(entity)) continue;
            const auto* intent = reg.try_get<AoeMovementIntent>(entity);
            const bool active = intent && intent->valid &&
                intent->produced_tick == tick &&
                glm::length(intent->velocity) > .00001f;
            const auto instance = captured.instance_id;
            auto* binding = reg.try_get<GpuMotionHandleBinding>(entity);
            if (binding && binding->instance_id != instance) {
                free_handles_.push_back(binding->handle);
                reg.remove<GpuMotionHandleBinding>(entity);
                binding = nullptr;
            }
            if (!binding) {
                const auto allocated = allocate_handle();
                if (!allocated.handle) {
                    error = "GPU unit handle space (0x0001-0xEFFF) exhausted";
                    return false;
                }
                binding = &reg.emplace<GpuMotionHandleBinding>(entity,
                    GpuMotionHandleBinding{instance, tick,
                        allocated.handle, allocated.generation});
            } else {
                binding->last_seen_tick = tick;
            }
            const HandleRecord handle{binding->handle, binding->generation};
            if (!handle.handle) { error = "GPU unit handle space (0x0001-0xEFFF) exhausted"; return false; }
            const auto& state = reg.get_or_emplace<AoeGlobalMotionState>(entity);
            GpuUnitData unit{};
            unit.position_radii[0] = captured.position.x;
            unit.position_radii[1] = captured.position.y;
            unit.position_radii[2] = captured.radii.x;
            unit.position_radii[3] = captured.radii.y;
            const glm::vec2 velocity = active ? intent->velocity : glm::vec2{0.f};
            unit.intent_dt[0] = velocity.x; unit.intent_dt[1] = velocity.y;
            unit.intent_dt[2] = dt; unit.intent_dt[3] = glm::length(velocity);
            unit.identity_wait[0] = handle.handle; unit.identity_wait[1] = handle.generation;
            unit.identity_wait[2] = state.wait_ticks;
            unit.identity_wait[3] = active ? 1u : 0u;
            handle_to_index_[handle.handle] = static_cast<std::uint32_t>(units_.size());
            mapped_handles_.push_back(handle.handle);
            units_.push_back(unit);
            entities_.push_back(entity);
            instance_ids_.push_back(instance);
            if (active) {
                ++active_unit_count_;
                index.records.push_back({entity, instance, captured.squad,
                    captured.team_id, intent->kind, captured.position,
                    captured.radii, velocity});
                index.maximum_reach = std::max(index.maximum_reach,
                    std::max(captured.radii.x, captured.radii.y) +
                    glm::length(velocity));
                const AoeGlobalMotionDecision raw{
                    .velocity = velocity,
                    .produced_tick = tick,
                    .valid = true};
                if (auto* decision =
                        reg.try_get<AoeGlobalMotionDecision>(entity))
                    *decision = raw;
                else
                    reg.emplace<AoeGlobalMotionDecision>(entity, raw);
                if (intent->locally_infeasible) ++diag.flow_infeasible_assignments;
            }
        }
        stale_handle_entities_.clear();
        for (const auto entity : reg.view<GpuMotionHandleBinding>()) {
            const auto& binding = reg.get<GpuMotionHandleBinding>(entity);
            if (binding.last_seen_tick == tick) continue;
            free_handles_.push_back(binding.handle);
            stale_handle_entities_.push_back(entity);
        }
        for (const auto entity : stale_handle_entities_)
            if (reg.valid(entity)) reg.remove<GpuMotionHandleBinding>(entity);
        diag.flow_active_intents += active_unit_count_;
        update_active_rect();
        return true;
    }

    bool evaluate_solve_requirement(EcsWorld& world, std::uint64_t tick) {
        const auto* snapshot = world.try_resource<AoeGameplayTickSnapshot>();
        if (!snapshot || snapshot->tick != tick) return true;
        auto& reg = world.reg();
        std::size_t active = 0;
        std::size_t blocked = 0;
        for (const auto& unit : snapshot->units) {
            const auto* intent = reg.try_get<AoeMovementIntent>(unit.entity);
            if (!intent || !intent->valid || intent->produced_tick != tick ||
                glm::length(intent->velocity) <= .00001f)
                continue;
            ++active;
            if (unit.squad == entt::null) return true;
            const auto* state = reg.try_get<AoeGlobalMotionState>(unit.entity);
            if ((state && state->wait_ticks >= 4u) ||
                intent->locally_infeasible)
                ++blocked;
        }
        if (blocked >= std::max<std::size_t>(32u, active / 100u))
            return true;
        constexpr float ConflictMargin = 1.f;
        for (std::size_t i = 0; i < snapshot->squad_bounds.size(); ++i) {
            const auto& a = snapshot->squad_bounds[i];
            const auto* a_team = reg.try_get<AoeTeam>(a.squad);
            if (!a_team) continue;
            for (std::size_t j = i + 1;
                 j < snapshot->squad_bounds.size(); ++j) {
                const auto& b = snapshot->squad_bounds[j];
                const auto* b_team = reg.try_get<AoeTeam>(b.squad);
                if (!b_team || a_team->id == b_team->id) continue;
                const bool overlap = glm::all(glm::greaterThanEqual(
                    a.high + glm::vec2(ConflictMargin), b.low)) &&
                    glm::all(glm::greaterThanEqual(
                        b.high + glm::vec2(ConflictMargin), a.low));
                if (overlap) return true;
            }
        }
        return false;
    }

    void gather_raw_motion(EcsWorld& world, std::uint64_t tick) {
        units_.clear();
        entities_.clear();
        instance_ids_.clear();
        active_unit_count_ = 0;
        auto& reg = world.reg();
        auto& index = world.resource_or_add<AoeUnitFlowIndex>();
        index.records.clear();
        index.candidates.clear();
        index.selected.clear();
        index.maximum_reach = 0.f;
        const auto& snapshot = world.resource<AoeGameplayTickSnapshot>();
        for (const auto& unit : snapshot.units) {
            const auto* intent = reg.try_get<AoeMovementIntent>(unit.entity);
            if (!intent || !intent->valid || intent->produced_tick != tick ||
                glm::length(intent->velocity) <= .00001f)
                continue;
            ++active_unit_count_;
            index.records.push_back({unit.entity, unit.instance_id,
                unit.squad, unit.team_id, intent->kind, unit.position,
                unit.radii, intent->velocity});
            index.maximum_reach = std::max(index.maximum_reach,
                std::max(unit.radii.x, unit.radii.y) +
                glm::length(intent->velocity));
            (void)reg.get_or_emplace<AoeGlobalMotionState>(unit.entity);
            const AoeGlobalMotionDecision raw{
                .velocity = intent->velocity,
                .produced_tick = tick,
                .valid = true};
            if (auto* decision =
                    reg.try_get<AoeGlobalMotionDecision>(unit.entity))
                *decision = raw;
            else
                reg.emplace<AoeGlobalMotionDecision>(unit.entity, raw);
        }
        world.resource_or_add<AoeGameplayDiagnostics>().flow_active_intents +=
            active_unit_count_;
        active_rect_ = {};
    }

    void update_active_rect() {
        if (units_.empty() || width_ == 0 || height_ == 0) {
            active_rect_ = {};
            return;
        }
        int min_x = static_cast<int>(width_);
        int min_y = static_cast<int>(height_);
        int max_x = -1;
        int max_y = -1;
        for (const auto& unit : units_) {
            const glm::vec2 start{unit.position_radii[0],
                                  unit.position_radii[1]};
            const glm::vec2 velocity{unit.intent_dt[0], unit.intent_dt[1]};
            const glm::vec2 end = start + velocity * unit.intent_dt[2];
            const glm::vec2 radii{unit.position_radii[2],
                                  unit.position_radii[3]};
            const glm::vec2 low = (glm::min(start, end) - radii -
                                   map_origin_) * pixels_per_world_unit_;
            const glm::vec2 high = (glm::max(start, end) + radii -
                                    map_origin_) * pixels_per_world_unit_;
            min_x = std::min(min_x, static_cast<int>(std::floor(low.x)));
            min_y = std::min(min_y, static_cast<int>(std::floor(low.y)));
            max_x = std::max(max_x, static_cast<int>(std::ceil(high.x)));
            max_y = std::max(max_y, static_cast<int>(std::ceil(high.y)));
        }
        // build_field.comp samples up to eight pixels away. Two extra pixels
        // cover raster rounding and the next fixed step's reservation edge.
        constexpr int Margin = 10;
        min_x = std::clamp(min_x - Margin, 0, static_cast<int>(width_));
        min_y = std::clamp(min_y - Margin, 0, static_cast<int>(height_));
        max_x = std::clamp(max_x + Margin, -1,
                           static_cast<int>(width_) - 1);
        max_y = std::clamp(max_y + Margin, -1,
                           static_cast<int>(height_) - 1);
        active_rect_ = max_x >= min_x && max_y >= min_y
            ? PixelRect{min_x, min_y, max_x - min_x + 1,
                        max_y - min_y + 1}
            : PixelRect{};
    }

    void upload_buffers(MotionBufferSlot& slot) {
        const auto ensure_capacity = [](GLuint buffer, std::size_t bytes,
                                        std::size_t& capacity, GLenum usage) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
            if (bytes > capacity) {
                // Geometric high-water growth avoids redefining storage every
                // fixed tick while squads spawn or recycle in small batches.
                capacity = std::max(bytes, capacity ? capacity * 2u : bytes);
                glBufferData(GL_SHADER_STORAGE_BUFFER, capacity, nullptr, usage);
            }
        };
        const std::size_t unit_bytes = units_.size() * sizeof(GpuUnitData);
        const std::size_t proposal_bytes = units_.size() * sizeof(GpuProposal);
        const std::size_t decision_bytes = units_.size() * sizeof(GpuDecision);
        const std::size_t handle_bytes =
            handle_to_index_.size() * sizeof(std::uint32_t);

        ensure_capacity(slot.unit, unit_bytes, slot.unit_capacity,
                        GL_DYNAMIC_DRAW);
        if (unit_bytes)
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, unit_bytes,
                            units_.data());
        ensure_capacity(slot.proposal, proposal_bytes,
                        slot.proposal_capacity, GL_DYNAMIC_DRAW);
        ensure_capacity(slot.decision, decision_bytes,
                        slot.decision_capacity, GL_STREAM_COPY);
        ensure_capacity(slot.handle_map, handle_bytes,
                        slot.handle_capacity, GL_DYNAMIC_DRAW);
        if (handle_bytes)
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, handle_bytes,
                            handle_to_index_.data());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, slot.unit);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, slot.proposal);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, slot.decision);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, slot.handle_map);
    }

    void dispatch_all() {
        glBindImageTexture(0, state_images_[current_index_], 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_RG16UI);
        glBindImageTexture(1, shared_field_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
        glBindImageTexture(2, reservation_, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(3, state_images_[next_index_], 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_RG16UI);
        const PixelRect clear_current = unite(
            state_dirty_[current_index_], active_rect_);
        const PixelRect prepare_next = unite(
            state_dirty_[next_index_], active_rect_);
        dispatch_pixels(Pass::ClearCurrent, clear_current);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        dispatch_units(Pass::FillUnits);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        dispatch_pixels(Pass::BuildField, active_rect_);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        for (int round = 0; round < ReservationRounds; ++round) {
            candidate_round_ = static_cast<std::uint32_t>(round);
            dispatch_units(Pass::Propose);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            dispatch_pixels(Pass::ClearReservation, active_rect_);
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
        dispatch_pixels(Pass::PrepareNext, prepare_next);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        dispatch_units(Pass::Commit);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        dispatch_units(Pass::WriteDecisions);
        glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        state_dirty_[next_index_] = active_rect_;
    }

    void retire_motion_slot(MotionBufferSlot& slot, bool wait) {
        if (!slot.pending) return;
        GLenum status = glClientWaitSync(slot.fence,
            wait ? GL_SYNC_FLUSH_COMMANDS_BIT : 0,
            wait ? 1000000000ull : 0ull);
        while (wait && status == GL_TIMEOUT_EXPIRED)
            status = glClientWaitSync(
                slot.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 1000000000ull);
        if (!wait && status == GL_TIMEOUT_EXPIRED) return;
        glDeleteSync(slot.fence);
        slot.fence = nullptr;
        slot.pending = false;
    }

    void queue_motion_result(MotionBufferSlot& slot, std::uint64_t tick) {
        const std::size_t bytes = units_.size() * sizeof(GpuDecision);
        glBindBuffer(GL_COPY_WRITE_BUFFER, slot.readback);
        if (bytes > slot.readback_capacity) {
            slot.readback_capacity = std::max(
                bytes, slot.readback_capacity
                    ? slot.readback_capacity * 2u : bytes);
            glBufferData(GL_COPY_WRITE_BUFFER, slot.readback_capacity,
                         nullptr, GL_STREAM_READ);
        }
        if (bytes) {
            glBindBuffer(GL_COPY_READ_BUFFER, slot.decision);
            glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                                0, 0, bytes);
        }
        slot.units = units_;
        slot.entities = entities_;
        slot.instances = instance_ids_;
        slot.tick = tick;
        slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        slot.pending = true;
    }

    void consume_motion_result(MotionBufferSlot& slot, EcsWorld& world,
                               std::uint64_t tick,
                               AoeGpuMotionDiagnostics& public_diag) {
        if (!slot.pending || slot.tick + 1u != tick) return;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        const auto started = std::chrono::steady_clock::now();
#endif
        const GLuint64 budget_ns = static_cast<GLuint64>(
            std::max(0.0, result_fence_budget_ms_) * 1000000.0);
        const GLenum status = glClientWaitSync(
            slot.fence, GL_SYNC_FLUSH_COMMANDS_BIT, budget_ns);
        if (status == GL_TIMEOUT_EXPIRED) {
            ++public_diag.async_deadline_misses;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
            download_ms_ += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started).count();
#endif
            return;
        }
        if (status == GL_WAIT_FAILED) {
            ++public_diag.async_deadline_misses;
            retire_motion_slot(slot, true);
            return;
        }
        decisions_.resize(slot.units.size());
        if (!decisions_.empty()) {
            glBindBuffer(GL_COPY_WRITE_BUFFER, slot.readback);
            glGetBufferSubData(GL_COPY_WRITE_BUFFER, 0,
                decisions_.size() * sizeof(GpuDecision), decisions_.data());
        }
        auto& reg = world.reg();
        auto& gameplay_diag = world.resource_or_add<AoeGameplayDiagnostics>();
        for (std::size_t i = 0; i < decisions_.size(); ++i) {
            const auto& gpu = decisions_[i];
            if (gpu.identity[0] != slot.units[i].identity_wait[0] ||
                gpu.identity[1] != slot.units[i].identity_wait[1] ||
                slot.units[i].identity_wait[3] == 0u ||
                !reg.valid(slot.entities[i])) continue;
            const auto* identity =
                reg.try_get<AoeGameplayIdentity>(slot.entities[i]);
            const auto* intent =
                reg.try_get<AoeMovementIntent>(slot.entities[i]);
            if (!identity || identity->instance_id != slot.instances[i] ||
                !intent || !intent->valid || intent->produced_tick != tick)
                continue;
            auto* decision_ptr =
                reg.try_get<AoeGlobalMotionDecision>(slot.entities[i]);
            if (!decision_ptr) continue;
            auto& decision = *decision_ptr;
            decision.velocity = {gpu.velocity[0], gpu.velocity[1]};
            decision.produced_tick = tick; decision.valid = true;
            auto& state = reg.get_or_emplace<AoeGlobalMotionState>(
                slot.entities[i]);
            const bool waiting = gpu.identity[2] != 2u &&
                                 slot.units[i].intent_dt[3] > .00001f;
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
        glDeleteSync(slot.fence);
        slot.fence = nullptr;
        slot.pending = false;
        ++public_diag.async_results_applied;
#if defined(GLD_ENABLE_PERFORMANCE_MONITORING)
        download_ms_ += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
#endif
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
    std::array<UniformLocations, static_cast<std::size_t>(Pass::Count)>
        uniforms_{};
    std::array<GLuint, 2> state_images_{0, 0};
    GLuint shared_field_ = 0, reservation_ = 0;
    std::array<MotionBufferSlot, 3> motion_slots_{};
    std::uint32_t width_ = 0, height_ = 0;
    std::uint32_t active_unit_count_ = 0;
    float pixels_per_world_unit_ = 8.f;
    double result_fence_budget_ms_ = 1.0;
    std::uint32_t solve_interval_ticks_ = 2;
    bool solve_required_ = false;
    PixelRect active_rect_{};
    PixelRect dispatch_rect_{};
    std::array<PixelRect, 2> state_dirty_{};
    glm::vec2 map_origin_{std::numeric_limits<float>::max()};
    std::uint64_t static_revision_ = 0;
    int current_index_ = 0, next_index_ = 1;
    std::uint32_t candidate_round_ = 0;
    std::uint32_t next_handle_ = 1;
    std::vector<std::uint16_t> free_handles_, handle_generations_;
    std::vector<std::uint32_t> handle_to_index_;
    std::vector<std::uint16_t> mapped_handles_;
    std::vector<entt::entity> stale_handle_entities_;
    std::vector<GpuUnitData> units_;
    std::vector<GpuDecision> decisions_;
    std::vector<entt::entity> entities_;
    std::vector<std::uint64_t> instance_ids_;
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
    if (!std::isfinite(pixels_per_world_unit) || pixels_per_world_unit <= 0.f)
        throw std::invalid_argument(
            "AoeGpuMotionPlugin requires positive pixels_per_world_unit");
    if (!std::isfinite(result_fence_budget_ms) ||
        result_fence_budget_ms < 0.0)
        throw std::invalid_argument(
            "AoeGpuMotionPlugin requires non-negative result_fence_budget_ms");
    if (solve_interval_ticks == 0)
        throw std::invalid_argument(
            "AoeGpuMotionPlugin requires positive solve_interval_ticks");
    auto runtime = std::make_shared<GpuMotionRuntime>(
        shader_root, pixels_per_world_unit, result_fence_budget_ms,
        solve_interval_ticks);
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

#pragma once

#include "aion/kernel/journal.hpp"
#include "aion/kernel/patch.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aion {

using SystemId = std::uint32_t;

enum class ResourceKey : std::uint8_t {
    Identity = 0,
    EntityState = 1,
    Position = 2,
    Velocity = 3,
    Health = 4,
};

class AccessMask final {
public:
    constexpr AccessMask() = default;
    constexpr AccessMask(ResourceKey key) : bits_(bit(key)) {}

    [[nodiscard]] static constexpr AccessMask of(std::initializer_list<ResourceKey> keys) noexcept {
        AccessMask result;
        for (const auto key : keys) result.bits_ |= bit(key);
        return result;
    }

    [[nodiscard]] constexpr bool contains(ResourceKey key) const noexcept {
        return (bits_ & bit(key)) != 0;
    }
    [[nodiscard]] constexpr bool intersects(AccessMask other) const noexcept {
        return (bits_ & other.bits_) != 0;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0; }
    [[nodiscard]] constexpr std::uint64_t bits() const noexcept { return bits_; }

    friend constexpr AccessMask operator|(AccessMask a, AccessMask b) noexcept {
        AccessMask result;
        result.bits_ = a.bits_ | b.bits_;
        return result;
    }

private:
    static constexpr std::uint64_t bit(ResourceKey key) noexcept {
        return 1ULL << static_cast<std::uint8_t>(key);
    }
    std::uint64_t bits_{0};
};

class SystemContext final {
public:
    SystemContext(const World& world, AccessMask reads, AccessMask writes) noexcept;

    [[nodiscard]] std::size_t entity_capacity();
    [[nodiscard]] bool alive(EntityId id);
    [[nodiscard]] std::optional<Vec3> position(EntityId id);
    [[nodiscard]] std::optional<Vec3> velocity(EntityId id);
    [[nodiscard]] std::optional<std::uint32_t> health(EntityId id);

    void set_position(EntityId id, Vec3 value);
    void set_velocity(EntityId id, Vec3 value);
    void set_health(EntityId id, std::uint32_t value);
    void destroy(EntityId id);

    // R2.1 dense/clustered lanes. Ownership is moved into a transactional range patch.
    void set_position_range(EntityId first, std::vector<Vec3> values);
    void set_velocity_range(EntityId first, std::vector<Vec3> values);
    void set_health_range(EntityId first, std::vector<std::uint32_t> values);

    // Non-authoritative result used by scheduler microbenchmarks and diagnostics.
    void set_auxiliary_result(std::uint64_t value) noexcept { auxiliary_result_ = value; }

    [[nodiscard]] bool ok() const noexcept { return error_.empty(); }
    [[nodiscard]] std::string_view error() const noexcept { return error_; }
    [[nodiscard]] const std::vector<Mutation>& mutations() const noexcept { return mutations_; }
    [[nodiscard]] const std::vector<Vec3RangePatch>& vec3_patches() const noexcept { return vec3_patches_; }
    [[nodiscard]] const std::vector<U32RangePatch>& u32_patches() const noexcept { return u32_patches_; }
    [[nodiscard]] std::uint64_t auxiliary_result() const noexcept { return auxiliary_result_; }

private:
    [[nodiscard]] bool require_read(ResourceKey key);
    [[nodiscard]] bool require_write(ResourceKey key);
    void fail(std::string_view message);

    const World& world_;
    AccessMask reads_;
    AccessMask writes_;
    std::vector<Mutation> mutations_;
    std::vector<Vec3RangePatch> vec3_patches_;
    std::vector<U32RangePatch> u32_patches_;
    std::string error_;
    std::uint64_t auxiliary_result_{0};
};

using SystemFunction = std::function<void(SystemContext&)>;

struct SystemSpec {
    SystemId id{};
    std::string name;
    AccessMask reads;
    AccessMask writes;
    std::vector<SystemId> after;
    SystemFunction run;
};

struct ExecutionWave {
    std::vector<std::size_t> system_indices;
};

struct PlanBuildResult {
    bool ok{false};
    std::string error;
};

class ExecutionPlan final {
public:
    [[nodiscard]] static PlanBuildResult build(std::vector<SystemSpec> systems, ExecutionPlan& out);

    [[nodiscard]] const std::vector<SystemSpec>& systems() const noexcept { return systems_; }
    [[nodiscard]] const std::vector<ExecutionWave>& waves() const noexcept { return waves_; }
    [[nodiscard]] std::size_t edge_count() const noexcept { return edge_count_; }

private:
    std::vector<SystemSpec> systems_;
    std::vector<ExecutionWave> waves_;
    std::size_t edge_count_{0};
};

enum class ExecutorKind : std::uint8_t {
    SerialReference,
    WorkerPool,
};

struct ExecutionOptions {
    ExecutorKind kind{ExecutorKind::SerialReference};
    std::size_t workers{1};
};

struct ExecutionResult {
    bool ok{false};
    std::size_t waves_completed{0};
    std::size_t systems_executed{0};
    std::size_t transactions_committed{0};
    std::uint64_t auxiliary_checksum{0};
    SystemId failed_system{0};
    std::string error;
};

class ExecutionRuntime final {
public:
    explicit ExecutionRuntime(ExecutionOptions options = {});
    ~ExecutionRuntime();
    ExecutionRuntime(ExecutionRuntime&&) noexcept;
    ExecutionRuntime& operator=(ExecutionRuntime&&) noexcept;
    ExecutionRuntime(const ExecutionRuntime&) = delete;
    ExecutionRuntime& operator=(const ExecutionRuntime&) = delete;

    [[nodiscard]] ExecutionResult execute(
        const ExecutionPlan& plan,
        World& world,
        ChangeJournal* journal = nullptr);

    // R2.1 hybrid authority path. Supports scalar + range outputs and PatchJournal replay/rollback.
    [[nodiscard]] ExecutionResult execute_patched(
        const ExecutionPlan& plan,
        World& world,
        PatchJournal* journal = nullptr,
        PatchCommitRuntime* commit_runtime = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class ExecutionKernel final {
public:
    // Convenience one-shot executor. Prefer ExecutionRuntime for repeated frames so worker threads persist.
    [[nodiscard]] static ExecutionResult execute(
        const ExecutionPlan& plan,
        World& world,
        const ExecutionOptions& options = {},
        ChangeJournal* journal = nullptr);
};

} // namespace aion

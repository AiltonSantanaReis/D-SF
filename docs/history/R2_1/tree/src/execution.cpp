#include "aion/kernel/execution.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <future>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <utility>

namespace aion {
namespace {

[[nodiscard]] bool has_hazard(const SystemSpec& a, const SystemSpec& b) noexcept {
    const auto a_conflicts = a.writes.intersects(b.reads | b.writes);
    const auto b_conflicts = b.writes.intersects(a.reads | a.writes);
    return a_conflicts || b_conflicts;
}

class WorkerPool final {
public:
    explicit WorkerPool(std::size_t workers) {
        workers = std::max<std::size_t>(workers, 1);
        threads_.reserve(workers);
        for (std::size_t i = 0; i < workers; ++i) {
            threads_.emplace_back([this] {
                for (;;) {
                    std::function<void()> job;
                    {
                        std::unique_lock lock(mutex_);
                        cv_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                        if (stopping_ && jobs_.empty()) return;
                        job = std::move(jobs_.front());
                        jobs_.pop_front();
                    }
                    job();
                }
            });
        }
    }

    ~WorkerPool() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& thread : threads_) {
            if (thread.joinable()) thread.join();
        }
    }

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    template <typename F>
    auto submit(F&& function) -> std::future<std::invoke_result_t<F>> {
        using Result = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(function));
        auto future = task->get_future();
        {
            std::lock_guard lock(mutex_);
            jobs_.emplace_back([task] { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

private:
    std::vector<std::thread> threads_;
    std::deque<std::function<void()>> jobs_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stopping_{false};
};

struct SystemOutput {
    SystemId id{};
    bool ok{false};
    std::string error;
    std::vector<Mutation> mutations;
    std::vector<Vec3RangePatch> vec3_patches;
    std::vector<U32RangePatch> u32_patches;
    std::uint64_t auxiliary_result{};
};

[[nodiscard]] SystemOutput run_system(const SystemSpec& spec, const World& world) {
    SystemContext context(world, spec.reads, spec.writes);
    SystemOutput output{};
    output.id = spec.id;
    try {
        if (!spec.run) { output.error = "system has no function"; return output; }
        spec.run(context);
    } catch (const std::exception& ex) {
        output.error = std::string("system threw exception: ") + ex.what(); return output;
    } catch (...) {
        output.error = "system threw non-standard exception"; return output;
    }
    output.auxiliary_result = context.auxiliary_result();
    if (!context.ok()) { output.error = std::string(context.error()); return output; }
    output.ok = true;
    output.mutations = context.mutations();
    output.vec3_patches = context.vec3_patches();
    output.u32_patches = context.u32_patches();
    return output;
}

[[nodiscard]] std::uint64_t mix_aux(std::uint64_t state, SystemId id, std::uint64_t value) noexcept {
    state ^= static_cast<std::uint64_t>(id) + 0x9e3779b97f4a7c15ULL + (state << 6U) + (state >> 2U);
    state ^= value + 0x517cc1b727220a95ULL + (state << 7U) + (state >> 3U);
    return state;
}

} // namespace

SystemContext::SystemContext(const World& world, AccessMask reads, AccessMask writes) noexcept
    : world_(world), reads_(reads), writes_(writes) {}

bool SystemContext::require_read(ResourceKey key) {
    if (reads_.contains(key)) return true;
    fail("system attempted an undeclared read");
    return false;
}

bool SystemContext::require_write(ResourceKey key) {
    if (writes_.contains(key)) return true;
    fail("system attempted an undeclared write");
    return false;
}

void SystemContext::fail(std::string_view message) {
    if (error_.empty()) error_.assign(message);
}

std::size_t SystemContext::entity_capacity() {
    if (!require_read(ResourceKey::Identity)) return 0;
    return world_.entity_capacity();
}

bool SystemContext::alive(EntityId id) {
    if (!require_read(ResourceKey::EntityState)) return false;
    return world_.alive(id);
}

std::optional<Vec3> SystemContext::position(EntityId id) {
    if (!require_read(ResourceKey::Position)) return std::nullopt;
    return world_.position(id);
}

std::optional<Vec3> SystemContext::velocity(EntityId id) {
    if (!require_read(ResourceKey::Velocity)) return std::nullopt;
    return world_.velocity(id);
}

std::optional<std::uint32_t> SystemContext::health(EntityId id) {
    if (!require_read(ResourceKey::Health)) return std::nullopt;
    return world_.health(id);
}

void SystemContext::set_position(EntityId id, Vec3 value) {
    if (!require_write(ResourceKey::Position)) return;
    mutations_.push_back({MutationKind::SetPosition, id, value, 0});
}

void SystemContext::set_velocity(EntityId id, Vec3 value) {
    if (!require_write(ResourceKey::Velocity)) return;
    mutations_.push_back({MutationKind::SetVelocity, id, value, 0});
}

void SystemContext::set_health(EntityId id, std::uint32_t value) {
    if (!require_write(ResourceKey::Health)) return;
    mutations_.push_back({MutationKind::SetHealth, id, {}, value});
}

void SystemContext::destroy(EntityId id) {
    if (!require_write(ResourceKey::EntityState)) return;
    mutations_.push_back({MutationKind::DestroyEntity, id, {}, 0});
}

void SystemContext::set_position_range(EntityId first, std::vector<Vec3> values) {
    if (!require_write(ResourceKey::Position)) return;
    if (values.empty()) { fail("position range cannot be empty"); return; }
    vec3_patches_.push_back({PatchComponent::Position, first, std::move(values)});
}

void SystemContext::set_velocity_range(EntityId first, std::vector<Vec3> values) {
    if (!require_write(ResourceKey::Velocity)) return;
    if (values.empty()) { fail("velocity range cannot be empty"); return; }
    vec3_patches_.push_back({PatchComponent::Velocity, first, std::move(values)});
}

void SystemContext::set_health_range(EntityId first, std::vector<std::uint32_t> values) {
    if (!require_write(ResourceKey::Health)) return;
    if (values.empty()) { fail("health range cannot be empty"); return; }
    u32_patches_.push_back({PatchComponent::Health, first, std::move(values)});
}

PlanBuildResult ExecutionPlan::build(std::vector<SystemSpec> systems, ExecutionPlan& out) {
    if (systems.empty()) {
        out = {};
        return {true, {}};
    }

    std::sort(systems.begin(), systems.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

    std::unordered_map<SystemId, std::size_t> by_id;
    by_id.reserve(systems.size());
    for (std::size_t i = 0; i < systems.size(); ++i) {
        if (systems[i].id == 0) return {false, "system id 0 is reserved"};
        if (!systems[i].run) return {false, "every system requires a function"};
        if (!by_id.emplace(systems[i].id, i).second) return {false, "duplicate system id"};
    }

    const auto n = systems.size();
    std::vector<std::vector<std::size_t>> adjacency(n);
    std::vector<std::size_t> indegree(n, 0);
    std::vector<std::vector<bool>> edge(n, std::vector<bool>(n, false));
    std::size_t edge_count = 0;

    auto add_edge = [&](std::size_t from, std::size_t to) {
        if (from == to || edge[from][to]) return;
        edge[from][to] = true;
        adjacency[from].push_back(to);
        ++indegree[to];
        ++edge_count;
    };

    // Explicit dependencies first.
    for (std::size_t i = 0; i < n; ++i) {
        for (const auto dep_id : systems[i].after) {
            const auto found = by_id.find(dep_id);
            if (found == by_id.end()) return {false, "explicit dependency references an unknown system"};
            if (found->second == i) return {false, "system cannot depend on itself"};
            add_edge(found->second, i);
        }
    }

    // Data hazards use stable SystemId order as the canonical tie-break. This is not a frame phase:
    // it only resolves otherwise ambiguous read/write and write/write precedence deterministically.
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            if (has_hazard(systems[i], systems[j])) add_edge(i, j);
        }
    }

    // Deterministic Kahn waves: all currently-ready systems form one parallel-safe wave.
    std::vector<ExecutionWave> waves;
    std::vector<std::size_t> current;
    current.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        if (indegree[i] == 0) current.push_back(i);
    }

    std::size_t visited = 0;
    while (!current.empty()) {
        std::sort(current.begin(), current.end(), [&](std::size_t a, std::size_t b) {
            return systems[a].id < systems[b].id;
        });
        waves.push_back({current});
        visited += current.size();

        std::vector<std::size_t> next;
        for (const auto node : current) {
            for (const auto target : adjacency[node]) {
                if (--indegree[target] == 0) next.push_back(target);
            }
        }
        current = std::move(next);
    }

    if (visited != n) return {false, "dependency graph contains a cycle or a dependency contradicts canonical hazard order"};

    out.systems_ = std::move(systems);
    out.waves_ = std::move(waves);
    out.edge_count_ = edge_count;
    return {true, {}};
}

struct ExecutionRuntime::Impl final {
    explicit Impl(ExecutionOptions requested) : options(requested) {
        options.workers = std::max<std::size_t>(options.workers, 1);
        if (options.kind == ExecutorKind::WorkerPool && options.workers > 1) {
            pool.emplace(options.workers);
        }
    }

    ExecutionOptions options;
    std::optional<WorkerPool> pool;
};

ExecutionRuntime::ExecutionRuntime(ExecutionOptions options)
    : impl_(std::make_unique<Impl>(options)) {}

ExecutionRuntime::~ExecutionRuntime() = default;
ExecutionRuntime::ExecutionRuntime(ExecutionRuntime&&) noexcept = default;
ExecutionRuntime& ExecutionRuntime::operator=(ExecutionRuntime&&) noexcept = default;

ExecutionResult ExecutionRuntime::execute(
    const ExecutionPlan& plan,
    World& world,
    ChangeJournal* journal) {

    ExecutionResult result{};
    result.ok = true;

    for (const auto& wave : plan.waves()) {
        std::vector<SystemOutput> outputs;
        outputs.reserve(wave.system_indices.size());

        if (impl_->pool) {
            std::vector<std::future<SystemOutput>> futures;
            futures.reserve(wave.system_indices.size());
            for (const auto index : wave.system_indices) {
                const auto* spec = &plan.systems()[index];
                futures.push_back(impl_->pool->submit([spec, &world] { return run_system(*spec, world); }));
            }
            for (auto& future : futures) outputs.push_back(future.get());
        } else {
            for (const auto index : wave.system_indices) {
                outputs.push_back(run_system(plan.systems()[index], world));
            }
        }

        std::sort(outputs.begin(), outputs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });

        for (const auto& output : outputs) {
            ++result.systems_executed;
            result.auxiliary_checksum = mix_aux(result.auxiliary_checksum, output.id, output.auxiliary_result);
            if (!output.ok) {
                result.ok = false;
                result.failed_system = output.id;
                result.error = output.error;
                return result;
            }
        }

        std::size_t mutation_count = 0;
        std::size_t range_count = 0;
        for (const auto& output : outputs) {
            mutation_count += output.mutations.size();
            range_count += output.vec3_patches.size() + output.u32_patches.size();
        }
        if (range_count != 0) {
            result.ok = false;
            result.error = "range patch output requires execute_patched";
            return result;
        }

        if (mutation_count > 0) {
            if (world.last_transaction_id() == std::numeric_limits<TransactionId>::max()) {
                result.ok = false;
                result.error = "transaction id space exhausted";
                return result;
            }

            Transaction tx{};
            tx.id = world.last_transaction_id() + 1;
            tx.mutations.reserve(mutation_count);
            for (auto& output : outputs) {
                tx.mutations.insert(tx.mutations.end(), output.mutations.begin(), output.mutations.end());
            }

            const auto commit = journal ? journal->commit(world, tx) : world.commit(tx);
            if (!commit.committed) {
                result.ok = false;
                result.error = std::string(commit.error);
                return result;
            }
            ++result.transactions_committed;
        }

        ++result.waves_completed;
    }

    return result;
}

ExecutionResult ExecutionRuntime::execute_patched(
    const ExecutionPlan& plan,
    World& world,
    PatchJournal* journal,
    PatchCommitRuntime* commit_runtime) {

    ExecutionResult result{};
    result.ok = true;

    for (const auto& wave : plan.waves()) {
        std::vector<SystemOutput> outputs;
        outputs.reserve(wave.system_indices.size());

        if (impl_->pool) {
            std::vector<std::future<SystemOutput>> futures;
            futures.reserve(wave.system_indices.size());
            for (const auto index : wave.system_indices) {
                const auto* spec = &plan.systems()[index];
                futures.push_back(impl_->pool->submit([spec, &world] { return run_system(*spec, world); }));
            }
            for (auto& future : futures) outputs.push_back(future.get());
        } else {
            for (const auto index : wave.system_indices) outputs.push_back(run_system(plan.systems()[index], world));
        }

        std::sort(outputs.begin(), outputs.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        for (const auto& output : outputs) {
            ++result.systems_executed;
            result.auxiliary_checksum = mix_aux(result.auxiliary_checksum, output.id, output.auxiliary_result);
            if (!output.ok) {
                result.ok = false; result.failed_system = output.id; result.error = output.error; return result;
            }
        }

        std::size_t scalar_count = 0, vec_count = 0, u32_count = 0;
        for (const auto& output : outputs) {
            scalar_count += output.mutations.size();
            vec_count += output.vec3_patches.size();
            u32_count += output.u32_patches.size();
        }
        if (scalar_count + vec_count + u32_count != 0) {
            if (world.last_transaction_id() == std::numeric_limits<TransactionId>::max()) {
                result.ok = false; result.error = "transaction id space exhausted"; return result;
            }
            PatchTransaction tx{}; tx.id = world.last_transaction_id() + 1;
            tx.scalar_mutations.reserve(scalar_count); tx.vec3_patches.reserve(vec_count); tx.u32_patches.reserve(u32_count);
            for (auto& output : outputs) {
                tx.scalar_mutations.insert(tx.scalar_mutations.end(),
                    std::make_move_iterator(output.mutations.begin()), std::make_move_iterator(output.mutations.end()));
                tx.vec3_patches.insert(tx.vec3_patches.end(),
                    std::make_move_iterator(output.vec3_patches.begin()), std::make_move_iterator(output.vec3_patches.end()));
                tx.u32_patches.insert(tx.u32_patches.end(),
                    std::make_move_iterator(output.u32_patches.begin()), std::make_move_iterator(output.u32_patches.end()));
            }

            PatchCommitResult commit{};
            if (journal) commit = journal->commit(world, tx);
            else if (commit_runtime) commit = commit_runtime->commit(world, tx);
            else commit = commit_patch_transaction(world, tx);
            if (!commit.committed) { result.ok = false; result.error = commit.error; return result; }
            ++result.transactions_committed;
        }
        ++result.waves_completed;
    }
    return result;
}

ExecutionResult ExecutionKernel::execute(
    const ExecutionPlan& plan,
    World& world,
    const ExecutionOptions& options,
    ChangeJournal* journal) {
    ExecutionRuntime runtime(options);
    return runtime.execute(plan, world, journal);
}

} // namespace aion

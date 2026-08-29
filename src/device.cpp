#include "aion/kernel/device.hpp"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

namespace aion {
namespace {
constexpr std::size_t kNoSlot = std::numeric_limits<std::size_t>::max();
}

bool ReferenceDeviceBackend::upload_impl(std::span<const std::byte> bytes, Token& token, std::string& error, bool honor_failure) {
    if (honor_failure && failure_enabled_ && successes_since_injection_ >= successes_before_failure_) {
        error = "injected device upload failure";
        token = 0;
        return false;
    }
    Allocation allocation;
    allocation.token = next_token_++;
    allocation.bytes.assign(bytes.begin(), bytes.end());
    token = allocation.token;
    allocations_.push_back(std::move(allocation));
    if (honor_failure && failure_enabled_) ++successes_since_injection_;
    error.clear();
    return true;
}

bool ReferenceDeviceBackend::upload(std::span<const std::byte> bytes, Token& token, std::string& error) {
    return upload_impl(bytes, token, error, true);
}

bool ReferenceDeviceBackend::restore(std::span<const std::byte> bytes, Token& token, std::string& error) {
    return upload_impl(bytes, token, error, false);
}

void ReferenceDeviceBackend::destroy(Token token) noexcept {
    const auto it = std::find_if(allocations_.begin(), allocations_.end(), [token](const Allocation& allocation) { return allocation.token == token; });
    if (it != allocations_.end()) allocations_.erase(it);
}

bool ReferenceDeviceBackend::alive(Token token) const noexcept {
    return std::any_of(allocations_.begin(), allocations_.end(), [token](const Allocation& allocation) { return allocation.token == token; });
}

std::size_t ReferenceDeviceBackend::allocated_bytes() const noexcept {
    std::size_t total = 0;
    for (const auto& allocation : allocations_) total += allocation.bytes.size();
    return total;
}

void ReferenceDeviceBackend::fail_after_successful_uploads(std::size_t successes_before_failure) noexcept {
    failure_enabled_ = true;
    successes_before_failure_ = successes_before_failure;
    successes_since_injection_ = 0;
}

void ReferenceDeviceBackend::clear_failure_injection() noexcept {
    failure_enabled_ = false;
    successes_before_failure_ = 0;
    successes_since_injection_ = 0;
}

std::size_t DeviceResidencyManager::find_key(const DeviceResourceKey& key) const noexcept {
    const auto it = key_to_slot_.find(key);
    return it == key_to_slot_.end() ? kNoSlot : it->second;
}

std::size_t DeviceResidencyManager::allocate_slot() {
    if (!free_slots_.empty()) {
        const auto slot = free_slots_.back();
        free_slots_.pop_back();
        return slot;
    }
    entries_.emplace_back();
    return entries_.size() - 1U;
}

void DeviceResidencyManager::touch(std::size_t slot) noexcept {
    entries_[slot].last_use = ++clock_;
}

bool DeviceResidencyManager::ensure(const DeviceResourceUpload& upload, DeviceResourceHandle& handle, std::string& error) {
    const std::array<DeviceResourceUpload, 1> group{upload};
    std::vector<DeviceResourceHandle> handles;
    if (!ensure_group(group, handles, error)) return false;
    handle = handles.front();
    return true;
}

bool DeviceResidencyManager::ensure_group(std::span<const DeviceResourceUpload> uploads, std::vector<DeviceResourceHandle>& handles, std::string& error) {
    handles.clear();
    if (uploads.empty()) {
        error = "device residency group must contain at least one resource";
        return false;
    }
    std::unordered_set<DeviceResourceKey, DeviceResourceKeyHash> unique_keys;
    unique_keys.reserve(uploads.size());
    for (const auto& upload : uploads) {
        if (upload.bytes.size() > budget_bytes_) {
            error = "device resource exceeds residency budget";
            return false;
        }
        if (!unique_keys.insert(upload.key).second) {
            error = "device residency group contains duplicate resource keys";
            return false;
        }
    }

    struct RequestPlan {
        bool existed_before{false};
        std::size_t existing_slot{kNoSlot};
        ReferenceDeviceBackend::Token staged_token{};
    };
    std::vector<RequestPlan> plan(uploads.size());
    std::size_t new_bytes = 0;
    for (std::size_t i = 0; i < uploads.size(); ++i) {
        const auto slot = find_key(uploads[i].key);
        if (slot != kNoSlot) {
            plan[i].existed_before = true;
            plan[i].existing_slot = slot;
            if (entries_[slot].host != uploads[i].bytes) {
                error = "immutable device resource key reused with different bytes";
                return false;
            }
        } else {
            new_bytes += uploads[i].bytes.size();
        }
    }

    std::vector<std::size_t> eviction_candidates;
    for (std::size_t slot = 0; slot < entries_.size(); ++slot) {
        if (!entries_[slot].occupied || entries_[slot].pinned) continue;
        bool protected_by_group = false;
        for (const auto& request : plan) {
            if (request.existed_before && request.existing_slot == slot) { protected_by_group = true; break; }
        }
        if (!protected_by_group) eviction_candidates.push_back(slot);
    }
    std::stable_sort(eviction_candidates.begin(), eviction_candidates.end(), [&](std::size_t a, std::size_t b) {
        if (entries_[a].last_use != entries_[b].last_use) return entries_[a].last_use < entries_[b].last_use;
        return a < b;
    });

    std::vector<std::size_t> evict_slots;
    std::size_t projected = resident_bytes_ + new_bytes;
    for (const auto slot : eviction_candidates) {
        if (projected <= budget_bytes_) break;
        projected -= entries_[slot].host.size();
        evict_slots.push_back(slot);
    }
    if (projected > budget_bytes_) {
        error = "insufficient evictable device residency budget for atomic group";
        return false;
    }

    for (const auto slot : evict_slots) backend_.destroy(entries_[slot].backend_token);

    std::vector<ReferenceDeviceBackend::Token> staged_tokens;
    staged_tokens.reserve(uploads.size());
    for (std::size_t i = 0; i < uploads.size(); ++i) {
        if (plan[i].existed_before) continue;
        ReferenceDeviceBackend::Token token{};
        if (!backend_.upload(uploads[i].bytes, token, error)) {
            for (const auto staged : staged_tokens) backend_.destroy(staged);
            for (const auto slot : evict_slots) {
                ReferenceDeviceBackend::Token restored{};
                std::string restore_error;
                if (!backend_.restore(entries_[slot].host, restored, restore_error)) {
                    error += "; rollback restore failed: " + restore_error;
                    return false;
                }
                entries_[slot].backend_token = restored;
            }
            return false;
        }
        plan[i].staged_token = token;
        staged_tokens.push_back(token);
    }

    for (const auto slot : evict_slots) {
        resident_bytes_ -= entries_[slot].host.size();
        key_to_slot_.erase(entries_[slot].key);
        entries_[slot].occupied = false;
        entries_[slot].host.clear();
        entries_[slot].backend_token = 0;
        entries_[slot].pinned = false;
        ++entries_[slot].generation;
        if (entries_[slot].generation == 0U) entries_[slot].generation = 1U;
        free_slots_.push_back(slot);
    }

    handles.resize(uploads.size());
    for (std::size_t i = 0; i < uploads.size(); ++i) {
        if (plan[i].existed_before) {
            const auto slot = plan[i].existing_slot;
            entries_[slot].pinned = entries_[slot].pinned || uploads[i].pinned;
            touch(slot);
            handles[i] = {static_cast<std::uint32_t>(slot), entries_[slot].generation};
            continue;
        }
        const auto slot = allocate_slot();
        auto& entry = entries_[slot];
        entry.occupied = true;
        entry.key = uploads[i].key;
        key_to_slot_[entry.key] = slot;
        entry.host = uploads[i].bytes;
        entry.backend_token = plan[i].staged_token;
        entry.pinned = uploads[i].pinned;
        touch(slot);
        resident_bytes_ += entry.host.size();
        handles[i] = {static_cast<std::uint32_t>(slot), entry.generation};
    }
    error.clear();
    return true;
}

bool DeviceResidencyManager::evict(const DeviceResourceKey& key, std::string& error) {
    const auto slot = find_key(key);
    if (slot == kNoSlot) {
        error = "device resource is not resident";
        return false;
    }
    if (entries_[slot].pinned) {
        error = "pinned device resource cannot be evicted";
        return false;
    }
    backend_.destroy(entries_[slot].backend_token);
    resident_bytes_ -= entries_[slot].host.size();
    key_to_slot_.erase(entries_[slot].key);
    entries_[slot].occupied = false;
    entries_[slot].host.clear();
    entries_[slot].backend_token = 0;
    ++entries_[slot].generation;
    if (entries_[slot].generation == 0U) entries_[slot].generation = 1U;
    free_slots_.push_back(slot);
    error.clear();
    return true;
}

bool DeviceResidencyManager::resident(DeviceResourceHandle handle) const noexcept {
    return handle.valid() && handle.slot < entries_.size() && entries_[handle.slot].occupied && entries_[handle.slot].generation == handle.generation;
}

bool DeviceResidencyManager::resident(const DeviceResourceKey& key) const noexcept { return find_key(key) != kNoSlot; }

DeviceResourceHandle DeviceResidencyManager::handle_for(const DeviceResourceKey& key) const noexcept {
    const auto slot = find_key(key);
    if (slot == kNoSlot) return {};
    return {static_cast<std::uint32_t>(slot), entries_[slot].generation};
}

std::span<const std::byte> DeviceResidencyManager::host_bytes(DeviceResourceHandle handle) const noexcept {
    if (!resident(handle)) return {};
    return entries_[handle.slot].host;
}

DeviceResidencyStats DeviceResidencyManager::stats() const noexcept {
    std::size_t count = 0;
    for (const auto& entry : entries_) if (entry.occupied) ++count;
    return {budget_bytes_, resident_bytes_, count};
}

} // namespace aion

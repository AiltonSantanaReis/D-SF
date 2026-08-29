#include "aion/kernel/device.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace aion;

[[noreturn]] void fail(const std::string& message) { std::cerr << "R5A FAIL: " << message << '\n'; std::exit(1); }
void require(bool condition, const std::string& message) { if (!condition) fail(message); }

DeviceResourceUpload upload(std::uint32_t resource, std::uint16_t subresource, std::size_t bytes, bool pinned = false) {
    DeviceResourceUpload out;
    out.key = geometry_resource_key({9U, 1U, resource}, 7U, DeviceResourceClass::GeometryPayload, subresource);
    out.bytes.resize(bytes, static_cast<std::byte>(resource & 0xFFU));
    out.pinned = pinned;
    return out;
}
}

int main() {
    using namespace aion;
    ReferenceDeviceBackend backend;
    DeviceResidencyManager manager(backend, 100U);
    std::string error;
    DeviceResourceHandle a{}, b{}, c{};

    require(manager.ensure(upload(1U, 0U, 40U), a, error), error);
    require(manager.ensure(upload(2U, 0U, 40U), b, error), error);
    require(manager.resident(a) && manager.resident(b), "initial resources not resident");

    // Touch B so A remains the LRU victim.
    DeviceResourceHandle b2{};
    require(manager.ensure(upload(2U, 0U, 40U), b2, error), error);
    require(b2 == b, "idempotent ensure changed handle");
    require(manager.ensure(upload(3U, 0U, 40U), c, error), error);
    require(!manager.resident(a), "LRU resource was not evicted");
    require(manager.resident(b) && manager.resident(c), "wrong resource evicted");

    // A stale handle must remain invalid even if its slot is reused.
    DeviceResourceHandle d{};
    require(manager.ensure(upload(4U, 0U, 20U), d, error), error);
    require(!manager.resident(a), "stale generation became valid after slot reuse");

    // Same immutable key, different bytes is illegal.
    auto different = upload(4U, 0U, 20U);
    different.bytes[0] = std::byte{0xEE};
    DeviceResourceHandle ignored{};
    require(!manager.ensure(different, ignored, error), "immutable key accepted different bytes");

    // Pinned resource cannot be the victim.
    ReferenceDeviceBackend pin_backend;
    DeviceResidencyManager pin_manager(pin_backend, 80U);
    DeviceResourceHandle pinned{}, ordinary{}, newcomer{};
    require(pin_manager.ensure(upload(10U, 0U, 40U, true), pinned, error), error);
    require(pin_manager.ensure(upload(11U, 0U, 40U), ordinary, error), error);
    require(pin_manager.ensure(upload(12U, 0U, 40U), newcomer, error), error);
    require(pin_manager.resident(pinned), "pinned resource was evicted");
    require(!pin_manager.resident(ordinary), "ordinary LRU resource should have been evicted");

    const auto stats = manager.stats();
    require(stats.resident_bytes <= stats.budget_bytes, "residency budget exceeded");
    require(stats.resident_bytes == backend.allocated_bytes(), "manager/backend byte accounting diverged");

    std::cout << "R5A PASS resident_bytes=" << stats.resident_bytes << " resources=" << stats.resident_resources << '\n';
    return 0;
}

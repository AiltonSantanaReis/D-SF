#pragma once

#include <limits>

namespace aion {

struct Vec3 {
    float x{};
    float y{};
    float z{};

    friend bool operator==(const Vec3&, const Vec3&) = default;
};

struct Aabb {
    Vec3 min{};
    Vec3 max{};

    friend bool operator==(const Aabb&, const Aabb&) = default;
};

struct Ray {
    Vec3 origin{};
    Vec3 direction{1.0F, 0.0F, 0.0F};
    float t_min{0.0F};
    float t_max{std::numeric_limits<float>::infinity()};
};

} // namespace aion

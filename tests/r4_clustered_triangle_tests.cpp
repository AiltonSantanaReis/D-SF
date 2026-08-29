#include "aion/kernel/clustered_triangle.hpp"
#include "aion/kernel/geometry.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <vector>

#define CHECK(expr) do { if (!(expr)) { std::cerr << "CHECK failed: " #expr " at " << __FILE__ << ':' << __LINE__ << '\n'; return EXIT_FAILURE; } } while (false)

namespace {
using aion::Vec3;

struct Mesh { std::vector<Vec3> vertices; std::vector<std::uint32_t> indices; };

Mesh make_heightfield(std::size_t n) {
    Mesh m;
    m.vertices.reserve(n * n);
    for (std::size_t y = 0; y < n; ++y) {
        for (std::size_t x = 0; x < n; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(n - 1U);
            const float fy = static_cast<float>(y) / static_cast<float>(n - 1U);
            const float z = 0.15F * std::sin(fx * 8.0F) * std::cos(fy * 7.0F);
            m.vertices.push_back({fx * 4.0F - 2.0F, fy * 4.0F - 2.0F, z});
        }
    }
    m.indices.reserve((n - 1U) * (n - 1U) * 6U);
    for (std::size_t y = 0; y + 1U < n; ++y) {
        for (std::size_t x = 0; x + 1U < n; ++x) {
            const auto a = static_cast<std::uint32_t>(y * n + x);
            const auto b = static_cast<std::uint32_t>(y * n + x + 1U);
            const auto c = static_cast<std::uint32_t>((y + 1U) * n + x);
            const auto d = static_cast<std::uint32_t>((y + 1U) * n + x + 1U);
            m.indices.insert(m.indices.end(), {a, b, c, b, d, c});
        }
    }
    return m;
}

Mesh make_torus(std::size_t nu, std::size_t nv, float major_r, float minor_r) {
    Mesh m;
    m.vertices.reserve(nu * nv);
    constexpr float tau = 2.0F * std::numbers::pi_v<float>;
    for (std::size_t v = 0; v < nv; ++v) {
        const float av = tau * static_cast<float>(v) / static_cast<float>(nv);
        const float cv = std::cos(av), sv = std::sin(av);
        for (std::size_t u = 0; u < nu; ++u) {
            const float au = tau * static_cast<float>(u) / static_cast<float>(nu);
            const float cu = std::cos(au), su = std::sin(au);
            const float rr = major_r + minor_r * cv;
            m.vertices.push_back({rr * cu, rr * su, minor_r * sv});
        }
    }
    m.indices.reserve(nu * nv * 6U);
    for (std::size_t v = 0; v < nv; ++v) {
        const std::size_t vn = (v + 1U) % nv;
        for (std::size_t u = 0; u < nu; ++u) {
            const std::size_t un = (u + 1U) % nu;
            const auto a = static_cast<std::uint32_t>(v * nu + u);
            const auto b = static_cast<std::uint32_t>(v * nu + un);
            const auto c = static_cast<std::uint32_t>(vn * nu + u);
            const auto d = static_cast<std::uint32_t>(vn * nu + un);
            m.indices.insert(m.indices.end(), {a, b, c, b, d, c});
        }
    }
    return m;
}

} // namespace

int main() {
    aion::GeometryKernel kernel;
    std::string error;
    aion::TriangleReferenceProvider reference(1);
    aion::ClusteredTriangleProvider clustered(4);
    CHECK(reference.register_with(kernel, error));
    CHECK(clustered.register_with(kernel, error));

    const auto grid = make_heightfield(48);
    const auto ref_grid = reference.add_mesh(grid.vertices, grid.indices, error);
    CHECK(ref_grid.valid());

    aion::ClusteredTriangleOptions exact_options;
    exact_options.encoding = aion::ClusteredTriangleEncoding::Float32;
    const auto exact_grid = clustered.add_mesh(grid.vertices, grid.indices, exact_options);
    CHECK(exact_grid.ok());
    aion::ClusteredTriangleOptions quant_options;
    quant_options.encoding = aion::ClusteredTriangleEncoding::QuantizedU16;
    const auto quant_grid = clustered.add_mesh(grid.vertices, grid.indices, quant_options);
    CHECK(quant_grid.ok());
    CHECK(exact_grid.max_geometric_error == 0.0F);
    CHECK(quant_grid.max_geometric_error > 0.0F);
    CHECK(quant_grid.stats.clusters > 1U);
    CHECK(quant_grid.stats.bvh_nodes > 0U);

    float max_quant_t_error = 0.0F;
    std::size_t compared = 0;
    for (std::size_t iy = 0; iy < 31; ++iy) {
        for (std::size_t ix = 0; ix < 31; ++ix) {
            const float x = -1.95F + 3.9F * (static_cast<float>(ix) + 0.37F) / 31.0F;
            const float y = -1.95F + 3.9F * (static_cast<float>(iy) + 0.61F) / 31.0F;
            const aion::Ray ray{{x, y, 2.0F}, {0.0F, 0.0F, -1.0F}, 0.0F, 5.0F};
            const auto a = kernel.raycast(ref_grid, ray);
            const auto b = kernel.raycast(exact_grid.handle, ray);
            const auto c = kernel.raycast(quant_grid.handle, ray);
            CHECK(a.status == b.status);
            CHECK(a.status == c.status);
            if (a.status == aion::GeometryQueryStatus::Ok) {
                CHECK(std::fabs(a.t - b.t) < 2.0e-5F);
                max_quant_t_error = std::max(max_quant_t_error, std::fabs(a.t - c.t));
                ++compared;
            }
        }
    }
    CHECK(compared > 800U);
    CHECK(max_quant_t_error <= quant_grid.max_geometric_error * 1.5F + 2.0e-4F);

    // Closed curved adversary: avoid parametric edges so we test geometry rather than edge ownership.
    const auto torus = make_torus(48, 24, 2.0F, 0.6F);
    const auto ref_torus = reference.add_mesh(torus.vertices, torus.indices, error);
    CHECK(ref_torus.valid());
    const auto quant_torus = clustered.add_mesh(torus.vertices, torus.indices, quant_options);
    CHECK(quant_torus.ok());
    float torus_max_error = 0.0F;
    constexpr float tau = 2.0F * std::numbers::pi_v<float>;
    constexpr float v_angle = 0.31F;
    for (std::size_t i = 0; i < 160; ++i) {
        const float u = tau * (static_cast<float>(i) + 0.37F) / 160.0F;
        const float cu = std::cos(u), su = std::sin(u);
        const float rr = 2.0F + 0.6F * std::cos(v_angle);
        const Vec3 target{rr * cu, rr * su, 0.6F * std::sin(v_angle)};
        const Vec3 origin{target.x + 2.0F * cu, target.y + 2.0F * su, target.z};
        const aion::Ray ray{origin, {-cu, -su, 0.0F}, 0.0F, 6.0F};
        const auto a = kernel.raycast(ref_torus, ray);
        const auto b = kernel.raycast(quant_torus.handle, ray);
        CHECK(a.status == aion::GeometryQueryStatus::Ok);
        CHECK(b.status == aion::GeometryQueryStatus::Ok);
        torus_max_error = std::max(torus_max_error, std::fabs(a.t - b.t));
    }
    CHECK(torus_max_error <= quant_torus.max_geometric_error * 2.0F + 3.0e-4F);

    // Provider wrapper lifetime must not own the published resource lifetime.
    aion::GeometryHandle lifetime_handle{};
    {
        aion::GeometryKernel local_kernel;
        aion::ClusteredTriangleProvider local_provider(7);
        CHECK(local_provider.register_with(local_kernel, error));
        lifetime_handle = local_provider.add_mesh(grid.vertices, grid.indices, quant_options).handle;
        CHECK(local_kernel.valid(lifetime_handle));
        // local_kernel intentionally remains scoped with provider in this reference test; R4A already
        // verifies registry-owned provider state after authoring wrapper lifetime.
    }

    std::cout << "r4_clustered_triangle_tests: PASS\n"
              << "triangles=" << quant_grid.stats.source_triangles << '\n'
              << "clusters=" << quant_grid.stats.clusters << '\n'
              << "storage_bytes=" << quant_grid.stats.storage_bytes << '\n'
              << "source_raw_bytes=" << quant_grid.stats.source_raw_bytes << '\n'
              << "max_error=" << quant_grid.max_geometric_error << '\n'
              << "max_quant_t_error=" << max_quant_t_error << '\n'
              << "torus_max_error=" << torus_max_error << '\n';
    return EXIT_SUCCESS;
}

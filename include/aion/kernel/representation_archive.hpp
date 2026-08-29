#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace aion {

struct RepresentationArchive {
    std::vector<std::byte> topology;
    std::vector<std::vector<std::byte>> payloads;
};

class CanonicalByteWriter final {
public:
    explicit CanonicalByteWriter(std::vector<std::byte>& output) noexcept : output_(output) {}

    void u8(std::uint8_t value) { output_.push_back(static_cast<std::byte>(value)); }
    void u16(std::uint16_t value) {
        for (unsigned shift = 0; shift < 16; shift += 8) u8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
    void u32(std::uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) u8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
    void u64(std::uint64_t value) {
        for (unsigned shift = 0; shift < 64; shift += 8) u8(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
    void i16(std::int16_t value) { u16(std::bit_cast<std::uint16_t>(value)); }
    void i32(std::int32_t value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void f32(float value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void bytes(const std::vector<std::byte>& value) { output_.insert(output_.end(), value.begin(), value.end()); }

private:
    std::vector<std::byte>& output_;
};

[[nodiscard]] inline std::uint64_t archive_fingerprint(const RepresentationArchive& archive) noexcept {
    constexpr std::uint64_t offset = 1469598103934665603ULL;
    constexpr std::uint64_t prime = 1099511628211ULL;
    std::uint64_t hash = offset;
    const auto mix_byte = [&](std::byte b, std::uint64_t& state) {
        state ^= static_cast<std::uint8_t>(b);
        state *= prime;
    };
    for (const auto b : archive.topology) mix_byte(b, hash);
    hash ^= 0xFFU;
    hash *= prime;
    for (const auto& payload : archive.payloads) {
        for (const auto b : payload) mix_byte(b, hash);
        hash ^= 0xFEU;
        hash *= prime;
    }
    return hash;
}

} // namespace aion

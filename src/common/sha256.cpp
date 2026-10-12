#include "common/sha256.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace netstream {

namespace {

inline uint32_t ror(uint32_t x, uint32_t n) noexcept {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t choose(uint32_t e, uint32_t f, uint32_t g) noexcept {
    return (e & f) ^ (~e & g);
}

inline uint32_t majority(uint32_t a, uint32_t b, uint32_t c) noexcept {
    return (a & b) ^ (a & c) ^ (b & c);
}

inline uint32_t sig0(uint32_t x) noexcept {
    return ror(x, 2) ^ ror(x, 13) ^ ror(x, 22);
}

inline uint32_t sig1(uint32_t x) noexcept {
    return ror(x, 6) ^ ror(x, 11) ^ ror(x, 25);
}

inline uint32_t theta0(uint32_t x) noexcept {
    return ror(x, 7) ^ ror(x, 18) ^ (x >> 3);
}

inline uint32_t theta1(uint32_t x) noexcept {
    return ror(x, 17) ^ ror(x, 19) ^ (x >> 10);
}

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

} // namespace

Sha256::Sha256() {
    state_[0] = 0x6a09e667;
    state_[1] = 0xbb67ae85;
    state_[2] = 0x3c6ef372;
    state_[3] = 0xa54ff53a;
    state_[4] = 0x510e527f;
    state_[5] = 0x9b05688c;
    state_[6] = 0x1f83d9ab;
    state_[7] = 0x5be0cd19;
    bit_count_ = 0;
}

void Sha256::transform(const uint8_t chunk[64]) {
    uint32_t w[64];
    for (size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(chunk[i * 4]) << 24) |
               (static_cast<uint32_t>(chunk[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(chunk[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(chunk[i * 4 + 3]));
    }
    for (size_t i = 16; i < 64; ++i) {
        w[i] = theta1(w[i - 2]) + w[i - 7] + theta0(w[i - 15]) + w[i - 16];
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (size_t i = 0; i < 64; ++i) {
        uint32_t t1 = h + sig1(e) + choose(e, f, g) + K[i] + w[i];
        uint32_t t2 = sig0(a) + majority(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const uint8_t* data, size_t length) {
    if (data == nullptr || length == 0) return;

    size_t buffer_idx = (bit_count_ / 8) % 64;
    bit_count_ += static_cast<uint64_t>(length) * 8;

    for (size_t i = 0; i < length; ++i) {
        buffer_[buffer_idx++] = data[i];
        if (buffer_idx == 64) {
            transform(buffer_);
            buffer_idx = 0;
        }
    }
}

void Sha256::update(const std::string& str) {
    update(reinterpret_cast<const uint8_t*>(str.data()), str.size());
}

std::string Sha256::finalize() {
    size_t buffer_idx = (bit_count_ / 8) % 64;
    buffer_[buffer_idx++] = 0x80;

    if (buffer_idx > 56) {
        while (buffer_idx < 64) {
            buffer_[buffer_idx++] = 0x00;
        }
        transform(buffer_);
        buffer_idx = 0;
    }

    while (buffer_idx < 56) {
        buffer_[buffer_idx++] = 0x00;
    }

    for (int i = 7; i >= 0; --i) {
        buffer_[buffer_idx++] = static_cast<uint8_t>((bit_count_ >> (i * 8)) & 0xFF);
    }
    transform(buffer_);

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (size_t i = 0; i < 8; ++i) {
        ss << std::setw(8) << state_[i];
    }
    return ss.str();
}

std::string Sha256::hash_bytes(const uint8_t* data, size_t length) {
    Sha256 ctx;
    ctx.update(data, length);
    return ctx.finalize();
}

std::string Sha256::hash_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return "";

    Sha256 ctx;
    char buf[16384];
    while (file.read(buf, sizeof(buf)) || file.gcount() > 0) {
        ctx.update(reinterpret_cast<const uint8_t*>(buf), static_cast<size_t>(file.gcount()));
    }
    return ctx.finalize();
}

} // namespace netstream

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <cstddef>

namespace netstream {

class Sha256 {
public:
    Sha256();
    void update(const uint8_t* data, size_t length);
    void update(const std::string& str);
    std::string finalize();

    static std::string hash_file(const std::string& filepath);
    static std::string hash_bytes(const uint8_t* data, size_t length);

private:
    void transform(const uint8_t chunk[64]);

    uint32_t state_[8];
    uint64_t bit_count_{0};
    uint8_t buffer_[64];
};

} // namespace netstream

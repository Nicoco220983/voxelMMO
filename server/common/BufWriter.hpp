#pragma once
#include <cstdint>
#include <cstring>

namespace voxelmmo {

/**
 * @brief Lightweight helper to write values sequentially into a pre-allocated byte buffer.
 */
struct BufWriter {
    uint8_t* buf;
    size_t&  offset;

    /** @brief Append a trivially-copyable value and advance the offset. */
    template<typename T>
    void write(const T& v) noexcept {
        std::memcpy(buf + offset, &v, sizeof(T));
        offset += sizeof(T);
    }
};

} // namespace voxelmmo

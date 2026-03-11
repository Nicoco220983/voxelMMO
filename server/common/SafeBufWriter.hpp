#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace voxelmmo {

/**
 * @brief Safe buffer writer that automatically grows the vector.
 * 
 * Guarantees: never writes beyond valid memory, throws on overflow if maxSize set.
 * Performance: resize is amortized O(1), negligible vs network I/O.
 */
class SafeBufWriter {
    std::vector<uint8_t>& buf;
    size_t pos = 0;
    size_t maxSize = 0;  // 0 = unlimited
    
public:
    /**
     * @param buffer Vector to write to
     * @param initialOffset Starting offset (buffer will be resized to at least this)
     */
    explicit SafeBufWriter(std::vector<uint8_t>& buffer, size_t initialOffset = 0) 
        : buf(buffer), pos(initialOffset) {
        // Ensure buffer is at least 'pos' size
        if (buf.size() < pos) buf.resize(pos);
    }
    
    /** @brief Set maximum allowed size (0 = unlimited) */
    void setMaxSize(size_t max) { maxSize = max; }
    
    /** @brief Write a trivially-copyable value */
    template<typename T>
    void write(const T& v) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        constexpr size_t sz = sizeof(T);
        
        // Bounds check if maxSize set
        if (maxSize > 0 && pos + sz > maxSize) {
            throw std::overflow_error("SafeBufWriter: maximum size exceeded");
        }
        
        // Auto-grow vector to fit data
        if (pos + sz > buf.size()) {
            buf.resize(pos + sz);
        }
        
        std::memcpy(buf.data() + pos, &v, sz);
        pos += sz;
    }
    
    /** @brief Current write offset */
    size_t offset() const { return pos; }
    
    /** @brief Shrink buffer to actual written size */
    void finalize() {
        buf.resize(pos);
    }
};

} // namespace voxelmmo

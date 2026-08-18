// tinymp2_bitreader.h
// Minimal MSB-first bit reader over a caller-owned byte buffer. No
// allocation, no copying: it just walks the pointer it was given, which is
// exactly what a header-only microcontroller decoder needs.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace tinymp2 {

class BitReader {
public:
    BitReader() : data_(nullptr), len_bytes_(0), bit_pos_(0) {}
    BitReader(const uint8_t* data, size_t len_bytes)
        : data_(data), len_bytes_(len_bytes), bit_pos_(0) {}

    void reset(const uint8_t* data, size_t len_bytes) {
        data_ = data;
        len_bytes_ = len_bytes;
        bit_pos_ = 0;
    }

    // Reads n bits (0 <= n <= 24) MSB-first, per the MPEG audio bitstream
    // convention. Reading past the end of the buffer returns zero bits for
    // the missing tail instead of faulting, so a truncated final frame just
    // decodes as silence in the missing region rather than crashing.
    uint32_t readBits(int n) {
        uint32_t value = 0;
        while (n > 0) {
            size_t byte_index = bit_pos_ >> 3;
            int bit_offset = static_cast<int>(bit_pos_ & 7);
            int bits_left_in_byte = 8 - bit_offset;
            int take = n < bits_left_in_byte ? n : bits_left_in_byte;
            uint8_t byte = byte_index < len_bytes_ ? data_[byte_index] : 0;
            int shift = bits_left_in_byte - take;
            uint32_t bits = (byte >> shift) & static_cast<uint32_t>((1u << take) - 1u);
            value = (value << take) | bits;
            bit_pos_ += static_cast<size_t>(take);
            n -= take;
        }
        return value;
    }

    void skipBits(size_t n) { bit_pos_ += n; }

    size_t bitPosition() const { return bit_pos_; }
    size_t bytesConsumed() const { return (bit_pos_ + 7) >> 3; }

private:
    const uint8_t* data_;
    size_t len_bytes_;
    size_t bit_pos_;
};

} // namespace tinymp2

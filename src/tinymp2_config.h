// tinymp2_config.h
// Compile-time configuration for the TinyMP2 decoder.
#pragma once

// TinyMP2 only implements MPEG-1 Layer II (mono or stereo). Every frame
// therefore decodes to exactly this many PCM sample-frames.
#define TINYMP2_SAMPLES_PER_FRAME 1152
#define TINYMP2_MAX_CHANNELS 2

// Largest possible MPEG-1 Layer II frame body: 384 kbps @ 32 kHz gives
// floor(144000 * 384 / 32000) + 1 (padding) = 1729 bytes, plus a 4-byte
// header and optional 2-byte CRC. Callers streaming from SD/network can size
// their staging buffer off this constant.
#define TINYMP2_MAX_FRAME_BYTES 1730

// tinymp2_decoder.h
// The MPEG-1 Layer II ("MP2") decoder itself: header parsing, bit
// allocation, scalefactor/sample decoding, and polyphase subband synthesis.
//
// This is a from-scratch C++ port of the well-tested algorithm used by
// pl_mpeg (MIT, Dominic Szablewski / phoboslab), which traces back to kjmp2
// (public domain, Martin J. Fiedler). The numerics are unchanged; the code
// has been restructured into a stack/heap-free, header-only class so it can
// live on a microcontroller: every buffer the decoder needs is a fixed-size
// member array (no malloc/new anywhere), and a single Decoder instance is
// meant to be a long-lived object (global or static), not something you
// construct per frame.
//
// RAM cost: sizeof(Decoder) is ~14 KB (dominated by the 512-tap synthesis
// window and the 2x1024-sample polyphase history buffer). See README.md for
// per-platform guidance -- this rules out classic 8-bit AVR boards, but is
// comfortable on ESP32/ESP8266, RP2040, SAMD21/51, STM32F4+, Teensy, etc.
#pragma once

#include <string.h>

#include "tinymp2_bitreader.h"
#include "tinymp2_config.h"
#include "tinymp2_tables.h"

namespace tinymp2 {

class Decoder {
public:
    struct FrameHeader {
        uint16_t sample_rate = 0;   // Hz
        uint8_t channels = 0;       // 1 = mono, 2 = stereo/joint-stereo/dual
        uint16_t bitrate_kbps = 0;
        uint16_t frame_bytes = 0;   // total bytes incl. 4-byte header (+2 if CRC present)
    };

    static constexpr int kSamplesPerFrame = TINYMP2_SAMPLES_PER_FRAME;
    static constexpr int kMaxChannels = TINYMP2_MAX_CHANNELS;

    Decoder() {
        // D_ is the 512-tap window duplicated back-to-back into 1024
        // entries, so the synthesis loop below can walk a linearly
        // increasing index without ever needing a modulo (this mirrors the
        // reference algorithm's own D[1024] construction exactly).
        memcpy(D_, tables::kSynthesisWindow, sizeof(tables::kSynthesisWindow));
        memcpy(D_ + 512, tables::kSynthesisWindow, sizeof(tables::kSynthesisWindow));
        reset();
    }

    // Resets all decoder state, including the polyphase synthesis history.
    // Call this when seeking or switching streams so stale filter history
    // from a previous stream doesn't bleed into the first decoded frame.
    void reset() {
        memset(allocation_, 0, sizeof(allocation_));
        memset(scale_factor_info_, 0, sizeof(scale_factor_info_));
        memset(scale_factor_, 0, sizeof(scale_factor_));
        memset(sample_, 0, sizeof(sample_));
        memset(V_, 0, sizeof(V_));
        v_pos_ = 0;
        has_header_ = false;
    }

    // Parses the frame header located at data[0] (data[0..1] must be the
    // sync word 0xFFFC/0xFFFD..0xFFFF pattern for MPEG-1 Layer II). Does not
    // consume/advance anything -- it's a pure peek, safe to call repeatedly
    // while more bytes trickle in.
    //
    // Returns true and fills `header` on success. Returns false if `data`
    // does not start with a valid MPEG-1 Layer II header; the caller should
    // then advance by one byte and retry (standard MPEG resync strategy).
    // `len` must be at least 4; a shorter buffer also yields false.
    bool parseHeader(const uint8_t* data, size_t len, FrameHeader& header) const {
        if (len < 4) return false;
        if (data[0] != 0xFF || (data[1] & 0xF0) != 0xF0) return false;

        BitReader br(data, len);
        br.skipBits(11); // sync
        uint32_t version = br.readBits(2);
        uint32_t layer = br.readBits(2);
        bool has_crc = br.readBits(1) == 0;

        if (version != 3 /* MPEG-1 */ || layer != 2 /* Layer II */) return false;

        uint32_t bitrate_index = br.readBits(4);
        if (bitrate_index == 0 || bitrate_index == 15) return false; // free-format / reserved

        uint32_t samplerate_index = br.readBits(2);
        if (samplerate_index == 3) return false; // reserved

        uint32_t padding = br.readBits(1);
        br.skipBits(1); // private bit
        uint32_t mode = br.readBits(2);

        uint16_t bitrate = tables::kBitRateKbps[bitrate_index];
        uint16_t samplerate = tables::kSampleRate[samplerate_index];
        uint32_t frame_bytes = (144000u * bitrate / samplerate) + padding;

        header.sample_rate = samplerate;
        header.channels = (mode == 3 /* mono */) ? 1 : 2;
        header.bitrate_kbps = bitrate;
        header.frame_bytes = static_cast<uint16_t>(frame_bytes + (has_crc ? 2 : 0));
        return true;
    }

    // Decodes one complete MP2 frame starting at data[0] (the sync byte).
    // `len` must be >= the frame_bytes reported by parseHeader (the caller
    // is expected to have buffered a full frame before calling this).
    //
    // Writes exactly kSamplesPerFrame (1152) interleaved PCM sample-frames
    // to pcmOut (capacity must be >= kSamplesPerFrame * channels int16_t
    // values). Returns the number of bytes consumed (== header.frame_bytes)
    // on success, or 0 if the header at data[0] is not a valid MPEG-1 Layer
    // II header (caller should resync by advancing one byte).
    //
    // If headerOut is non-null it receives the parsed header.
    size_t decodeFrame(const uint8_t* data, size_t len, int16_t* pcmOut,
                        FrameHeader* headerOut = nullptr) {
        FrameHeader header;
        if (!parseHeader(data, len, header)) return 0;
        if (headerOut) *headerOut = header;
        if (len < header.frame_bytes) return 0;

        BitReader br(data, len);
        br.skipBits(16);              // sync + version/layer/protection
        uint32_t bitrate_index = br.readBits(4);
        uint32_t samplerate_index = br.readBits(2);
        br.skipBits(1);                // padding (not needed once frame_bytes is known)
        br.skipBits(1);                // private bit
        uint32_t mode = br.readBits(2);

        int bound = 32;
        if (mode == 1 /* joint stereo */) {
            bound = (br.readBits(2) + 1) << 2;
        } else {
            br.skipBits(2);
            bound = (mode == 3 /* mono */) ? 0 : 32;
        }

        br.skipBits(4); // copyright, original, emphasis
        if (!(data[1] & 0x01)) br.skipBits(16); // CRC, if present (protection_bit==0)

        const int channels = (mode == 3) ? 1 : 2;

        // --- Quantizer table selection (ISO 3-B.2 tables a-d) ---
        int class1 = (channels == 1) ? 0 : 1;
        int class2 = tables::kQuantLutStep1[class1][bitrate_index - 1];
        int tab3_packed = tables::kQuantLutStep2[class2][samplerate_index];
        int sblimit = tab3_packed & 63;
        int tab3_row = (tab3_packed >> 6) ? 1 : 0; // high-rate table (A/B) vs low-rate (C/D)

        if (bound > sblimit) bound = sblimit;

        // --- Step 1: bit allocation ---
        for (int sb = 0; sb < bound; sb++) {
            allocation_[0][sb] = readAllocation(br, sb, tab3_row);
            allocation_[1][sb] = readAllocation(br, sb, tab3_row);
        }
        for (int sb = bound; sb < sblimit; sb++) {
            allocation_[0][sb] = allocation_[1][sb] = readAllocation(br, sb, tab3_row);
        }

        // --- Scale factor selection info (SCFSI) ---
        for (int sb = 0; sb < sblimit; sb++) {
            for (int ch = 0; ch < channels; ch++) {
                if (allocation_[ch][sb]) {
                    scale_factor_info_[ch][sb] = static_cast<uint8_t>(br.readBits(2));
                }
            }
            if (channels == 1) scale_factor_info_[1][sb] = scale_factor_info_[0][sb];
        }

        // --- Scale factors ---
        for (int sb = 0; sb < sblimit; sb++) {
            for (int ch = 0; ch < channels; ch++) {
                if (!allocation_[ch][sb]) continue;
                int32_t* sf = scale_factor_[ch][sb];
                switch (scale_factor_info_[ch][sb]) {
                    case 0:
                        sf[0] = static_cast<int32_t>(br.readBits(6));
                        sf[1] = static_cast<int32_t>(br.readBits(6));
                        sf[2] = static_cast<int32_t>(br.readBits(6));
                        break;
                    case 1:
                        sf[0] = sf[1] = static_cast<int32_t>(br.readBits(6));
                        sf[2] = static_cast<int32_t>(br.readBits(6));
                        break;
                    case 2:
                        sf[0] = sf[1] = sf[2] = static_cast<int32_t>(br.readBits(6));
                        break;
                    default: // 3
                        sf[0] = static_cast<int32_t>(br.readBits(6));
                        sf[1] = sf[2] = static_cast<int32_t>(br.readBits(6));
                        break;
                }
            }
            if (channels == 1) {
                scale_factor_[1][sb][0] = scale_factor_[0][sb][0];
                scale_factor_[1][sb][1] = scale_factor_[0][sb][1];
                scale_factor_[1][sb][2] = scale_factor_[0][sb][2];
            }
        }

        // --- Coefficient input and subband synthesis ---
        // A Layer II frame carries 3 "parts" of 4 granules of 12 samples per
        // subband (3*4*12 = 144 samples/subband = 1152 samples/channel).
        int out_pos = 0;
        for (int part = 0; part < 3; part++) {
            for (int granule = 0; granule < 4; granule++) {
                for (int sb = 0; sb < bound; sb++) {
                    readSamples(br, 0, sb, part);
                    readSamples(br, 1, sb, part);
                }
                for (int sb = bound; sb < sblimit; sb++) {
                    readSamples(br, 0, sb, part);
                    sample_[1][sb][0] = sample_[0][sb][0];
                    sample_[1][sb][1] = sample_[0][sb][1];
                    sample_[1][sb][2] = sample_[0][sb][2];
                }
                for (int sb = sblimit; sb < 32; sb++) {
                    sample_[0][sb][0] = sample_[0][sb][1] = sample_[0][sb][2] = 0;
                    sample_[1][sb][0] = sample_[1][sb][1] = sample_[1][sb][2] = 0;
                }

                for (int p = 0; p < 3; p++) {
                    v_pos_ = (v_pos_ - 64) & 1023;
                    int synth_channels = (channels == 1) ? 1 : 2;
                    for (int ch = 0; ch < synth_channels; ch++) {
                        synthGranule(ch, p, out_pos, pcmOut, channels);
                    }
                    out_pos += 32;
                }
            }
        }

        return header.frame_bytes;
    }

private:
    // Steps 3+4 of the quantizer lookup chain: which quantizer spec (if any)
    // applies to this subband, and how many bits its allocation code itself
    // takes to transmit.
    const tables::QuantizerSpec* readAllocation(BitReader& br, int sb, int tab3_row) {
        uint8_t packed = tables::kQuantLutStep3[tab3_row][sb];
        int nbal = packed >> 4;
        int step4_row = packed & 0x0F;
        uint32_t code = br.readBits(nbal);
        int qtab = tables::kQuantLutStep4[step4_row][code];
        return qtab ? &tables::kQuantTab[qtab - 1] : nullptr;
    }

    // Reconstructs a 6-bit transmitted scalefactor index into the fixed-point
    // (Q16.16-ish) multiplier used below. sf==63 is the reserved/mute value.
    static int32_t expandScaleFactor(int32_t sf) {
        if (sf == 63) return 0;
        int shift = sf / 3;
        return (tables::kScaleFactorBase[sf % 3] + ((1 << shift) >> 1)) >> shift;
    }

    void readSamples(BitReader& br, int ch, int sb, int part) {
        const tables::QuantizerSpec* q = allocation_[ch][sb];
        int* out = sample_[ch][sb];

        if (!q) {
            out[0] = out[1] = out[2] = 0;
            return;
        }

        int32_t sf = expandScaleFactor(scale_factor_[ch][sb][part]);

        int raw[3];
        if (q->group) {
            // Non-power-of-two level counts (3, 5, 9) are packed three
            // samples to a single codeword.
            uint32_t code = br.readBits(q->bits);
            int levels = q->levels;
            raw[0] = static_cast<int>(code % levels);
            code /= levels;
            raw[1] = static_cast<int>(code % levels);
            raw[2] = static_cast<int>(code / levels);
        } else {
            raw[0] = static_cast<int>(br.readBits(q->bits));
            raw[1] = static_cast<int>(br.readBits(q->bits));
            raw[2] = static_cast<int>(br.readBits(q->bits));
        }

        // Requantize (ISO 3-D.2 dequantization formula, fixed-point form)
        // and apply the scalefactor in one integer multiply.
        int adj = q->levels;
        int32_t scale = 65536 / (adj + 1);
        int32_t half = ((adj + 1) >> 1) - 1;

        for (int i = 0; i < 3; i++) {
            int32_t val = (half - raw[i]) * scale;
            out[i] = (val * (sf >> 12) + ((val * (sf & 4095) + 2048) >> 12)) >> 12;
        }
    }

    // 32-point cosine matrixing, implemented as the fast 32-input IDCT used
    // by the ISO reference algorithm. Produces one 64-sample "V" vector from
    // the 32 dequantized subband samples belonging to granule-part `part_idx`
    // of subframe `gr`, written into the circular history buffer V_[ch] at
    // the current v_pos_.
    void idct32(int s[32][3], int part_idx, float* d, int dp) {
        float t01, t02, t03, t04, t05, t06, t07, t08, t09, t10, t11, t12,
              t13, t14, t15, t16, t17, t18, t19, t20, t21, t22, t23, t24,
              t25, t26, t27, t28, t29, t30, t31, t32, t33;

        t01 = (float)(s[0][part_idx] + s[31][part_idx]); t02 = (float)(s[0][part_idx] - s[31][part_idx]) * 0.500602998235f;
        t03 = (float)(s[1][part_idx] + s[30][part_idx]); t04 = (float)(s[1][part_idx] - s[30][part_idx]) * 0.505470959898f;
        t05 = (float)(s[2][part_idx] + s[29][part_idx]); t06 = (float)(s[2][part_idx] - s[29][part_idx]) * 0.515447309923f;
        t07 = (float)(s[3][part_idx] + s[28][part_idx]); t08 = (float)(s[3][part_idx] - s[28][part_idx]) * 0.53104259109f;
        t09 = (float)(s[4][part_idx] + s[27][part_idx]); t10 = (float)(s[4][part_idx] - s[27][part_idx]) * 0.553103896034f;
        t11 = (float)(s[5][part_idx] + s[26][part_idx]); t12 = (float)(s[5][part_idx] - s[26][part_idx]) * 0.582934968206f;
        t13 = (float)(s[6][part_idx] + s[25][part_idx]); t14 = (float)(s[6][part_idx] - s[25][part_idx]) * 0.622504123036f;
        t15 = (float)(s[7][part_idx] + s[24][part_idx]); t16 = (float)(s[7][part_idx] - s[24][part_idx]) * 0.674808341455f;
        t17 = (float)(s[8][part_idx] + s[23][part_idx]); t18 = (float)(s[8][part_idx] - s[23][part_idx]) * 0.744536271002f;
        t19 = (float)(s[9][part_idx] + s[22][part_idx]); t20 = (float)(s[9][part_idx] - s[22][part_idx]) * 0.839349645416f;
        t21 = (float)(s[10][part_idx] + s[21][part_idx]); t22 = (float)(s[10][part_idx] - s[21][part_idx]) * 0.972568237862f;
        t23 = (float)(s[11][part_idx] + s[20][part_idx]); t24 = (float)(s[11][part_idx] - s[20][part_idx]) * 1.16943993343f;
        t25 = (float)(s[12][part_idx] + s[19][part_idx]); t26 = (float)(s[12][part_idx] - s[19][part_idx]) * 1.48416461631f;
        t27 = (float)(s[13][part_idx] + s[18][part_idx]); t28 = (float)(s[13][part_idx] - s[18][part_idx]) * 2.05778100995f;
        t29 = (float)(s[14][part_idx] + s[17][part_idx]); t30 = (float)(s[14][part_idx] - s[17][part_idx]) * 3.40760841847f;
        t31 = (float)(s[15][part_idx] + s[16][part_idx]); t32 = (float)(s[15][part_idx] - s[16][part_idx]) * 10.1900081235f;

        t33 = t01 + t31; t31 = (t01 - t31) * 0.502419286188f;
        t01 = t03 + t29; t29 = (t03 - t29) * 0.52249861494f;
        t03 = t05 + t27; t27 = (t05 - t27) * 0.566944034816f;
        t05 = t07 + t25; t25 = (t07 - t25) * 0.64682178336f;
        t07 = t09 + t23; t23 = (t09 - t23) * 0.788154623451f;
        t09 = t11 + t21; t21 = (t11 - t21) * 1.06067768599f;
        t11 = t13 + t19; t19 = (t13 - t19) * 1.72244709824f;
        t13 = t15 + t17; t17 = (t15 - t17) * 5.10114861869f;
        t15 = t33 + t13; t13 = (t33 - t13) * 0.509795579104f;
        t33 = t01 + t11; t01 = (t01 - t11) * 0.601344886935f;
        t11 = t03 + t09; t09 = (t03 - t09) * 0.899976223136f;
        t03 = t05 + t07; t07 = (t05 - t07) * 2.56291544774f;
        t05 = t15 + t03; t15 = (t15 - t03) * 0.541196100146f;
        t03 = t33 + t11; t11 = (t33 - t11) * 1.30656296488f;
        t33 = t05 + t03; t05 = (t05 - t03) * 0.707106781187f;
        t03 = t15 + t11; t15 = (t15 - t11) * 0.707106781187f;
        t03 += t15;
        t11 = t13 + t07; t13 = (t13 - t07) * 0.541196100146f;
        t07 = t01 + t09; t09 = (t01 - t09) * 1.30656296488f;
        t01 = t11 + t07; t07 = (t11 - t07) * 0.707106781187f;
        t11 = t13 + t09; t13 = (t13 - t09) * 0.707106781187f;
        t11 += t13; t01 += t11;
        t11 += t07; t07 += t13;
        t09 = t31 + t17; t31 = (t31 - t17) * 0.509795579104f;
        t17 = t29 + t19; t29 = (t29 - t19) * 0.601344886935f;
        t19 = t27 + t21; t21 = (t27 - t21) * 0.899976223136f;
        t27 = t25 + t23; t23 = (t25 - t23) * 2.56291544774f;
        t25 = t09 + t27; t09 = (t09 - t27) * 0.541196100146f;
        t27 = t17 + t19; t19 = (t17 - t19) * 1.30656296488f;
        t17 = t25 + t27; t27 = (t25 - t27) * 0.707106781187f;
        t25 = t09 + t19; t19 = (t09 - t19) * 0.707106781187f;
        t25 += t19;
        t09 = t31 + t23; t31 = (t31 - t23) * 0.541196100146f;
        t23 = t29 + t21; t21 = (t29 - t21) * 1.30656296488f;
        t29 = t09 + t23; t23 = (t09 - t23) * 0.707106781187f;
        t09 = t31 + t21; t31 = (t31 - t21) * 0.707106781187f;
        t09 += t31; t29 += t09; t09 += t23; t23 += t31;
        t17 += t29; t29 += t25; t25 += t09; t09 += t27;
        t27 += t23; t23 += t19; t19 += t31;
        t21 = t02 + t32; t02 = (t02 - t32) * 0.502419286188f;
        t32 = t04 + t30; t04 = (t04 - t30) * 0.52249861494f;
        t30 = t06 + t28; t28 = (t06 - t28) * 0.566944034816f;
        t06 = t08 + t26; t08 = (t08 - t26) * 0.64682178336f;
        t26 = t10 + t24; t10 = (t10 - t24) * 0.788154623451f;
        t24 = t12 + t22; t22 = (t12 - t22) * 1.06067768599f;
        t12 = t14 + t20; t20 = (t14 - t20) * 1.72244709824f;
        t14 = t16 + t18; t16 = (t16 - t18) * 5.10114861869f;
        t18 = t21 + t14; t14 = (t21 - t14) * 0.509795579104f;
        t21 = t32 + t12; t32 = (t32 - t12) * 0.601344886935f;
        t12 = t30 + t24; t24 = (t30 - t24) * 0.899976223136f;
        t30 = t06 + t26; t26 = (t06 - t26) * 2.56291544774f;
        t06 = t18 + t30; t18 = (t18 - t30) * 0.541196100146f;
        t30 = t21 + t12; t12 = (t21 - t12) * 1.30656296488f;
        t21 = t06 + t30; t30 = (t06 - t30) * 0.707106781187f;
        t06 = t18 + t12; t12 = (t18 - t12) * 0.707106781187f;
        t06 += t12;
        t18 = t14 + t26; t26 = (t14 - t26) * 0.541196100146f;
        t14 = t32 + t24; t24 = (t32 - t24) * 1.30656296488f;
        t32 = t18 + t14; t14 = (t18 - t14) * 0.707106781187f;
        t18 = t26 + t24; t24 = (t26 - t24) * 0.707106781187f;
        t18 += t24; t32 += t18;
        t18 += t14; t26 = t14 + t24;
        t14 = t02 + t16; t02 = (t02 - t16) * 0.509795579104f;
        t16 = t04 + t20; t04 = (t04 - t20) * 0.601344886935f;
        t20 = t28 + t22; t22 = (t28 - t22) * 0.899976223136f;
        t28 = t08 + t10; t10 = (t08 - t10) * 2.56291544774f;
        t08 = t14 + t28; t14 = (t14 - t28) * 0.541196100146f;
        t28 = t16 + t20; t20 = (t16 - t20) * 1.30656296488f;
        t16 = t08 + t28; t28 = (t08 - t28) * 0.707106781187f;
        t08 = t14 + t20; t20 = (t14 - t20) * 0.707106781187f;
        t08 += t20;
        t14 = t02 + t10; t02 = (t02 - t10) * 0.541196100146f;
        t10 = t04 + t22; t22 = (t04 - t22) * 1.30656296488f;
        t04 = t14 + t10; t10 = (t14 - t10) * 0.707106781187f;
        t14 = t02 + t22; t02 = (t02 - t22) * 0.707106781187f;
        t14 += t02; t04 += t14; t14 += t10; t10 += t02;
        t16 += t04; t04 += t08; t08 += t14; t14 += t28;
        t28 += t10; t10 += t20; t20 += t02; t21 += t16;
        t16 += t32; t32 += t04; t04 += t06; t06 += t08;
        t08 += t18; t18 += t14; t14 += t30; t30 += t28;
        t28 += t26; t26 += t10; t10 += t12; t12 += t20;
        t20 += t24; t24 += t02;

        d[dp + 48] = -t33;
        d[dp + 49] = d[dp + 47] = -t21;
        d[dp + 50] = d[dp + 46] = -t17;
        d[dp + 51] = d[dp + 45] = -t16;
        d[dp + 52] = d[dp + 44] = -t01;
        d[dp + 53] = d[dp + 43] = -t32;
        d[dp + 54] = d[dp + 42] = -t29;
        d[dp + 55] = d[dp + 41] = -t04;
        d[dp + 56] = d[dp + 40] = -t03;
        d[dp + 57] = d[dp + 39] = -t06;
        d[dp + 58] = d[dp + 38] = -t25;
        d[dp + 59] = d[dp + 37] = -t08;
        d[dp + 60] = d[dp + 36] = -t11;
        d[dp + 61] = d[dp + 35] = -t18;
        d[dp + 62] = d[dp + 34] = -t09;
        d[dp + 63] = d[dp + 33] = -t14;
        d[dp + 32] = -t05;
        d[dp + 0] = t05; d[dp + 31] = -t30;
        d[dp + 1] = t30; d[dp + 30] = -t27;
        d[dp + 2] = t27; d[dp + 29] = -t28;
        d[dp + 3] = t28; d[dp + 28] = -t07;
        d[dp + 4] = t07; d[dp + 27] = -t26;
        d[dp + 5] = t26; d[dp + 26] = -t23;
        d[dp + 6] = t23; d[dp + 25] = -t10;
        d[dp + 7] = t10; d[dp + 24] = -t15;
        d[dp + 8] = t15; d[dp + 23] = -t12;
        d[dp + 9] = t12; d[dp + 22] = -t19;
        d[dp + 10] = t19; d[dp + 21] = -t20;
        d[dp + 11] = t20; d[dp + 20] = -t13;
        d[dp + 12] = t13; d[dp + 19] = -t24;
        d[dp + 13] = t24; d[dp + 18] = -t31;
        d[dp + 14] = t31; d[dp + 17] = -t02;
        d[dp + 15] = t02; d[dp + 16] = 0.0f;
    }

    // Runs the polyphase synthesis filter for one 32-sample sub-block of one
    // channel: matrixes the dequantized subband samples into the circular
    // history buffer V_, windows 512 samples of V_ against the synthesis
    // window, and writes 32 interleaved PCM samples to pcmOut at out_pos.
    void synthGranule(int ch, int part_idx, int out_pos, int16_t* pcmOut, int channels) {
        idct32(sample_[ch], part_idx, V_[ch], v_pos_);

        float U[32];
        memset(U, 0, sizeof(U));

        int d_index = 512 - (v_pos_ >> 1);
        int v_index = (v_pos_ % 128) >> 1;
        while (v_index < 1024) {
            for (int i = 0; i < 32; ++i) {
                U[i] += D_[d_index++] * V_[ch][v_index++];
            }
            v_index += 128 - 32;
            d_index += 64 - 32;
        }

        d_index -= (512 - 32);
        v_index = (128 - 32 + 1024) - v_index;
        while (v_index < 1024) {
            for (int i = 0; i < 32; ++i) {
                U[i] += D_[d_index++] * V_[ch][v_index++];
            }
            v_index += 128 - 32;
            d_index += 64 - 32;
        }

        // U[] accumulates in the same fixed-point-ish scale as the ISO
        // reference (window values pre-scaled by 2^15, sample values by
        // 2^15), hence the combined 1<<31-ish normalization back to a
        // +-32767 PCM range.
        if (channels == 1) {
            if (ch == 0) {
                for (int j = 0; j < 32; j++) {
                    pcmOut[out_pos + j] = clampPcm(U[j] / -1090519040.0f);
                }
            }
        } else {
            for (int j = 0; j < 32; j++) {
                pcmOut[(out_pos + j) * 2 + ch] = clampPcm(U[j] / -1090519040.0f);
            }
        }
    }

    static int16_t clampPcm(float f) {
        if (f > 1.0f) f = 1.0f;
        if (f < -1.0f) f = -1.0f;
        return static_cast<int16_t>(f * 32767.0f);
    }

    const tables::QuantizerSpec* allocation_[2][32];
    uint8_t scale_factor_info_[2][32];
    int32_t scale_factor_[2][32][3];
    int sample_[2][32][3];

    float D_[1024];
    float V_[2][1024];
    int v_pos_ = 0;
    bool has_header_ = false;
};

} // namespace tinymp2

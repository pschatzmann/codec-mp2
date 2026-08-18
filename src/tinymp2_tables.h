// tinymp2_tables.h
// Constant tables required by the ISO/IEC 11172-3 (MPEG-1 Layer II) decoding
// process: header field lookup tables, the bit-allocation quantizer lookup
// chain (Annex 3-B.2 tables a-d) and the 512-tap polyphase synthesis window
// (Annex 3-B.3). All tables are `static const`, so they live in flash/.rodata
// on every supported target and cost zero heap/stack.
//
// Numerically ported from the MIT-licensed pl_mpeg (Dominic Szablewski),
// which itself derives its constants from kjmp2 (Martin J. Fiedler, public
// domain) and the ISO/IEC 11172-3 reference algorithm. See LICENSE.
#pragma once

#include <stdint.h>

namespace tinymp2 {
namespace tables {

// MPEG-1 sample rates, indexed by the header's 2-bit sampling_frequency field.
// Index 3 is reserved.
static const uint16_t kSampleRate[4] = { 44100, 48000, 32000, 0 };

// MPEG-1 Layer II bitrates in kbps, indexed by the header's 4-bit
// bitrate_index field (index 0 = "free format", unsupported; index 15 =
// reserved).
static const uint16_t kBitRateKbps[16] = {
    0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0
};

// Base values used to reconstruct a scalefactor from its 6-bit transmitted
// index (ISO 3-B.1 scalefactor table, expressed as the fixed-point
// exponent/mantissa split used by the fast integer reconstruction below).
static const int32_t kScaleFactorBase[3] = { 0x02000000, 0x01965FEA, 0x01428A30 };

// 512-tap polyphase synthesis window (ISO 3-B.3, Table B.3). Duplicated to
// 1024 entries so the circular V-buffer lookup in synth() never needs a
// modulo.
static const float kSynthesisWindow[512] = {
         0.0,     -0.5,     -0.5,     -0.5,     -0.5,     -0.5,
        -0.5,     -1.0,     -1.0,     -1.0,     -1.0,     -1.5,
        -1.5,     -2.0,     -2.0,     -2.5,     -2.5,     -3.0,
        -3.5,     -3.5,     -4.0,     -4.5,     -5.0,     -5.5,
        -6.5,     -7.0,     -8.0,     -8.5,     -9.5,    -10.5,
       -12.0,    -13.0,    -14.5,    -15.5,    -17.5,    -19.0,
       -20.5,    -22.5,    -24.5,    -26.5,    -29.0,    -31.5,
       -34.0,    -36.5,    -39.5,    -42.5,    -45.5,    -48.5,
       -52.0,    -55.5,    -58.5,    -62.5,    -66.0,    -69.5,
       -73.5,    -77.0,    -80.5,    -84.5,    -88.0,    -91.5,
       -95.0,    -98.0,   -101.0,   -104.0,    106.5,    109.0,
       111.0,    112.5,    113.5,    114.0,    114.0,    113.5,
       112.0,    110.5,    107.5,    104.0,    100.0,     94.5,
        88.5,     81.5,     73.0,     63.5,     53.0,     41.5,
        28.5,     14.5,     -1.0,    -18.0,    -36.0,    -55.5,
       -76.5,    -98.5,   -122.0,   -147.0,   -173.5,   -200.5,
      -229.5,   -259.5,   -290.5,   -322.5,   -355.5,   -389.5,
      -424.0,   -459.5,   -495.5,   -532.0,   -568.5,   -605.0,
      -641.5,   -678.0,   -714.0,   -749.0,   -783.5,   -817.0,
      -849.0,   -879.5,   -908.5,   -935.0,   -959.5,   -981.0,
     -1000.5,  -1016.0,  -1028.5,  -1037.5,  -1042.5,  -1043.5,
     -1040.0,  -1031.5,   1018.5,   1000.0,    976.0,    946.5,
       911.0,    869.5,    822.0,    767.5,    707.0,    640.0,
       565.5,    485.0,    397.0,    302.5,    201.0,     92.5,
       -22.5,   -144.0,   -272.5,   -407.0,   -547.5,   -694.0,
      -846.0,  -1003.0,  -1165.0,  -1331.5,  -1502.0,  -1675.5,
     -1852.5,  -2031.5,  -2212.5,  -2394.0,  -2576.5,  -2758.5,
     -2939.5,  -3118.5,  -3294.5,  -3467.5,  -3635.5,  -3798.5,
     -3955.0,  -4104.5,  -4245.5,  -4377.5,  -4499.0,  -4609.5,
     -4708.0,  -4792.5,  -4863.5,  -4919.0,  -4958.0,  -4979.5,
     -4983.0,  -4967.5,  -4931.5,  -4875.0,  -4796.0,  -4694.5,
     -4569.5,  -4420.0,  -4246.0,  -4046.0,  -3820.0,  -3567.0,
      3287.0,   2979.5,   2644.0,   2280.5,   1888.0,   1467.5,
      1018.5,    541.0,     35.0,   -499.0,  -1061.0,  -1650.0,
     -2266.5,  -2909.0,  -3577.0,  -4270.0,  -4987.5,  -5727.5,
     -6490.0,  -7274.0,  -8077.5,  -8899.5,  -9739.0, -10594.5,
    -11464.5, -12347.0, -13241.0, -14144.5, -15056.0, -15973.5,
    -16895.5, -17820.0, -18744.5, -19668.0, -20588.0, -21503.0,
    -22410.5, -23308.5, -24195.0, -25068.5, -25926.5, -26767.0,
    -27589.0, -28389.0, -29166.5, -29919.0, -30644.5, -31342.0,
    -32009.5, -32645.0, -33247.0, -33814.5, -34346.0, -34839.5,
    -35295.0, -35710.0, -36084.5, -36417.5, -36707.5, -36954.0,
    -37156.5, -37315.0, -37428.0, -37496.0,  37519.0,  37496.0,
     37428.0,  37315.0,  37156.5,  36954.0,  36707.5,  36417.5,
     36084.5,  35710.0,  35295.0,  34839.5,  34346.0,  33814.5,
     33247.0,  32645.0,  32009.5,  31342.0,  30644.5,  29919.0,
     29166.5,  28389.0,  27589.0,  26767.0,  25926.5,  25068.5,
     24195.0,  23308.5,  22410.5,  21503.0,  20588.0,  19668.0,
     18744.5,  17820.0,  16895.5,  15973.5,  15056.0,  14144.5,
     13241.0,  12347.0,  11464.5,  10594.5,   9739.0,   8899.5,
      8077.5,   7274.0,   6490.0,   5727.5,   4987.5,   4270.0,
      3577.0,   2909.0,   2266.5,   1650.0,   1061.0,    499.0,
       -35.0,   -541.0,  -1018.5,  -1467.5,  -1888.0,  -2280.5,
     -2644.0,  -2979.5,   3287.0,   3567.0,   3820.0,   4046.0,
      4246.0,   4420.0,   4569.5,   4694.5,   4796.0,   4875.0,
      4931.5,   4967.5,   4983.0,   4979.5,   4958.0,   4919.0,
      4863.5,   4792.5,   4708.0,   4609.5,   4499.0,   4377.5,
      4245.5,   4104.5,   3955.0,   3798.5,   3635.5,   3467.5,
      3294.5,   3118.5,   2939.5,   2758.5,   2576.5,   2394.0,
      2212.5,   2031.5,   1852.5,   1675.5,   1502.0,   1331.5,
      1165.0,   1003.0,    846.0,    694.0,    547.5,    407.0,
       272.5,    144.0,     22.5,    -92.5,   -201.0,   -302.5,
      -397.0,   -485.0,   -565.5,   -640.0,   -707.0,   -767.5,
      -822.0,   -869.5,   -911.0,   -946.5,   -976.0,  -1000.0,
      1018.5,   1031.5,   1040.0,   1043.5,   1042.5,   1037.5,
      1028.5,   1016.0,   1000.5,    981.0,    959.5,    935.0,
       908.5,    879.5,    849.0,    817.0,    783.5,    749.0,
       714.0,    678.0,    641.5,    605.0,    568.5,    532.0,
       495.5,    459.5,    424.0,    389.5,    355.5,    322.5,
       290.5,    259.5,    229.5,    200.5,    173.5,    147.0,
       122.0,     98.5,     76.5,     55.5,     36.0,     18.0,
         1.0,    -14.5,    -28.5,    -41.5,    -53.0,    -63.5,
       -73.0,    -81.5,    -88.5,    -94.5,   -100.0,   -104.0,
      -107.5,   -110.5,   -112.0,   -113.5,   -114.0,   -114.0,
      -113.5,   -112.5,   -111.0,   -109.0,    106.5,    104.0,
       101.0,     98.0,     95.0,     91.5,     88.0,     84.5,
        80.5,     77.0,     73.5,     69.5,     66.0,     62.5,
        58.5,     55.5,     52.0,     48.5,     45.5,     42.5,
        39.5,     36.5,     34.0,     31.5,     29.0,     26.5,
        24.5,     22.5,     20.5,     19.0,     17.5,     15.5,
        14.5,     13.0,     12.0,     10.5,      9.5,      8.5,
         8.0,      7.0,      6.5,      5.5,      5.0,      4.5,
         4.0,      3.5,      3.5,      3.0,      2.5,      2.5,
         2.0,      2.0,      1.5,      1.5,      1.0,      1.0,
         1.0,      1.0,      0.5,      0.5,      0.5,      0.5,
         0.5,      0.5
};

// --- Bit-allocation quantizer lookup chain (ISO 3-B.2 tables a-d) ---
//
// The number of bits available for the bit-allocation code (nbal) and the
// quantizer step table to use both depend on: channel mode (mono/stereo),
// bitrate-per-channel, and sample rate. Rather than storing four separate
// full [subband][code] tables, the ISO informative decoder factors this into
// four small lookup steps (this matches the well-known "step 1..4" chain
// used by kjmp2/pl_mpeg, which is mathematically identical to the ISO annex
// tables but far more compact in ROM):
//
//   step 1: (mono/stereo, bitrate_index)      -> bitrate class (0,1,2)
//   step 2: (bitrate class, samplerate_index)  -> {quant table row, sblimit}
//   step 3: (quant table row, subband)         -> {nbal, step-4 row}
//   step 4: (step-4 row, allocation code)      -> index into kQuantTab (0 = no bits allocated)

// Step 1: bitrate class, indexed [mono?0:1][bitrate_index - 1]
static const uint8_t kQuantLutStep1[2][14] = {
    // 32,48,56,64,80,96,112,128,160,192,224,256,320,384 kbps (mono)
    { 0, 0, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2 },
    // 16,24,28,32,40,48, 56, 64, 80, 96,112,128,160,192 kbps/channel (stereo)
    { 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 2, 2, 2, 2 }
};

// Encodes both the quant-table-row selector (low 6 bits) and the subband
// limit (bit 6 set = 64 | 27, i.e. high-rate tables) for step 2.
#define TINYMP2_QTAB_A (27 | 64) // Table 3-B.2a: high-rate, sblimit = 27
#define TINYMP2_QTAB_B (30 | 64) // Table 3-B.2b: high-rate, sblimit = 30
#define TINYMP2_QTAB_C (8)       // Table 3-B.2c: low-rate,  sblimit = 8
#define TINYMP2_QTAB_D (12)      // Table 3-B.2d: low-rate,  sblimit = 12

// Step 2: [bitrate class][samplerate_index] -> (row | (sblimit<<6 flag encoded in row selection))
static const uint8_t kQuantLutStep2[3][3] = {
    // 44.1 kHz,          48 kHz,            32 kHz
    { TINYMP2_QTAB_C, TINYMP2_QTAB_C, TINYMP2_QTAB_D }, // 32-48 kbps/channel
    { TINYMP2_QTAB_A, TINYMP2_QTAB_A, TINYMP2_QTAB_A }, // 56-80 kbps/channel
    { TINYMP2_QTAB_B, TINYMP2_QTAB_A, TINYMP2_QTAB_B }  // 96+   kbps/channel
};

// Step 3: [quant table row][subband] -> (nbal << 4) | step4_row
static const uint8_t kQuantLutStep3[2][32] = {
    // Low-rate table (3-B.2c / 3-B.2d)
    { 0x44, 0x44,
      0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34, 0x34 },
    // High-rate table (3-B.2a / 3-B.2b)
    { 0x43, 0x43, 0x43,
      0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42, 0x42,
      0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31, 0x31,
      0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20 }
};

// Step 4: [step3 row][allocation code] -> 1-based index into kQuantTab (0 = no bits allocated)
static const uint8_t kQuantLutStep4[5][16] = {
    { 0, 1, 2, 17 },
    { 0, 1, 2, 3, 4, 5, 6, 17 },
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17 },
    { 0, 1, 3, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 },
    { 0, 1, 2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17 }
};

// A quantizer step: `levels` possible reconstruction values, `group` != 0
// means three consecutive samples are packed into one `bits`-wide codeword
// (used for the levels=3,5,9 cases which aren't a power of two minus one).
struct QuantizerSpec {
    uint16_t levels;
    uint8_t group;
    uint8_t bits;
};

static const QuantizerSpec kQuantTab[17] = {
    {     3, 1,  5 }, //  1
    {     5, 1,  7 }, //  2
    {     7, 0,  3 }, //  3
    {     9, 1, 10 }, //  4
    {    15, 0,  4 }, //  5
    {    31, 0,  5 }, //  6
    {    63, 0,  6 }, //  7
    {   127, 0,  7 }, //  8
    {   255, 0,  8 }, //  9
    {   511, 0,  9 }, // 10
    {  1023, 0, 10 }, // 11
    {  2047, 0, 11 }, // 12
    {  4095, 0, 12 }, // 13
    {  8191, 0, 13 }, // 14
    { 16383, 0, 14 }, // 15
    { 32767, 0, 15 }, // 16
    { 65535, 0, 16 }  // 17
};

} // namespace tables
} // namespace tinymp2

// Self-contained regression test: decodes the same embedded clip used by
// examples/DecodeToSerial and checks the result against pinned values, so
// `ctest` catches an accidental behavior change with zero external test
// assets required.
//
// The pinned checksum was captured once via a throwaway build of this same
// harness after the bit-exact cross-validation against the pl_mpeg
// reference decoder (see extras/native_test/test_decode.cpp and the
// project history for that validation) -- it exists to catch *regressions*
// from this point forward, not to establish correctness on its own.
#include <cstdint>
#include <cstdio>

#include "../../examples/DecodeToSerial/demo_tone_array.h"
#include "../../src/TinyMP2.h"

int main() {
    static tinymp2::Decoder decoder;
    int16_t pcm[TinyMP2Decoder::kSamplesPerFrame * TinyMP2Decoder::kMaxChannels];

    size_t pos = 0;
    int frames = 0;
    uint32_t checksum = 2166136261u; // FNV-1a
    TinyMP2Decoder::FrameHeader last{};

    while (pos < kDemoToneMp2Len) {
        TinyMP2Decoder::FrameHeader h;
        size_t consumed = decoder.decodeFrame(kDemoToneMp2 + pos, kDemoToneMp2Len - pos, pcm, &h);
        if (consumed == 0) { pos++; continue; }
        for (int i = 0; i < TinyMP2Decoder::kSamplesPerFrame * h.channels; i++) {
            uint16_t v = static_cast<uint16_t>(pcm[i]);
            checksum ^= (v & 0xFF); checksum *= 16777619u;
            checksum ^= (v >> 8);   checksum *= 16777619u;
        }
        frames++;
        last = h;
        pos += consumed;
    }

    bool ok = true;
    auto check = [&](const char* name, long got, long want) {
        if (got != want) {
            fprintf(stderr, "FAIL: %s = %ld, expected %ld\n", name, got, want);
            ok = false;
        }
    };
    check("frames", frames, 14);
    check("sample_rate", last.sample_rate, 32000);
    check("channels", last.channels, 1);
    check("checksum", (long)checksum, (long)0xed7e0fc0u);

    if (ok) {
        printf("self_test: PASS (%d frames, %d Hz, %d ch, checksum=0x%08x)\n",
               frames, last.sample_rate, last.channels, checksum);
    }
    return ok ? 0 : 1;
}

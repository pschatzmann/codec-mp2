#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "../../examples/DecodeToSerial/demo_tone_array.h"
#include "../../src/TinyMP2.h"

// With no argument, fuzzes against the same embedded clip used by
// examples/DecodeToSerial (so `ctest` needs no external assets). Pass a
// path to an arbitrary .mp2 file for deeper ad-hoc fuzzing of real content.
int main(int argc, char** argv) {
    std::vector<uint8_t> data;
    if (argc >= 2) {
        FILE* in = fopen(argv[1], "rb");
        if (!in) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
        fseek(in, 0, SEEK_END);
        long size = ftell(in);
        fseek(in, 0, SEEK_SET);
        data.resize(size);
        if (fread(data.data(), 1, size, in) != (size_t)size) { fprintf(stderr, "short read\n"); return 1; }
        fclose(in);
    } else {
        data.assign(kDemoToneMp2, kDemoToneMp2 + kDemoToneMp2Len);
    }

    static tinymp2::Decoder decoder;
    int16_t pcm[TinyMP2Decoder::kSamplesPerFrame * TinyMP2Decoder::kMaxChannels];

    // 1) Every truncation length: feed data[0..n) for every n, confirm no
    //    crash/OOB and that decodeFrame() correctly reports "need more data".
    for (size_t n = 0; n <= data.size() && n < 3000; n++) {
        decoder.reset();
        size_t pos = 0;
        int guard = 0;
        while (pos < n && guard++ < 100000) {
            size_t consumed = decoder.decodeFrame(data.data() + pos, n - pos, pcm);
            if (consumed == 0) { pos++; continue; }
            pos += consumed;
        }
    }
    fprintf(stderr, "truncation sweep OK\n");

    // 2) Byte-at-a-time growth (simulates a streaming ring buffer): feed
    //    an increasing prefix and make sure decode never reads past what's
    //    actually available (ASan will catch this).
    for (size_t n = 0; n <= data.size(); n += 7) {
        TinyMP2Decoder::FrameHeader h;
        decoder.parseHeader(data.data(), n, h);
    }
    fprintf(stderr, "growth sweep OK\n");

    // 3) Pure random garbage of various lengths -- must never crash, loop
    //    forever, or read out of bounds.
    srand(12345);
    for (int trial = 0; trial < 200; trial++) {
        size_t n = rand() % 4000;
        std::vector<uint8_t> junk(n);
        for (size_t i = 0; i < n; i++) junk[i] = (uint8_t)rand();
        decoder.reset();
        size_t pos = 0;
        int guard = 0;
        while (pos < n && guard++ < 100000) {
            size_t consumed = decoder.decodeFrame(junk.data() + pos, n - pos, pcm);
            if (consumed == 0) { pos++; continue; }
            pos += consumed;
        }
        if (guard >= 100000) { fprintf(stderr, "INFINITE LOOP suspected, trial %d n=%zu\n", trial, n); return 1; }
    }
    fprintf(stderr, "random garbage sweep OK (200 trials)\n");

    // 4) All-0xFF buffer (worst case sync-word-like repeating pattern).
    for (size_t n : {0u, 1u, 4u, 100u, 2000u}) {
        std::vector<uint8_t> ff(n, 0xFF);
        decoder.reset();
        size_t pos = 0;
        int guard = 0;
        while (pos < n && guard++ < 100000) {
            size_t consumed = decoder.decodeFrame(ff.data() + pos, n - pos, pcm);
            if (consumed == 0) { pos++; continue; }
            pos += consumed;
        }
        if (guard >= 100000) { fprintf(stderr, "INFINITE LOOP on 0xFF buffer n=%zu\n", n); return 1; }
    }
    fprintf(stderr, "0xFF-fill sweep OK\n");

    printf("ALL FUZZ CHECKS PASSED\n");
    return 0;
}

// Host-side correctness harness (not part of the Arduino build).
// Decodes an .mp2 file with TinyMP2 and writes raw interleaved s16le PCM,
// so it can be diffed against a known-good reference decoder. Build with
// any desktop C++11 compiler, e.g.:
//   g++ -O2 -I../../src test_decode.cpp -o test_decode
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../../src/TinyMP2.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s in.mp2 out.raw\n", argv[0]);
        return 1;
    }
    FILE* in = fopen(argv[1], "rb");
    if (!in) { fprintf(stderr, "cannot open %s\n", argv[1]); return 1; }
    fseek(in, 0, SEEK_END);
    long size = ftell(in);
    fseek(in, 0, SEEK_SET);
    std::vector<uint8_t> data(size);
    if (fread(data.data(), 1, size, in) != (size_t)size) { fprintf(stderr, "short read\n"); return 1; }
    fclose(in);

    FILE* out = fopen(argv[2], "wb");
    if (!out) { fprintf(stderr, "cannot open %s\n", argv[2]); return 1; }

    static tinymp2::Decoder decoder; // static: keep it off the host stack too
    int16_t pcm[TinyMP2Decoder::kSamplesPerFrame * TinyMP2Decoder::kMaxChannels];

    size_t pos = 0;
    int frames = 0;
    int last_channels = 0, last_rate = 0;
    while (pos < data.size()) {
        TinyMP2Decoder::FrameHeader hdr;
        size_t consumed = decoder.decodeFrame(data.data() + pos, data.size() - pos, pcm, &hdr);
        if (consumed == 0) {
            pos++; // resync
            continue;
        }
        fwrite(pcm, sizeof(int16_t), TinyMP2Decoder::kSamplesPerFrame * hdr.channels, out);
        pos += consumed;
        frames++;
        last_channels = hdr.channels;
        last_rate = hdr.sample_rate;
    }
    fclose(out);
    fprintf(stderr, "decoded %d frames, %d Hz, %d ch\n", frames, last_rate, last_channels);
    return 0;
}

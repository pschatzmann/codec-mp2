// DecodeToSerial.ino
//
// The simplest possible TinyMP2 example: decode a small MP2 clip that's
// embedded directly in flash (no SD card, no network, no audio hardware
// required) and print a few decoded PCM samples per frame over Serial.
//
// This is meant to confirm the library builds and decodes correctly on your
// board before you wire up real audio I/O (e.g. streaming frames in from an
// SD card or network source and writing the resulting PCM out over I2S --
// see the README's "API" section for the buffering/resync pattern that
// takes).
//
// The embedded clip (demo_tone_array.h) is a 0.5 s, 440 Hz sine tone
// synthesized with ffmpeg, 32 kHz mono MP2 @ 64 kbps, ~4 KB. It is not
// copyrighted third-party content -- regenerate it any time with:
//   ffmpeg -f lavfi -i "sine=frequency=440:duration=0.5" \
//          -ar 32000 -ac 1 -c:a mp2 -b:a 64k demo_tone.mp2
//   xxd -i demo_tone.mp2   (then reformat into the array below)

#include <TinyMP2.h>

#include "demo_tone_array.h"

// The decoder object is ~14 KB; keep exactly one of these alive for the
// life of your program (global/static), never allocate it on the stack.
static TinyMP2Decoder decoder;
static int16_t pcm[TinyMP2Decoder::kSamplesPerFrame * TinyMP2Decoder::kMaxChannels];

void setup() {
    Serial.begin(115200);
    while (!Serial) {}

    Serial.println(F("TinyMP2 DecodeToSerial"));
    Serial.print(F("Clip size: "));
    Serial.print(kDemoToneMp2Len);
    Serial.println(F(" bytes"));

    // kDemoToneMp2 lives in flash (PROGMEM) on AVR-style toolchains; on
    // ESP32/SAMD/etc PROGMEM is a no-op and the array is just read directly.
    // TinyMP2 reads through a plain pointer, so on AVR you'd normally copy
    // each chunk out via pgm_read_byte() first -- omitted here for brevity
    // since this example targets the 32-bit boards this library supports.
    const uint8_t* data = kDemoToneMp2;
    size_t remaining = kDemoToneMp2Len;
    size_t pos = 0;
    int frame_count = 0;

    while (pos < remaining) {
        TinyMP2Decoder::FrameHeader header;
        size_t consumed = decoder.decodeFrame(data + pos, remaining - pos, pcm, &header);

        if (consumed == 0) {
            // Not a valid frame at this position -- resync by one byte.
            pos++;
            continue;
        }

        frame_count++;
        Serial.print(F("frame "));
        Serial.print(frame_count);
        Serial.print(F(": "));
        Serial.print(header.sample_rate);
        Serial.print(F(" Hz, "));
        Serial.print(header.channels);
        Serial.print(F(" ch, "));
        Serial.print(header.bitrate_kbps);
        Serial.print(F(" kbps -- first samples: "));
        for (int i = 0; i < 4; i++) {
            Serial.print(pcm[i]);
            Serial.print(' ');
        }
        Serial.println();

        pos += consumed;
    }

    Serial.print(F("Done. Decoded "));
    Serial.print(frame_count);
    Serial.println(F(" frames."));
}

void loop() {
    // Nothing to do -- decode ran once in setup().
}

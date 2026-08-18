# TinyMP2

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![CMake](https://img.shields.io/badge/CMake-Supported-blue.svg)](https://cmake.org/)
[![IDF Component](https://img.shields.io/badge/IDF-Component-blue.svg)](https://github.com/pschatzmann/TinyMP2)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A header-only MPEG-1 Layer II ("MP2") audio decoder in C++, built for
microcontrollers: no dynamic allocation anywhere in the decode path, no
external dependencies, and a fixed, known-at-compile-time RAM cost.

MP2 is the audio codec used by DAB/DAB+ digital radio, DVB broadcast, and a
lot of legacy broadcast/production audio -- this decoder turns MP2 frames
into 16-bit PCM you can feed straight to I2S, a DAC, or a WAV file.

## Status and provenance

The core algorithm and numeric tables (bit-allocation quantizer tables, the
512-tap synthesis window, the dequantization formula, polyphase synthesis)
are ported from [PL_MPEG](https://github.com/phoboslab/pl_mpeg) (MIT,
Dominic Szablewski), which traces back to
[kjmp2](http://keyj.emphy.de/kjmp2/) by Martin J. Fiedler. See
[LICENSE](LICENSE) for full attribution.

The port has been cross-validated **bit-exact** against the PL_MPEG
reference decoder across every channel mode and quantizer table Layer II
defines: mono, stereo, dual-channel, and joint-stereo (including a stream
that legitimately switches mode frame-to-frame), at both the high-rate
(Table 3-B.2a/b) and low-rate (3-B.2c/d) quantizer tables. It has also been
run under ASan/UBSan against truncated, byte-at-a-time-streamed, and random
garbage input with no crashes, no out-of-bounds access, and no infinite
loops. See [Testing](#testing) to reproduce this.

What hasn't been done: testing against real-world broadcast capture files
(only synthesized/encoder-generated test signals so far) or on physical
microcontroller hardware (verified to compile cleanly for ESP32 via
arduino-cli; RAM/flash budget below is measured from that build, not
estimated).

## Memory budget

`sizeof(tinymp2::Decoder)` is **~14 KB** (14,408 bytes measured on a 64-bit
desktop build; layout-dependent padding may shift this slightly per
target), dominated by:

- the 512-tap synthesis window, duplicated to 1024 entries (4 KB)
- the polyphase filter history, 2 channels x 1024 floats (8 KB)
- bit-allocation/scalefactor/sample state for the current frame (~2 KB)

There is no heap use and no recursion anywhere in the decode path -- every
buffer is a fixed-size member of the `Decoder` object. Keep exactly one
instance alive for the life of your program (`static`/global), never on the
stack, and never construct one per frame.

This makes TinyMP2 a fit for 32-bit boards with a few tens of KB of free
RAM. `examples/DecodeToSerial` (the decoder plus its own ~4 KB embedded
test clip) compiles to, measured via `arduino-cli`:

| Board | Flash | RAM |
|---|---|---|
| ESP32 | 281 KB / 1.3 MB (21%) | 41 KB / 320 KB (12%) |
| RP2040 (Pico) | 67 KB / 2.0 MB (3%) | 28 KB / 256 KB (10%) |
| SAMD21 (MKR Zero) | 28 KB / 256 KB (10%) | 21 KB / 32 KB (**64%**) |

ESP32, ESP8266, RP2040, SAMD51, STM32F4-and-up, and Teensy all have plenty
of headroom. Base SAMD21 (32 KB total RAM) fits but is tight, as the table
shows -- fine if the decoder is most of what your sketch does, worth
watching if it's one piece of a bigger program. It rules out classic 8-bit
AVR (Uno/Nano/Mega) outright: 14 KB alone exceeds an Uno's total 2 KB of
SRAM.

## API

```cpp
#include <TinyMP2.h>

static TinyMP2Decoder decoder;   // ~14 KB -- keep this static/global
int16_t pcm[TinyMP2Decoder::kSamplesPerFrame * TinyMP2Decoder::kMaxChannels];

// data[0] must point at the frame sync byte (0xFF, 0xFx). Returns the
// number of bytes consumed (== the frame size) on success, or 0 if data[0]
// isn't a valid MPEG-1 Layer II header -- advance by one byte and retry.
TinyMP2Decoder::FrameHeader header;
size_t consumed = decoder.decodeFrame(data, available, pcm, &header);
if (consumed) {
    // pcm[0 .. kSamplesPerFrame*header.channels) is interleaved PCM
    // (1152 sample-frames per Layer II frame, always).
}
```

- `bool parseHeader(data, len, header)` -- peek at a header without
  decoding, e.g. to check `header.frame_bytes` before you've buffered a
  whole frame.
- `size_t decodeFrame(data, len, pcmOut, headerOut = nullptr)` -- parse +
  decode one frame in one call.
- `void reset()` -- clears all state, including the polyphase filter
  history. Call this when seeking or switching streams.

For a streaming source (SD card, network, ring buffer) that doesn't hand
you whole frames at once: buffer bytes into a staging array at least
`TINYMP2_MAX_FRAME_BYTES` (1730) bytes long, call `decodeFrame()` against
its front, advance by the returned consumed count, and `memmove()` any
leftover partial frame to the front before refilling. See
`examples/DecodeToSerial` for the framing/resync loop itself (against an
in-memory buffer); adapt the same loop to your I/O source.

## Using TinyMP2 with arduino-audio-tools

TinyMP2 is deliberately just a decoder: it turns bytes in, PCM out, with no
opinion on where the bytes come from or where the PCM goes. If you want I2S
output, Bluetooth, HTTP streaming, SD playback, resampling, mixing, or any
of the other plumbing around that, pair it with
[arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools) --
a much larger audio framework (same author) providing `AudioStream`
abstractions, dozens of codecs, and I/O backends for most of what an
Arduino audio project needs.

audio-tools already ships a ready-made adapter,
[`MP2Decoder`](https://github.com/pschatzmann/arduino-audio-tools/blob/main/src/AudioTools/AudioCodecs/CodecMP2.h)
in `AudioTools/AudioCodecs/CodecMP2.h`, whose header explicitly says
"Depends on https://github.com/pschatzmann/TinyMP2". It wraps a
`TinyMP2Decoder` as an `AudioDecoder` (audio-tools' push-based decoder
interface: compressed bytes in via `write()`, PCM out to a `Print`/
`AudioStream` you set with `setOutput()`) -- internally it does exactly the
buffer/resync loop described under [API](#api) above, so compressed data
handed to it doesn't need to be frame-aligned.

A minimal playback sketch looks like:

```cpp
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP2.h"

I2SStream i2s;
MP2Decoder mp2;
EncodedAudioStream decoder(&i2s, &mp2); // MP2 -> PCM -> i2s

void setup() {
    auto cfg = i2s.defaultConfig(TX_MODE);
    cfg.channels = 2;
    cfg.bits_per_sample = 16;
    i2s.begin(cfg);
    decoder.begin();
}

void loop() {
    // feed compressed MP2 bytes from wherever they come from (SD, HTTP,
    // Bluetooth, ...) -- decoder.write() re-derives the real sample
    // rate/channel count from each frame header and reconfigures the
    // output stream automatically when it changes.
    decoder.write(mp2_bytes, mp2_bytes_len);
}
```

(Check the current audio-tools API/examples for your board -- `I2SStream`
config fields and constructor shapes have shifted between releases.)

### Containers

TinyMP2 only speaks a raw MP2 elementary stream -- back-to-back frames with
their own sync headers, nothing wrapping them (see
[Limitations](#limitations--out-of-scope)). Real-world MP2 audio is often
wrapped in something else: an MPEG-1 Program Stream (`.mpg`), an MPEG-TS
broadcast stream, or a media container like AVI. audio-tools provides
several container/demuxer classes under `AudioTools/AudioCodecs/` to strip
that wrapper before the bytes ever reach `MP2Decoder`:

- **`DemuxerMPG`** (`ContainerMPG.h`) -- demuxes an ISO/IEC 11172-1
  MPEG-1 Program Stream, splitting out the raw MPEG audio elementary stream
  (any of Layer I/II/III) and forwarding it to whichever `AudioDecoder`
  you've wired up via `setOutputAudio()` -- `MP2Decoder` included. This is
  the counterpart to producing a `.mpg` file in the first place, and is the
  one most directly relevant to MP2 content.
- **`ContainerBinary`, `ContainerAVI`, `ContainerOgg`, `ContainerM4A`/
  `ContainerMP4`** -- framing/demuxing for other container formats, for
  when your compressed MP2 (or other codec) data arrives wrapped
  differently.
- **`MultiDecoder`** -- auto-detects the compressed format and dispatches
  to the right decoder, useful if you don't know ahead of time whether
  you're getting raw MP2, MP3, WAV, etc.
- **`CodecChain`** -- chains a demuxer and a decoder (or several codecs)
  together as a single pipeline stage.

Note on licensing: TinyMP2 itself is MIT-licensed (see [LICENSE](LICENSE))
and usable standalone under those terms regardless of the above.
arduino-audio-tools as a whole (including `CodecMP2.h`) is GPLv3-licensed,
so a project built on top of it inherits GPLv3 terms for that project.

## Installation

For Arduino, download this library as a zip and use Library ->
Include Library -> Add .ZIP Library. Or git clone this project into
your Arduino libraries folder, e.g.

```
cd ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinyMP2.git
```

No external Arduino library dependencies -- just the C++ standard headers
(`<stdint.h>`, `<string.h>`).

For CMake or ESP-IDF projects instead, see [Testing](#testing) below for
`add_subdirectory()`/`EXTRA_COMPONENT_DIRS` usage.

## Testing

TinyMP2 ships a desktop CMake/CTest suite under `extras/native_test/`
(ignored by the Arduino build, which only looks at `src/` and `examples/`):

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

- `self_test` decodes the same embedded clip used by
  `examples/DecodeToSerial` and checks the result (frame count, sample
  rate, channel count, and a checksum of the PCM output) against pinned
  values, to catch regressions with zero external test assets.
- `fuzz_test` runs the truncation/streaming/random-garbage robustness sweep
  described above.
- `test_decode` is a standalone utility (not a ctest) for decoding an
  arbitrary `.mp2` file to raw interleaved s16le PCM, e.g. to diff against
  a reference decoder by hand:
  `./build/extras/native_test/test_decode some.mp2 out.raw`

As an ESP-IDF component, add this repository to your project's
`EXTRA_COMPONENT_DIRS` (or drop it into `components/`) and
`#include "TinyMP2.h"`; the root `CMakeLists.txt` detects `ESP_PLATFORM`
and registers itself as a component automatically.

## Limitations / out of scope

- **MPEG-1 Layer II only.** Layer I, Layer III (MP3), and MPEG-2/2.5
  low-sample-rate extensions are not implemented; `decodeFrame()` rejects
  their headers (returns 0).
- **No container demuxing.** This decodes a raw MP2 elementary stream
  (back-to-back frames with sync headers), not `.wav`, `.ts`, or any other
  container -- strip/demux those yourself first, or see
  [Containers](#containers) above for arduino-audio-tools classes that do
  this for you.
- **Float internally.** The synthesis filter uses `float` for simplicity
  and to keep the port easy to validate against the reference decoder;
  boards without an FPU (e.g. RP2040, classic SAMD21) will pay a
  soft-float cost per sample. A fixed-point synthesis path is a plausible
  future optimization but isn't implemented here.
- **No CRC verification.** The optional 16-bit CRC (protection_bit == 0)
  is skipped over, not checked, matching the reference decoder's behavior.

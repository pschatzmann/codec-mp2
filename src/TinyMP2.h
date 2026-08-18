// TinyMP2.h
// Header-only MPEG-1 Layer II ("MP2") audio decoder for microcontrollers.
//
//   #include <TinyMP2.h>
//   tinymp2::Decoder decoder;                 // ~14 KB, keep it static/global
//   int16_t pcm[TinyMP2Decoder::kSamplesPerFrame * 2];
//   auto consumed = decoder.decodeFrame(mp2_bytes, available, pcm);
//
// See README.md for the full API, memory budget, and a streaming example.
#pragma once

#include "tinymp2_config.h"
#include "tinymp2_bitreader.h"
#include "tinymp2_tables.h"
#include "tinymp2_decoder.h"

using TinyMP2Decoder = tinymp2::Decoder;

// AudioToolbox on AAudio (docs/LAUNCHER.md, Stage 3b): an output AudioUnit configured the way
// FMOD's iOS output does it pulls audio through its render callback with buffers shaped by the
// stream format the unit was given (float interleaved, int16 non-interleaved), never past them.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atomic>

#include "audio_toolbox.h"

static int fails = 0;
#define EXPECT(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

namespace {

using namespace mcfm::audio;

constexpr uint32_t fourcc(const char (&s)[5]) {
  return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]);
}

struct Capture {
  std::atomic<int> calls{0}, frames{0}, bad_shape{0};
  uint32_t channels = 0, bytes_per_sample = 0;
  bool interleaved = true;
  double last_sample_time = -1;
  std::atomic<int> time_not_increasing{0};
};

int32_t render(void *ref, uint32_t *, const AudioTimeStamp *ts, uint32_t bus, uint32_t frames, AudioBufferList *list) {
  Capture *c = static_cast<Capture *>(ref);
  c->calls++;
  c->frames += static_cast<int>(frames);
  if (bus != 0 || !list) c->bad_shape++;
  uint32_t expect_buffers = c->interleaved ? 1 : c->channels;
  if (list->mNumberBuffers != expect_buffers) c->bad_shape++;
  for (uint32_t i = 0; i < list->mNumberBuffers; i++) {
    AudioBuffer &b = list->mBuffers[i];
    uint32_t want_channels = c->interleaved ? c->channels : 1;
    if (b.mNumberChannels != want_channels || b.mDataByteSize != frames * want_channels * c->bytes_per_sample) c->bad_shape++;
    memset(b.mData, 0x11, b.mDataByteSize);  // fill exactly what was given (overruns would trip guards)
  }
  if (ts->mSampleTime <= c->last_sample_time) c->time_not_increasing++;
  c->last_sample_time = ts->mSampleTime;
  return 0;
}

void run_case(const char *name, uint32_t format_flags, uint32_t bits, bool interleaved) {
  AudioComponentDescription desc = {fourcc("auou"), fourcc("rioc"), fourcc("appl"), 0, 0};
  void *component = AudioComponentFindNext(nullptr, &desc);
  EXPECT(component != nullptr);
  void *unit = nullptr;
  EXPECT(AudioComponentInstanceNew(component, &unit) == 0 && unit);
  uint32_t one = 1;
  EXPECT(AudioUnitSetProperty(unit, kAudioOutputUnitProperty_EnableIO, kAudioUnitScope_Output, 0, &one, 4) == 0);
  AudioStreamBasicDescription format = {};
  format.mSampleRate = 48000;
  format.mFormatID = fourcc("lpcm");
  format.mFormatFlags = format_flags | kAudioFormatFlagIsPacked | (interleaved ? 0 : kAudioFormatFlagIsNonInterleaved);
  format.mChannelsPerFrame = 2;
  format.mBitsPerChannel = bits;
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = interleaved ? 2 * bits / 8 : bits / 8;
  format.mBytesPerPacket = format.mBytesPerFrame;
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format, sizeof format) == 0);
  AudioStreamBasicDescription back = {};
  uint32_t size = sizeof back;
  EXPECT(AudioUnitGetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &back, &size) == 0);
  EXPECT(size == sizeof back && back.mFormatFlags == format.mFormatFlags && back.mBitsPerChannel == bits);
  uint32_t max_frames = 1024;
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max_frames, 4) == 0);
  Capture capture;
  capture.channels = 2;
  capture.bytes_per_sample = bits / 8;
  capture.interleaved = interleaved;
  AURenderCallbackStruct callback = {render, &capture};
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof callback) == 0);
  EXPECT(AudioUnitInitialize(unit) == 0);
  EXPECT(AudioOutputUnitStart(unit) == 0);
  usleep(1200000);
  EXPECT(AudioOutputUnitStop(unit) == 0);
  int frames = capture.frames.load();
  printf("audio %s: %d callbacks, %d frames\n", name, capture.calls.load(), frames);
  EXPECT(frames > 30000);                  // about 1.2 s at 48 kHz
  EXPECT(capture.bad_shape.load() == 0);
  EXPECT(capture.time_not_increasing.load() == 0);
  EXPECT(AudioUnitUninitialize(unit) == 0);
  EXPECT(AudioComponentInstanceDispose(unit) == 0);
}

}  // namespace

int main() {
  run_case("float interleaved", kAudioFormatFlagIsFloat, 32, true);
  run_case("int16 non-interleaved", kAudioFormatFlagIsSignedInteger, 16, false);

  // AudioSession answers FMOD reads; AudioQueue (a decoder path the game's banks do not use) is
  // unimplemented.
  uint32_t channels = 0, size = 4;
  EXPECT(AudioSessionGetProperty(fourcc("choc"), &size, &channels) == 0 && channels == 2);
  double rate = 0;
  size = 8;
  EXPECT(AudioSessionGetProperty(fourcc("chsr"), &size, &rate) == 0 && rate == 48000.0);
  EXPECT(audio_symbol("AudioQueueNewOutput") != nullptr);
  EXPECT(reinterpret_cast<int32_t (*)()>(audio_symbol("AudioQueueNewOutput"))() == -4);
  EXPECT(audio_symbol("AudioComponentFindNext") == reinterpret_cast<void *>(&AudioComponentFindNext));
  EXPECT(audio_symbol("NotAnAudioFunction") == nullptr);

  if (fails) { printf("%d failure(s)\n", fails); return 1; }
  printf("audio_test: all passed\n");
  return 0;
}

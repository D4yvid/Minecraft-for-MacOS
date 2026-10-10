// AudioToolbox on AAudio (docs/LAUNCHER.md, Stage 3b): an output AudioUnit configured the way
// FMOD's iOS output does it pulls audio through its render callback with buffers shaped by the
// stream format the unit was given (float interleaved, int16 non-interleaved), never past them.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atomic>
#include <vector>

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
    memset(b.mData, 0x11, b.mDataByteSize);  // fill exactly what was given
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

// Renders frame i of channel c as the int16 value 1000 * c + i: the interleaved result shows any
// channel swap, wrong stride or wrong plane offset.
int32_t pattern(void *ref, uint32_t *, const AudioTimeStamp *, uint32_t, uint32_t frames, AudioBufferList *list) {
  int *rendered = static_cast<int *>(ref);
  for (uint32_t c = 0; c < list->mNumberBuffers; c++) {
    int16_t *p = static_cast<int16_t *>(list->mBuffers[c].mData);
    for (uint32_t i = 0; i < frames; i++) p[i] = static_cast<int16_t>(1000 * c + (*rendered + i) % 1000);
  }
  *rendered += static_cast<int>(frames);
  return 0;
}

AudioStreamBasicDescription int16_planar(uint32_t channels) {
  AudioStreamBasicDescription f = {};
  f.mSampleRate = 48000;
  f.mFormatID = fourcc("lpcm");
  f.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked | kAudioFormatFlagIsNonInterleaved;
  f.mChannelsPerFrame = channels;
  f.mBitsPerChannel = 16;
  f.mFramesPerPacket = 1;
  f.mBytesPerFrame = f.mBytesPerPacket = 2;
  return f;
}

void planar_and_slicing() {
  AudioComponentDescription desc = {fourcc("auou"), fourcc("rioc"), fourcc("appl"), 0, 0};
  void *unit = nullptr;
  AudioComponentInstanceNew(AudioComponentFindNext(nullptr, &desc), &unit);
  AudioStreamBasicDescription f = int16_planar(3);
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &f, sizeof f) == 0);
  uint32_t max_frames = 64;  // smaller than the request: rendered in slices
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &max_frames, 4) == 0);
  int rendered = 0;
  AURenderCallbackStruct callback = {pattern, &rendered};
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback, sizeof callback) == 0);
  EXPECT(AudioUnitInitialize(unit) == 0);
  constexpr uint32_t kFrames = 1000;
  std::vector<int16_t> out(kFrames * 3 + 64, 0x5A5A);  // 64 guard samples after the frames
  render_for_test(unit, out.data(), kFrames);
  bool ok = rendered == static_cast<int>(kFrames);
  for (uint32_t i = 0; i < kFrames && ok; i++)
    for (uint32_t c = 0; c < 3; c++) ok &= out[i * 3 + c] == static_cast<int16_t>(1000 * c + i % 1000);
  EXPECT(ok);
  bool guard = true;
  for (size_t i = kFrames * 3; i < out.size(); i++) guard &= out[i] == 0x5A5A;
  EXPECT(guard);

  // Once initialized, the format and the slice size are fixed (Apple: kAudioUnitErr_Initialized);
  // a second Initialize changes nothing.
  AudioStreamBasicDescription stereo = int16_planar(2);
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &stereo, sizeof stereo) == -10849);
  uint32_t bigger = 4096;
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0, &bigger, 4) == -10849);
  EXPECT(AudioUnitInitialize(unit) == 0);
  // The render format is the input scope's element 0; an output-scope format does not replace it.
  AudioUnitUninitialize(unit);
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, 1, &stereo, sizeof stereo) == 0);
  AudioStreamBasicDescription back = {};
  uint32_t size = sizeof back;
  AudioUnitGetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &back, &size);
  EXPECT(back.mChannelsPerFrame == 3);
  // Formats AAudio cannot play are refused (kAudioUnitErr_FormatNotSupported).
  AudioStreamBasicDescription bad = int16_planar(2);
  bad.mBitsPerChannel = 24;
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &bad, sizeof bad) == -10868);
  bad = int16_planar(2);
  bad.mFormatID = fourcc("aac ");
  EXPECT(AudioUnitSetProperty(unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &bad, sizeof bad) == -10868);
  AudioComponentInstanceDispose(unit);
}

}  // namespace

int main() {
  planar_and_slicing();
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

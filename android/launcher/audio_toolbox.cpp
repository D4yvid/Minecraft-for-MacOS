#include "audio_toolbox.h"

#include <aaudio/AAudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace mcfm {
namespace audio {
namespace {

constexpr int32_t kNoErr = 0;
constexpr int32_t kUnimplemented = -4;            // kAudio_UnimplementedError
constexpr int32_t kParamErr = -50;
constexpr int32_t kInvalidProperty = -10879;      // kAudioUnitErr_InvalidProperty
constexpr int32_t kFormatNotSupported = -10868;   // kAudioUnitErr_FormatNotSupported
constexpr int32_t kUninitialized = -10867;        // kAudioUnitErr_Uninitialized
constexpr int32_t kInitialized = -10849;          // kAudioUnitErr_Initialized
constexpr uint32_t kSampleTimeValid = 1;          // kAudioTimeStampSampleTimeValid

constexpr uint32_t fourcc(const char (&s)[5]) {
  return uint32_t(uint8_t(s[0])) << 24 | uint32_t(uint8_t(s[1])) << 16 | uint32_t(uint8_t(s[2])) << 8 | uint8_t(s[3]);
}

// The one component we offer: an output unit (RemoteIO, or the default output).
char g_output_component;

AudioStreamBasicDescription hardware_format() {
  AudioStreamBasicDescription f = {};
  f.mSampleRate = 48000;
  f.mFormatID = fourcc("lpcm");
  f.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  f.mChannelsPerFrame = 2;
  f.mBitsPerChannel = 32;
  f.mFramesPerPacket = 1;
  f.mBytesPerFrame = f.mBytesPerPacket = 8;
  return f;
}

struct Unit {
  AudioStreamBasicDescription format = hardware_format();
  AURenderCallbackStruct callback = {nullptr, nullptr};
  uint32_t max_frames = 4096;
  bool initialized = false;
  AAudioStream *stream = nullptr;
  double sample_time = 0;
  std::mutex lock;  // stream open/close (the app's thread, the error-recovery thread)
  std::vector<uint8_t> planes;  // non-interleaved: per-channel scratch, then interleaved after
  std::vector<uint8_t> list_storage;
};

bool interleaved(const AudioStreamBasicDescription &f) { return !(f.mFormatFlags & kAudioFormatFlagIsNonInterleaved); }
uint32_t bytes_per_sample(const AudioStreamBasicDescription &f) { return f.mBitsPerChannel / 8; }

// The formats AAudio plays: 32-bit float or 16-bit signed integer, packed linear PCM.
bool supported(const AudioStreamBasicDescription &f) {
  if (f.mFormatID != fourcc("lpcm") || f.mChannelsPerFrame < 1 || f.mChannelsPerFrame > 8 || f.mSampleRate <= 0) return false;
  bool is_float = (f.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
  return (is_float && f.mBitsPerChannel == 32) || (!is_float && f.mBitsPerChannel == 16 && (f.mFormatFlags & kAudioFormatFlagIsSignedInteger));
}

AudioBufferList *buffer_list(Unit *u, uint32_t buffers) {
  size_t size = offsetof(AudioBufferList, mBuffers) + buffers * sizeof(AudioBuffer);
  if (u->list_storage.size() < size) u->list_storage.resize(size);
  AudioBufferList *list = reinterpret_cast<AudioBufferList *>(u->list_storage.data());
  list->mNumberBuffers = buffers;
  return list;
}

// Pulls `frames` frames (≤ max_frames) from the render callback into `out` (interleaved).
void pull(Unit *u, uint8_t *out, uint32_t frames) {
  const AudioStreamBasicDescription &f = u->format;
  uint32_t channels = f.mChannelsPerFrame, sample = bytes_per_sample(f);
  AudioTimeStamp ts = {};
  ts.mSampleTime = u->sample_time;
  ts.mFlags = kSampleTimeValid;
  uint32_t flags = 0;
  if (interleaved(f)) {
    AudioBufferList *list = buffer_list(u, 1);
    list->mBuffers[0] = AudioBuffer{channels, frames * channels * sample, out};
    if (u->callback.inputProc(u->callback.inputProcRefCon, &flags, &ts, 0, frames, list) != 0)
      memset(out, 0, frames * channels * sample);
  } else {
    // planes is sized for max_frames when the unit is initialized: no allocation here.
    AudioBufferList *list = buffer_list(u, channels);
    for (uint32_t c = 0; c < channels; c++)
      list->mBuffers[c] = AudioBuffer{1, frames * sample, u->planes.data() + size_t(c) * u->max_frames * sample};
    if (u->callback.inputProc(u->callback.inputProcRefCon, &flags, &ts, 0, frames, list) != 0) {
      memset(out, 0, frames * channels * sample);
    } else {
      for (uint32_t i = 0; i < frames; i++)
        for (uint32_t c = 0; c < channels; c++)
          memcpy(out + (size_t(i) * channels + c) * sample, u->planes.data() + (size_t(c) * u->max_frames + i) * sample, sample);
    }
  }
  u->sample_time += frames;
}

aaudio_data_callback_result_t on_audio(AAudioStream *, void *user, void *data, int32_t frames) {
  Unit *u = static_cast<Unit *>(user);
  uint8_t *out = static_cast<uint8_t *>(data);
  uint32_t frame_bytes = u->format.mChannelsPerFrame * bytes_per_sample(u->format);
  for (int32_t done = 0; done < frames;) {  // in slices of at most max_frames, as an AudioUnit does
    uint32_t n = static_cast<uint32_t>(frames - done);
    if (n > u->max_frames) n = u->max_frames;
    pull(u, out + size_t(done) * frame_bytes, n);
    done += static_cast<int32_t>(n);
  }
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

bool open_stream(Unit *u);

// AAudio stops a stream for good when its device goes away (headphones, route change); it
// must be reopened from another thread (not from the error callback).
void on_error(AAudioStream *stream, void *user, aaudio_result_t error) {
  Unit *u = static_cast<Unit *>(user);
  fprintf(stderr, "mcfm: audio: stream error %d (%s), reopening\n", error, AAudio_convertResultToText(error));
  std::thread([u, stream] {
    std::lock_guard<std::mutex> hold(u->lock);
    if (u->stream != stream) return;  // stopped or reopened meanwhile
    AAudioStream_close(stream);
    u->stream = nullptr;
    if (!open_stream(u)) fprintf(stderr, "mcfm: audio: cannot reopen the output stream\n");
  }).detach();
}

void log_once(const char *message) {
  static std::mutex lock;
  static std::vector<const char *> seen;
  std::lock_guard<std::mutex> hold(lock);
  for (const char *m : seen)
    if (m == message) return;
  seen.push_back(message);
  fprintf(stderr, "mcfm: audio: %s\n", message);
}

int32_t unimplemented() {
  log_once("AudioQueue/AudioFile are not implemented (the game's sound banks do not need them)");
  return kUnimplemented;
}

}  // namespace

void *AudioComponentFindNext(void *after, const AudioComponentDescription *desc) {
  if (after || !desc || desc->componentType != fourcc("auou")) return nullptr;
  if (desc->componentSubType != fourcc("rioc") && desc->componentSubType != fourcc("def ")) return nullptr;
  return &g_output_component;
}

int32_t AudioComponentInstanceNew(void *component, void **unit) {
  if (component != &g_output_component || !unit) return kParamErr;
  *unit = new Unit;
  return kNoErr;
}

int32_t AudioComponentInstanceDispose(void *unit) {
  if (!unit) return kParamErr;
  AudioUnitUninitialize(unit);
  // Not freed: an error-recovery thread (on_error) may still be about to look at it. FMOD
  // creates one output unit per run.
  return kNoErr;
}

int32_t AudioUnitSetProperty(void *unit, uint32_t id, uint32_t scope, uint32_t element, const void *data, uint32_t size) {
  Unit *u = static_cast<Unit *>(unit);
  if (!u || !data) return kParamErr;
  if (id == kAudioUnitProperty_StreamFormat) {
    if (size < sizeof(AudioStreamBasicDescription)) return kParamErr;
    // The render format is the input scope of element 0 (what the app gives the output unit);
    // other scopes and elements (the hardware side, the input bus) are accepted and ignored.
    if (scope != kAudioUnitScope_Input || element != 0) return kNoErr;
    // Buffers are sized at Initialize and in use while running: fixed until Uninitialize.
    if (u->initialized) return kInitialized;
    AudioStreamBasicDescription f;
    memcpy(&f, data, sizeof f);
    if (!supported(f)) {
      log_once("unsupported stream format (only float32 / int16 linear PCM)");
      return kFormatNotSupported;
    }
    u->format = f;
    return kNoErr;
  }
  if (id == kAudioUnitProperty_SetRenderCallback) {
    if (size < sizeof(AURenderCallbackStruct)) return kParamErr;
    if (u->stream) return kInitialized;  // the AAudio thread is calling it
    memcpy(&u->callback, data, sizeof u->callback);
    return kNoErr;
  }
  if (id == kAudioUnitProperty_MaximumFramesPerSlice) {
    if (size < 4) return kParamErr;
    uint32_t n;
    memcpy(&n, data, 4);
    if (n == 0 || n > 16384) return kParamErr;
    if (u->initialized) return kInitialized;
    u->max_frames = n;
    return kNoErr;
  }
  if (id == kAudioOutputUnitProperty_EnableIO) return kNoErr;  // output on, input (recording) never
  return kInvalidProperty;
}

int32_t AudioUnitGetProperty(void *unit, uint32_t id, uint32_t scope, uint32_t element, void *data, uint32_t *size) {
  Unit *u = static_cast<Unit *>(unit);
  if (!u || !data || !size) return kParamErr;
  if (id == kAudioUnitProperty_StreamFormat) {
    if (*size < sizeof(AudioStreamBasicDescription)) return kParamErr;
    // Output scope of element 0 is the hardware side; the input scope is what the app renders.
    AudioStreamBasicDescription f = scope == kAudioUnitScope_Output && element == 0 ? hardware_format() : u->format;
    memcpy(data, &f, sizeof f);
    *size = sizeof f;
    return kNoErr;
  }
  if (id == kAudioUnitProperty_MaximumFramesPerSlice) {
    if (*size < 4) return kParamErr;
    memcpy(data, &u->max_frames, 4);
    *size = 4;
    return kNoErr;
  }
  return kInvalidProperty;
}

int32_t AudioUnitInitialize(void *unit) {
  Unit *u = static_cast<Unit *>(unit);
  if (!u) return kParamErr;
  if (u->initialized) return kNoErr;  // never reallocate buffers a running stream uses
  u->planes.assign(size_t(u->max_frames) * u->format.mChannelsPerFrame * bytes_per_sample(u->format), 0);
  buffer_list(u, u->format.mChannelsPerFrame);  // allocate the list for the largest case now
  u->initialized = true;
  return kNoErr;
}

int32_t AudioUnitUninitialize(void *unit) {
  Unit *u = static_cast<Unit *>(unit);
  if (!u) return kParamErr;
  AudioOutputUnitStop(unit);
  u->initialized = false;
  return kNoErr;
}

namespace {

// Opens and starts the AAudio stream in the unit's format; u->lock held.
bool open_stream(Unit *u) {
  AAudioStreamBuilder *b = nullptr;
  if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return false;
  bool is_float = (u->format.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
  AAudioStreamBuilder_setFormat(b, is_float ? AAUDIO_FORMAT_PCM_FLOAT : AAUDIO_FORMAT_PCM_I16);
  AAudioStreamBuilder_setChannelCount(b, static_cast<int32_t>(u->format.mChannelsPerFrame));
  AAudioStreamBuilder_setSampleRate(b, static_cast<int32_t>(u->format.mSampleRate));
  AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setDataCallback(b, on_audio, u);
  AAudioStreamBuilder_setErrorCallback(b, on_error, u);
  aaudio_result_t r = AAudioStreamBuilder_openStream(b, &u->stream);
  AAudioStreamBuilder_delete(b);
  if (r != AAUDIO_OK) {
    u->stream = nullptr;
    log_once("cannot open an AAudio stream");
    return false;
  }
  if (AAudioStream_requestStart(u->stream) != AAUDIO_OK) {
    AAudioStream_close(u->stream);
    u->stream = nullptr;
    return false;
  }
  fprintf(stderr, "mcfm: audio: output started (%d Hz, %u channels, %s%s)\n", AAudioStream_getSampleRate(u->stream),
          u->format.mChannelsPerFrame, is_float ? "float32" : "int16", interleaved(u->format) ? "" : ", non-interleaved");
  return true;
}

}  // namespace

int32_t AudioOutputUnitStart(void *unit) {
  Unit *u = static_cast<Unit *>(unit);
  if (!u) return kParamErr;
  if (!u->initialized || !u->callback.inputProc) return kUninitialized;
  std::lock_guard<std::mutex> hold(u->lock);
  if (u->stream) return kNoErr;
  u->sample_time = 0;
  return open_stream(u) ? kNoErr : kUnimplemented;
}

int32_t AudioOutputUnitStop(void *unit) {
  Unit *u = static_cast<Unit *>(unit);
  if (!u) return kParamErr;
  std::lock_guard<std::mutex> hold(u->lock);
  if (u->stream) {
    AAudioStream_requestStop(u->stream);
    AAudioStream_close(u->stream);  // waits for the callback to return
    u->stream = nullptr;
  }
  return kNoErr;
}

int32_t AudioSessionGetProperty(uint32_t id, uint32_t *size, void *data) {
  auto put = [&](const void *value, uint32_t n) -> int32_t {
    if (!size || !data || *size < n) return static_cast<int32_t>(fourcc("!siz"));
    memcpy(data, value, n);
    *size = n;
    return kNoErr;
  };
  if (id == fourcc("choc")) { uint32_t v = 2; return put(&v, 4); }               // output channels
  if (id == fourcc("chsr")) { double v = 48000.0; return put(&v, 8); }           // hardware sample rate
  if (id == fourcc("acat")) { uint32_t v = fourcc("ambi"); return put(&v, 4); }  // ambient: no recording
  return static_cast<int32_t>(fourcc("pty?"));
}

void render_for_test(void *unit, void *out, uint32_t frames) { on_audio(nullptr, unit, out, static_cast<int32_t>(frames)); }

void *audio_symbol(const char *name) {
  struct Entry { const char *name; void *address; };
  static const Entry kEntries[] = {
      {"AudioComponentFindNext", reinterpret_cast<void *>(&AudioComponentFindNext)},
      {"AudioComponentInstanceDispose", reinterpret_cast<void *>(&AudioComponentInstanceDispose)},
      {"AudioComponentInstanceNew", reinterpret_cast<void *>(&AudioComponentInstanceNew)},
      {"AudioOutputUnitStart", reinterpret_cast<void *>(&AudioOutputUnitStart)},
      {"AudioOutputUnitStop", reinterpret_cast<void *>(&AudioOutputUnitStop)},
      {"AudioSessionGetProperty", reinterpret_cast<void *>(&AudioSessionGetProperty)},
      {"AudioUnitGetProperty", reinterpret_cast<void *>(&AudioUnitGetProperty)},
      {"AudioUnitInitialize", reinterpret_cast<void *>(&AudioUnitInitialize)},
      {"AudioUnitSetProperty", reinterpret_cast<void *>(&AudioUnitSetProperty)},
      {"AudioUnitUninitialize", reinterpret_cast<void *>(&AudioUnitUninitialize)},
  };
  for (const Entry &e : kEntries)
    if (strcmp(e.name, name) == 0) return e.address;
  // The rest of the AudioToolbox the game imports: AudioQueue*, AudioFile*, AudioUnitRender.
  if (strncmp(name, "AudioQueue", 10) == 0 || strncmp(name, "AudioFile", 9) == 0 || strcmp(name, "AudioUnitRender") == 0)
    return reinterpret_cast<void *>(&unimplemented);
  return nullptr;
}

}  // namespace audio
}  // namespace mcfm

#pragma once
// AudioToolbox for the game image's FMOD on Android (docs/LAUNCHER.md, Stage 3b): FMOD's iOS
// output is a RemoteIO AudioUnit pulling audio through a render callback; here that unit runs on
// an AAudio stream. AudioSession answers what FMOD asks; AudioQueue/AudioFile (a hardware-decoder
// path the game's PCM16 / FMOD-ADPCM banks never use) report kAudio_UnimplementedError. Apple's
// struct layouts (sizes checked against the generated darwin_abi.h).
#include <stddef.h>
#include <stdint.h>

#include "darwin_abi.h"

namespace mcfm {
namespace audio {

struct AudioComponentDescription { uint32_t componentType, componentSubType, componentManufacturer, componentFlags, componentFlagsMask; };
struct AudioStreamBasicDescription {
  double mSampleRate;
  uint32_t mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame, mChannelsPerFrame, mBitsPerChannel,
      mReserved;
};
struct AudioTimeStamp {
  double mSampleTime;
  uint64_t mHostTime;
  double mRateScalar;
  uint64_t mWordClockTime;
  char mSMPTETime[24];
  uint32_t mFlags, mReserved;
};
struct AudioBuffer { uint32_t mNumberChannels, mDataByteSize; void *mData; };
struct AudioBufferList { uint32_t mNumberBuffers; AudioBuffer mBuffers[1]; };
typedef int32_t (*AURenderCallback)(void *ref, uint32_t *flags, const AudioTimeStamp *ts, uint32_t bus, uint32_t frames,
                                    AudioBufferList *data);
struct AURenderCallbackStruct { AURenderCallback inputProc; void *inputProcRefCon; };

static_assert(sizeof(AudioComponentDescription) == darwin::kSizeof_AudioComponentDescription, "AudioComponentDescription");
static_assert(sizeof(AudioStreamBasicDescription) == darwin::kSizeof_AudioStreamBasicDescription, "ASBD");
static_assert(sizeof(AudioTimeStamp) == darwin::kSizeof_AudioTimeStamp &&
                  offsetof(AudioTimeStamp, mFlags) == darwin::kOffsetof_AudioTimeStamp_mFlags,
              "AudioTimeStamp");
static_assert(sizeof(AudioBuffer) == darwin::kSizeof_AudioBuffer && sizeof(AudioBufferList) == darwin::kSizeof_AudioBufferList &&
                  offsetof(AudioBufferList, mBuffers) == darwin::kOffsetof_AudioBufferList_mBuffers,
              "AudioBufferList");

enum : uint32_t {
  kAudioUnitProperty_StreamFormat = 8,
  kAudioUnitProperty_MaximumFramesPerSlice = 14,
  kAudioUnitProperty_SetRenderCallback = 23,
  kAudioOutputUnitProperty_EnableIO = 2003,
  kAudioUnitScope_Global = 0,
  kAudioUnitScope_Input = 1,
  kAudioUnitScope_Output = 2,
  kAudioFormatFlagIsFloat = 1,
  kAudioFormatFlagIsSignedInteger = 4,
  kAudioFormatFlagIsPacked = 8,
  kAudioFormatFlagIsNonInterleaved = 32,
};

// The functions, with the iOS signatures (the game calls them through its AudioToolbox imports).
void *AudioComponentFindNext(void *after, const AudioComponentDescription *desc);
int32_t AudioComponentInstanceNew(void *component, void **unit);
int32_t AudioComponentInstanceDispose(void *unit);
int32_t AudioUnitSetProperty(void *unit, uint32_t id, uint32_t scope, uint32_t element, const void *data, uint32_t size);
int32_t AudioUnitGetProperty(void *unit, uint32_t id, uint32_t scope, uint32_t element, void *data, uint32_t *size);
int32_t AudioUnitInitialize(void *unit);
int32_t AudioUnitUninitialize(void *unit);
int32_t AudioOutputUnitStart(void *unit);
int32_t AudioOutputUnitStop(void *unit);
int32_t AudioSessionGetProperty(uint32_t id, uint32_t *size, void *data);

// The provider for the image's AudioToolbox imports: the address of `name` (no Mach-O '_'), or
// null when we do not provide it.
void *audio_symbol(const char *name);

}  // namespace audio
}  // namespace mcfm

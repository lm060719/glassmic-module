#pragma once
#include "protocol.h"
#include <jni.h>
namespace gm {
extern SharedAudio* mappedAudio;
extern ClientStats* mappedStats;
bool fillAudio(void* out,bool floating,int channels,int rate,int frames,const void* stream);
void installAudioHooks();
void installRecordMetadata(JNIEnv* env,void* api);
void installOpenSl();
}

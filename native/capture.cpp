#include "capture.h"
#include "reader.h"
#include "zygisk.hpp"
#include <aaudio/AAudio.h>
#include <shadowhook.h>
#include <android/dlext.h>
#include <android/log.h>
#include <array>
#include <dlfcn.h>
#include <mutex>
#include <pthread.h>
namespace gm {
static_assert(std::atomic<pthread_t>::is_always_lock_free);
SharedAudio* mappedAudio=nullptr;
ClientStats* mappedStats=nullptr;
static ReaderPool<32> readers;
// Threads inside AAudioStream_read. Its legacy path calls AudioRecord::read on
// the same thread, which must not consume the shared timeline a second time.
static std::array<std::atomic<pthread_t>,32> aaudioThreads{};
static bool insideAAudio() {
    const pthread_t self=pthread_self();
    for(auto& t:aaudioThreads)if(t.load(std::memory_order_acquire)==self)return true;
    return false;
}
bool fillAudio(void* out,bool floating,int channels,int rate,int frames,const void* stream) {
    if(!mappedAudio||!mappedStats)return false;
    return readers.fill(stream,mappedAudio,mappedStats,out,floating,channels,rate,frames);
}
using Read=int32_t(*)(AAudioStream*,void*,int32_t,int64_t);
using SetCallback=void(*)(AAudioStreamBuilder*,AAudioStream_dataCallback,void*);
using Open=aaudio_result_t(*)(AAudioStreamBuilder*,AAudioStream**);
using Close=aaudio_result_t(*)(AAudioStream*);
using Delete=aaudio_result_t(*)(AAudioStreamBuilder*);
static Read originalRead=nullptr;
static SetCallback originalSet=nullptr;
static Open originalOpen=nullptr;static Close originalClose=nullptr;static Delete originalDelete=nullptr;
struct Callback {AAudioStream_dataCallback function=nullptr;void* user=nullptr;AAudioStream* stream=nullptr;};
struct Template {AAudioStreamBuilder* builder=nullptr;AAudioStream_dataCallback function=nullptr;void* user=nullptr;};
static std::array<Callback,128> callbacks;
static std::array<Template,64> templates;
static std::mutex controlMutex; // Never acquired by an audio callback.
static aaudio_data_callback_result_t callback(AAudioStream* stream,void* user,void* data,int32_t frames) {
    auto* cb=static_cast<Callback*>(user);
    const auto format=AAudioStream_getFormat(stream);
    if(AAudioStream_getDirection(stream)==AAUDIO_DIRECTION_INPUT&&(format==AAUDIO_FORMAT_PCM_I16||format==AAUDIO_FORMAT_PCM_FLOAT))
        fillAudio(data,AAudioStream_getFormat(stream)==AAUDIO_FORMAT_PCM_FLOAT,
                  AAudioStream_getChannelCount(stream),AAudioStream_getSampleRate(stream),frames,stream);
    return cb->function(stream,cb->user,data,frames);
}
static void setCallback(AAudioStreamBuilder* builder,AAudioStream_dataCallback cb,void* user) {
    std::lock_guard<std::mutex> lock(controlMutex);
    Template* slot=nullptr;
    for(auto& t:templates)if(t.builder==builder){slot=&t;break;}
    if(!slot)for(auto& t:templates)if(!t.builder){slot=&t;break;}
    if(slot)*slot={cb?builder:nullptr,cb,user};
    originalSet(builder,cb,user);
}
static aaudio_result_t openStream(AAudioStreamBuilder* builder,AAudioStream** stream) {
    std::lock_guard<std::mutex> lock(controlMutex);
    Template* t=nullptr;for(auto& item:templates)if(item.builder==builder){t=&item;break;}
    Callback* wrapper=nullptr;
    if(t&&t->function&&originalSet&&originalClose)for(auto& cb:callbacks)if(!cb.stream){wrapper=&cb;break;}
    if(!wrapper)return originalOpen(builder,stream);
    *wrapper={t->function,t->user,reinterpret_cast<AAudioStream*>(UINTPTR_MAX)};
    originalSet(builder,callback,wrapper);
    auto result=originalOpen(builder,stream);
    originalSet(builder,t->function,t->user);
    if(result==AAUDIO_OK&&stream)wrapper->stream=*stream;else wrapper->stream=nullptr;
    return result;
}
static aaudio_result_t closeStream(AAudioStream* stream) {
    auto result=originalClose(stream);
    if(result==AAUDIO_OK){std::lock_guard<std::mutex> lock(controlMutex);for(auto& cb:callbacks)if(cb.stream==stream)cb.stream=nullptr;}
    return result;
}
static aaudio_result_t deleteBuilder(AAudioStreamBuilder* builder) {
    auto result=originalDelete(builder);
    if(result==AAUDIO_OK){std::lock_guard<std::mutex> lock(controlMutex);for(auto& t:templates)if(t.builder==builder)t.builder=nullptr;}
    return result;
}
static int32_t read(AAudioStream* stream,void* data,int32_t frames,int64_t timeout) {
    std::atomic<pthread_t>* mark=nullptr;
    for(auto& t:aaudioThreads){pthread_t empty=0;if(t.compare_exchange_strong(empty,pthread_self(),std::memory_order_acq_rel)){mark=&t;break;}}
    int32_t n=originalRead(stream,data,frames,timeout);
    if(mark)mark->store(0,std::memory_order_release);
    const auto format=AAudioStream_getFormat(stream);
    if(n>0&&AAudioStream_getDirection(stream)==AAUDIO_DIRECTION_INPUT&&(format==AAUDIO_FORMAT_PCM_I16||format==AAUDIO_FORMAT_PCM_FLOAT))
        fillAudio(data,AAudioStream_getFormat(stream)==AAUDIO_FORMAT_PCM_FLOAT,
                  AAudioStream_getChannelCount(stream),AAudioStream_getSampleRate(stream),n,stream);
    return n;
}
struct Record {
    std::atomic<uintptr_t> object{0};
    std::atomic<uint64_t> format{0};
};
static std::array<Record,64> records;
static jfieldID nativeRecord=nullptr;
static jmethodID sampleRate=nullptr,channelCount=nullptr,audioFormat=nullptr;
static jint (*originalStart)(JNIEnv*,jobject,jint,jint)=nullptr;
static void (*originalRelease)(JNIEnv*,jobject)=nullptr;
static void clearRecord(uintptr_t ptr) {
    for(auto& r:records)if(r.object.load(std::memory_order_acquire)==ptr)r.object.store(0,std::memory_order_release);
}
static jint startRecord(JNIEnv* env,jobject object,jint sync,jint session) {
    jint result=originalStart(env,object,sync,session);
    if(result||!nativeRecord)return result;
    uintptr_t ptr=uintptr_t(env->GetLongField(object,nativeRecord));
    int sr=env->CallIntMethod(object,sampleRate);
    int ch=env->CallIntMethod(object,channelCount);
    int fmt=env->CallIntMethod(object,audioFormat);
    if(env->ExceptionCheck()){env->ExceptionClear();return result;}
    if(!ptr||sr<8000||sr>192000||ch<1||ch>8||(fmt!=2&&fmt!=4))return result;
    clearRecord(ptr);
    for(auto& r:records){
        uintptr_t expected=0;
        if(r.object.compare_exchange_strong(expected,UINTPTR_MAX)){
            r.format.store(uint64_t(uint32_t(sr))|(uint64_t(ch)<<32)|(uint64_t(fmt==4)<<40),std::memory_order_relaxed);
            r.object.store(ptr,std::memory_order_release);break;
        }
    }
    return result;
}
static void releaseRecord(JNIEnv* env,jobject object) {
    if(nativeRecord)clearRecord(uintptr_t(env->GetLongField(object,nativeRecord)));
    originalRelease(env,object);
}
using RecordRead=ssize_t(*)(void*,void*,size_t,bool);
static RecordRead originalRecordRead=nullptr;
static ssize_t recordRead(void* object,void* data,size_t bytes,bool blocking) {
    ssize_t n=originalRecordRead(object,data,bytes,blocking);
    if(n<=0||insideAAudio())return n;
    for(auto& r:records)if(r.object.load(std::memory_order_acquire)==uintptr_t(object)){
        uint64_t meta=r.format.load(std::memory_order_relaxed);
        int sr=int(uint32_t(meta)),ch=int((meta>>32)&255);bool floating=(meta>>40)&1;
        fillAudio(data,floating,ch,sr,int(n)/(ch*(floating?4:2)),object);break;
    }
    return n;
}
void installRecordMetadata(JNIEnv* env,void* apiPtr) {
    auto* api=static_cast<zygisk::Api*>(apiPtr);
    jclass cls=env->FindClass("android/media/AudioRecord");
    if(!cls){env->ExceptionClear();return;}
    // Android 17 renamed the field. Both names store the native AudioRecord
    // pointer (see AOSP getFieldSp), so keep one format/cache path for all APIs.
    nativeRecord=env->GetFieldID(cls,"mNativeAudioRecordHandle","J");
    if(env->ExceptionCheck()){
        env->ExceptionClear();nativeRecord=env->GetFieldID(cls,"mNativeRecorderInJavaObj","J");
    }
    if(env->ExceptionCheck()){env->ExceptionClear();nativeRecord=nullptr;env->DeleteLocalRef(cls);return;}
    sampleRate=env->GetMethodID(cls,"getSampleRate","()I");
    if(env->ExceptionCheck()){env->ExceptionClear();nativeRecord=nullptr;env->DeleteLocalRef(cls);return;}
    channelCount=env->GetMethodID(cls,"getChannelCount","()I");
    if(env->ExceptionCheck()){env->ExceptionClear();nativeRecord=nullptr;env->DeleteLocalRef(cls);return;}
    audioFormat=env->GetMethodID(cls,"getAudioFormat","()I");
    if(env->ExceptionCheck()||!nativeRecord||!sampleRate||!channelCount||!audioFormat){
        env->ExceptionClear();nativeRecord=nullptr;env->DeleteLocalRef(cls);return;
    }
    JNINativeMethod methods[]{
        {const_cast<char*>("native_start"),const_cast<char*>("(II)I"),reinterpret_cast<void*>(startRecord)},
        {const_cast<char*>("native_release"),const_cast<char*>("()V"),reinterpret_cast<void*>(releaseRecord)}
    };
    api->hookJniNativeMethods(env,"android/media/AudioRecord",methods,2);
    originalStart=reinterpret_cast<decltype(originalStart)>(methods[0].fnPtr);
    originalRelease=reinterpret_cast<decltype(originalRelease)>(methods[1].fnPtr);
    if(mappedStats)mappedStats->metadataReady.store(originalStart&&originalRelease?1:0);
    env->DeleteLocalRef(cls);
}
void installAudioHooks() {
    if(mappedStats)mappedStats->installStage.store(2);
    int error=shadowhook_init(SHADOWHOOK_MODE_UNIQUE,false);
    if(error){
        if(mappedStats){mappedStats->hookError.store(uint32_t(error));mappedStats->installStage.store(4);}
        __android_log_print(ANDROID_LOG_ERROR,"GlassMic","ShadowHook initialization failed: %d (%s)",error,shadowhook_to_errmsg(error));
        return;
    }
    uint32_t mask=0;
    if(shadowhook_hook_sym_name("libaaudio.so","AAudioStream_read",reinterpret_cast<void*>(read),reinterpret_cast<void**>(&originalRead)))mask|=1;
    if(shadowhook_hook_sym_name("libaaudio.so","AAudioStreamBuilder_setDataCallback",reinterpret_cast<void*>(setCallback),reinterpret_cast<void**>(&originalSet)))mask|=2;
    shadowhook_hook_sym_name("libaaudio.so","AAudioStreamBuilder_openStream",reinterpret_cast<void*>(openStream),reinterpret_cast<void**>(&originalOpen));
    shadowhook_hook_sym_name("libaaudio.so","AAudioStream_close",reinterpret_cast<void*>(closeStream),reinterpret_cast<void**>(&originalClose));
    shadowhook_hook_sym_name("libaaudio.so","AAudioStreamBuilder_delete",reinterpret_cast<void*>(deleteBuilder),reinterpret_cast<void**>(&originalDelete));
    if(originalStart&&shadowhook_hook_sym_name("libaudioclient.so","_ZN7android11AudioRecord4readEPvmb",reinterpret_cast<void*>(recordRead),reinterpret_cast<void**>(&originalRecordRead)))mask|=4;
    if(mappedStats)mappedStats->hookMask.fetch_or(mask);
    installOpenSl();
    if(mappedStats){
        if(!mappedStats->hookMask.load())mappedStats->hookError.store(uint32_t(shadowhook_get_errno()));
        mappedStats->installStage.store(mappedStats->hookMask.load()?3:4);
    }
    __android_log_print(ANDROID_LOG_INFO,"GlassMic","native backend initialized; mask=%u",mask);
}
}
static int nothingFd=-1;
extern "C" void* __real_dlopen(const char* name,int flags);
// ShadowHook dlopen()s this library once while hooking the linker, to learn the
// soinfo layout from its constructor call. ROMs rarely ship it, so the module
// provides one; without a descriptor the normal search path is still tried.
extern "C" void* __wrap_dlopen(const char* name,int flags) {
    if(nothingFd>=0&&name&&strcmp(name,"libshadowhook_nothing.so")==0) {
        android_dlextinfo ext{};ext.flags=ANDROID_DLEXT_USE_LIBRARY_FD;ext.library_fd=nothingFd;
        return android_dlopen_ext(name,flags,&ext);
    }
    return __real_dlopen(name,flags);
}
// Runs before app specialization, while the descriptor is still mappable.
extern "C" __attribute__((visibility("default"))) int glassmic_prepare(int nothing) {
    nothingFd=nothing;
    int error=shadowhook_init(SHADOWHOOK_MODE_UNIQUE,false);
    nothingFd=-1;return error;
}
extern "C" __attribute__((visibility("default"))) void glassmic_bind(gm::SharedAudio* audio,gm::ClientStats* stats) {
    gm::mappedAudio=audio;gm::mappedStats=stats;
    if(stats)stats->installStage.store(1);
}
extern "C" __attribute__((visibility("default"))) void glassmic_metadata(JNIEnv* env,void* api) {
    gm::installRecordMetadata(env,api);
}
extern "C" __attribute__((visibility("default"))) void glassmic_install() {gm::installAudioHooks();}

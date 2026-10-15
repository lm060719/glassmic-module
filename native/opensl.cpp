#include "capture.h"
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <shadowhook.h>
#include <array>
#include <atomic>
#include <sys/mman.h>
namespace gm {
using Get=SLresult(*)(SLObjectItf,SLInterfaceID,void*);
using Create=SLresult(*)(SLEngineItf,SLObjectItf*,SLDataSource*,SLDataSink*,SLuint32,const SLInterfaceID*,const SLboolean*);
using Enqueue=SLresult(*)(SLAndroidSimpleBufferQueueItf,const void*,SLuint32);
using Register=SLresult(*)(SLAndroidSimpleBufferQueueItf,slAndroidSimpleBufferQueueCallback,void*);
using CreateEngine=SLresult(*)(SLObjectItf*,SLuint32,const SLEngineOption*,SLuint32,const SLInterfaceID*,const SLboolean*);
static Get originalGet=nullptr;
static Create originalCreate=nullptr;
static Enqueue originalEnqueue=nullptr;
static Register originalRegister=nullptr;
static CreateEngine originalEngine=nullptr;
static void (*originalDestroy)(SLObjectItf)=nullptr;
struct Buffer {const void* data=nullptr;uint32_t bytes=0;};
struct Record {
    std::atomic<const void*> object{nullptr},queue{nullptr};
    int rate=48000,channels=1;bool floating=false;
    std::atomic<bool> valid{true};
    slAndroidSimpleBufferQueueCallback cb=nullptr;void* user=nullptr;
    std::array<Buffer,32> buffers{};
    std::atomic<uint32_t> head{0},tail{0};
    std::atomic_flag producer=ATOMIC_FLAG_INIT;
};
static std::array<Record,64> records;
static Record* find(const void* pointer,bool object=false) {
    for(auto& r:records)if((object?r.object:r.queue).load(std::memory_order_acquire)==pointer)return &r;
    return nullptr;
}
template<class T> static bool patch(T* slot,T value,T* original) {
    if(__atomic_load_n(slot,__ATOMIC_ACQUIRE)==value)return true;
    size_t page=size_t(sysconf(_SC_PAGESIZE));uintptr_t begin=uintptr_t(slot)&~(page-1);
    if(mprotect(reinterpret_cast<void*>(begin),page,PROT_READ|PROT_WRITE))return false;
    if(!*original)*original=*slot;
    __atomic_store_n(slot,value,__ATOMIC_RELEASE);return true;
}
template<class VT,class ITF> static VT* table(ITF itf){return const_cast<VT*>(*reinterpret_cast<const VT* const*>(itf));}
static void destroy(SLObjectItf object) {
    Record* r=find(object,true);
    originalDestroy(object);
    if(r){r->valid.store(false);r->queue.store(nullptr,std::memory_order_release);r->object.store(nullptr,std::memory_order_release);}
}
static SLresult getInterface(SLObjectItf self,SLInterfaceID id,void* output);
static void completed(SLAndroidSimpleBufferQueueItf self,void* context) {
    Record* r=find(self);
    if(!r)return;
    uint32_t head=r->head.load(std::memory_order_relaxed),tail=r->tail.load(std::memory_order_acquire);
    if(r->valid.load()&&head!=tail) {
        Buffer b=r->buffers[head%r->buffers.size()];r->head.store(head+1,std::memory_order_release);
        if(b.data&&b.bytes)fillAudio(const_cast<void*>(b.data),r->floating,r->channels,r->rate,
                                 int(b.bytes)/(r->channels*(r->floating?4:2)),self);
    }
    if(r->cb)r->cb(self,context);
}
static SLresult enqueue(SLAndroidSimpleBufferQueueItf self,const void* data,SLuint32 bytes) {
    Record* r=find(self);
    if(r&&r->valid.load()) {
        if(r->producer.test_and_set(std::memory_order_acquire))r->valid.store(false);
        else {
            uint32_t tail=r->tail.load(std::memory_order_relaxed),head=r->head.load(std::memory_order_acquire);
            if(tail-head>=r->buffers.size())r->valid.store(false);
            else {r->buffers[tail%r->buffers.size()]={data,bytes};r->tail.store(tail+1,std::memory_order_release);}
            r->producer.clear(std::memory_order_release);
        }
    }
    SLresult result=originalEnqueue(self,data,bytes);
    if(r&&result!=SL_RESULT_SUCCESS)r->valid.store(false);
    return result;
}
static SLresult registerCallback(SLAndroidSimpleBufferQueueItf self,slAndroidSimpleBufferQueueCallback cb,void* context) {
    Record* r=find(self);
    if(r&&cb){r->cb=cb;r->user=context;return originalRegister(self,completed,context);}
    return originalRegister(self,cb,context);
}
static SLresult createRecorder(SLEngineItf self,SLObjectItf* object,SLDataSource* source,SLDataSink* sink,
                              SLuint32 count,const SLInterfaceID* ids,const SLboolean* required) {
    SLresult result=originalCreate(self,object,source,sink,count,ids,required);
    if(result!=SL_RESULT_SUCCESS||!object||!*object||!sink||!sink->pFormat)return result;
    auto* format=static_cast<SLDataFormat_PCM*>(sink->pFormat);
    bool floating=false;
    if(format->formatType==SL_ANDROID_DATAFORMAT_PCM_EX)
        floating=static_cast<SLAndroidDataFormat_PCM_EX*>(sink->pFormat)->representation==SL_ANDROID_PCM_REPRESENTATION_FLOAT;
    else if(format->formatType!=SL_DATAFORMAT_PCM)return result;
    if((!floating&&format->bitsPerSample!=16)||(floating&&format->bitsPerSample!=32)||
       format->endianness!=SL_BYTEORDER_LITTLEENDIAN||format->numChannels<1||format->numChannels>8)return result;
    Record* slot=find(*object,true);
    if(!slot)for(auto& r:records)if(!r.object.load()){slot=&r;break;}
    if(slot) {
        slot->queue.store(nullptr);slot->rate=int(format->samplesPerSec/1000);slot->channels=int(format->numChannels);
        slot->floating=floating;slot->valid.store(true);slot->head.store(0);slot->tail.store(0);
        slot->cb=nullptr;slot->producer.clear();slot->object.store(*object,std::memory_order_release);
        auto* t=table<SLObjectItf_>(*object);
        patch(&t->GetInterface,getInterface,&originalGet);patch(&t->Destroy,destroy,&originalDestroy);
    }
    return result;
}
static SLresult getInterface(SLObjectItf self,SLInterfaceID id,void* output) {
    SLresult result=originalGet(self,id,output);if(result!=SL_RESULT_SUCCESS||!output)return result;
    void* itf=*static_cast<void**>(output);if(!itf)return result;
    if(id==SL_IID_ENGINE || memcmp(id,SL_IID_ENGINE,sizeof(*id))==0) {
        auto* t=table<SLEngineItf_>(reinterpret_cast<SLEngineItf>(itf));
        patch(&t->CreateAudioRecorder,createRecorder,&originalCreate);
    } else if(id==SL_IID_ANDROIDSIMPLEBUFFERQUEUE || memcmp(id,SL_IID_ANDROIDSIMPLEBUFFERQUEUE,sizeof(*id))==0) {
        auto* r=find(self,true);if(!r)return result;
        auto* t=table<SLAndroidSimpleBufferQueueItf_>(reinterpret_cast<SLAndroidSimpleBufferQueueItf>(itf));
        if(patch(&t->Enqueue,enqueue,&originalEnqueue)&&patch(&t->RegisterCallback,registerCallback,&originalRegister))
            r->queue.store(itf,std::memory_order_release);
    }
    return result;
}
static SLresult createEngine(SLObjectItf* object,SLuint32 n,const SLEngineOption* options,
                            SLuint32 count,const SLInterfaceID* ids,const SLboolean* required) {
    SLresult result=originalEngine(object,n,options,count,ids,required);
    if(result==SL_RESULT_SUCCESS&&object&&*object) {
        auto* t=table<SLObjectItf_>(*object);patch(&t->GetInterface,getInterface,&originalGet);
    }
    return result;
}
void installOpenSl() {
    if(shadowhook_hook_sym_name("libOpenSLES.so","slCreateEngine",reinterpret_cast<void*>(createEngine),reinterpret_cast<void**>(&originalEngine)))
        if(mappedStats)mappedStats->hookMask.fetch_or(8);
}
}

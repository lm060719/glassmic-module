#pragma once
#include "protocol.h"
#include <array>
#include <cmath>
namespace gm {
class Reader {
    uint32_t generation_=0;int rate_=0;
    double cursor_=0;
    uint64_t lastRead_=0;
    std::array<std::array<float,17>,64> filter_{};
    void configure(int rate) {
        rate_=rate;
        if(rate>=int(RATE))return;
        double cutoff=0.45*rate/RATE;
        for(int phase=0;phase<64;++phase) {
            double sum=0;
            for(int t=0;t<17;++t) {
                double x=t-8-double(phase)/64;
                double sinc=fabs(x)<1e-9?2*cutoff:sin(2*M_PI*cutoff*x)/(M_PI*x);
                double window=0.42-0.5*cos(2*M_PI*t/16)+0.08*cos(4*M_PI*t/16);
                filter_[phase][t]=float(sinc*window);sum+=filter_[phase][t];
            }
            for(auto& v:filter_[phase])v=float(v/sum);
        }
    }
public:
    bool fill(const SharedAudio* s,ClientStats* stats,void* out,bool floating,int channels,int rate,int frames) {
        if(!s||!stats||!out||channels<1||channels>8||rate<8000||rate>192000||frames<=0||frames>65536)return false;
        uint64_t now=nowNs();stats->captureNs.store(now,std::memory_order_relaxed);
        uint64_t heartbeat=s->heartbeatNs.load(std::memory_order_acquire);
        uint32_t mode=s->mode.load(std::memory_order_acquire);
        if(!stats->allowed.load()||mode==uint32_t(Mode::Real)||!heartbeat||heartbeat>now||now-heartbeat>500000000)return false;
        uint64_t write=s->writeFrame.load(std::memory_order_acquire),floor=s->floorFrame.load(std::memory_order_acquire);
        uint32_t generation=s->generation.load(std::memory_order_acquire);
        if(generation!=generation_||now-lastRead_>250000000||rate_!=rate) {
            generation_=generation;cursor_=double(std::max(floor,write> CUSHION?write-CUSHION:0));
            if(rate_!=rate)configure(rate);
        }
        lastRead_=now;
        uint64_t oldest=std::max(floor,write>CAPACITY?write-CAPACITY:0);
        if(cursor_<oldest){stats->skipped.fetch_add(oldest-uint64_t(cursor_));cursor_=double(std::max(oldest,write>CUSHION?write-CUSHION:0));}
        uint64_t missing=0;
        double step=double(RATE)/rate;
        for(int f=0;f<frames;++f) {
            float value=0;
            uint64_t index=uint64_t(cursor_);
            if(mode==uint32_t(Mode::File)) {
                int future=rate<int(RATE)?8:1;
                if(index+future<write) {
                    double fraction=cursor_-index;
                    if(rate<int(RATE)) {
                        const auto& filter=filter_[std::min(63,int(fraction*64))];
                        for(int tap=0;tap<17;++tap) {
                            int64_t position=int64_t(index)+tap-8;
                            position=std::max<int64_t>(int64_t(oldest),position);
                            value+=sampleLoad(s,uint64_t(position))*filter[tap];
                        }
                    } else {
                        float a=sampleLoad(s,index),b=sampleLoad(s,index+1);
                        value=a+float(fraction)*(b-a);
                    }
                    cursor_+=step;
                } else ++missing;
            }
            value=std::clamp(value,-1.0f,1.0f);
            for(int c=0;c<channels;++c) {
                if(floating)static_cast<float*>(out)[f*channels+c]=value;
                else static_cast<int16_t*>(out)[f*channels+c]=int16_t(lrintf(value*32767));
            }
        }
        stats->reads.fetch_add(1,std::memory_order_relaxed);
        stats->frames.fetch_add(uint64_t(frames),std::memory_order_relaxed);
        stats->missing.fetch_add(missing,std::memory_order_relaxed);
        return true;
    }
};
// Fixed readers keyed by stream. Audio callbacks must not use thread_local:
// Android's emulated TLS mallocs and locks on a thread's first access.
template<size_t N> class ReaderPool {
    struct Slot {
        std::atomic<const void*> key{nullptr};
        std::atomic<uint64_t> touched{0};
        std::atomic_flag busy=ATOMIC_FLAG_INIT; // try-lock only; never waited on
        Reader reader;
    };
    std::array<Slot,N> slots_{};
    static bool wanted(const Slot& s,int pass,const void* stream,uint64_t now) {
        const void* key=s.key.load(std::memory_order_relaxed);
        if(pass==0)return key==stream;
        if(pass==1)return key==nullptr;
        uint64_t touched=s.touched.load(std::memory_order_relaxed);
        return touched<=now&&now-touched>250000000;
    }
public:
    bool fill(const void* stream,const SharedAudio* s,ClientStats* stats,void* out,bool floating,int channels,int rate,int frames) {
        const uint64_t now=nowNs();
        Slot* found=nullptr;
        // This stream's reader, then a free slot, then one idle for 250 ms.
        for(int pass=0;pass<3&&!found;++pass)
            for(auto& slot:slots_) {
                if(!wanted(slot,pass,stream,now)||slot.busy.test_and_set(std::memory_order_acquire))continue;
                if(wanted(slot,pass,stream,now)){found=&slot;break;}
                slot.busy.clear(std::memory_order_release);
            }
        // A large number of streams falls back to the real microphone.
        if(!found)return false;
        if(found->key.load(std::memory_order_relaxed)!=stream){found->reader=Reader{};found->key.store(stream,std::memory_order_relaxed);}
        found->touched.store(now,std::memory_order_relaxed);
        bool filled=found->reader.fill(s,stats,out,floating,channels,rate,frames);
        found->busy.clear(std::memory_order_release);
        return filled;
    }
};
}

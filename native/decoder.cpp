#define MA_NO_DEVICE_IO
#define MA_NO_ENGINE
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "decoder.h"
namespace gm {
struct Decoder::Impl { ma_decoder decoder{}; bool ready=false; ma_uint64 frames=0; };
Decoder::Decoder():impl_(new Impl) {}
Decoder::~Decoder() { if(impl_->ready) ma_decoder_uninit(&impl_->decoder); }
bool Decoder::open(const std::string& path) {
    if(impl_->ready) ma_decoder_uninit(&impl_->decoder);
    impl_->ready=false; impl_->frames=0;
    auto cfg=ma_decoder_config_init(ma_format_f32,1,48000);
    if(ma_decoder_init_file(path.c_str(),&cfg,&impl_->decoder)!=MA_SUCCESS) return false;
    impl_->ready=true; ma_decoder_get_length_in_pcm_frames(&impl_->decoder,&impl_->frames); return true;
}
uint64_t Decoder::read(float* out,uint64_t frames) {
    ma_uint64 got=0;
    if(impl_->ready) ma_decoder_read_pcm_frames(&impl_->decoder,out,frames,&got);
    return got;
}
bool Decoder::seek(uint64_t frame) {return impl_->ready && ma_decoder_seek_to_pcm_frame(&impl_->decoder,frame)==MA_SUCCESS;}
uint64_t Decoder::length() const {return impl_->frames;}
}

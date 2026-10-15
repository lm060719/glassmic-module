// Portable decoder regression probe; no Android or device access.
#include "decoder.h"
#include <array>
#include <cmath>
#include <cstdio>
int main(int argc,char** argv) {
    if(argc<2)return 2;
    for(int i=1;i<argc;++i) {
        gm::Decoder decoder;
        if(!decoder.open(argv[i])||decoder.length()<48000){fprintf(stderr,"FAIL open/length: %s\n",argv[i]);return 1;}
        std::array<float,480> pcm{};double energy=0;
        if(decoder.read(pcm.data(),pcm.size())!=pcm.size())return 1;
        for(float value:pcm){if(!std::isfinite(value)||fabs(value)>1)return 1;energy+=value*value;}
        if(energy<0.01||!decoder.seek(decoder.length()/2)||decoder.read(pcm.data(),pcm.size())!=pcm.size())return 1;
        printf("PASS decode + seek: %s; %llu frames at 48 kHz\n",argv[i],static_cast<unsigned long long>(decoder.length()));
    }
    return 0;
}

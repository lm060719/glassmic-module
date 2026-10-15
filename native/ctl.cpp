#include "protocol.h"
#include "media_files.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <vector>
using namespace gm;
static int fail(const std::string& e){printf("{\"ok\":false,\"error\":%s}\n",json(e).c_str());return 1;}
static std::vector<uint8_t> base64(const std::string& s) {
    std::vector<uint8_t> out;uint32_t value=0;int bits=-8;
    for(unsigned char c:s){if(c=='=')break;int v=-1;
        if(c>='A'&&c<='Z')v=c-'A';else if(c>='a'&&c<='z')v=c-'a'+26;
        else if(c>='0'&&c<='9')v=c-'0'+52;else if(c=='+')v=62;else if(c=='/')v=63;
        if(v<0)return {};value=(value<<6)|uint32_t(v);bits+=6;
        if(bits>=0){out.push_back(uint8_t(value>>bits));bits-=8;}}
    return out;
}
int main(int argc,char** argv) {
    if(getuid()!=0)return fail("Root required");
    std::string data=DEFAULT_DATA;int i=1;
    if(argc>3&&std::string(argv[i])=="--data"){data=argv[++i];++i;}
    if(i>=argc)return fail("missing command");
    std::string cmd=argv[i++],text;
    if(cmd=="browse"||cmd=="import") {
        if(argc-i!=1)return fail("storage path required");
        std::string result=cmd=="browse"?browseMedia(argv[i]):importMedia(data,argv[i]);
        puts(result.c_str());return result.find("\"ok\":true")!=std::string::npos?0:1;
    }
    if(cmd=="upload"||cmd=="commit") {
        if(i>=argc||!mediaName(argv[i]))return fail("invalid filename");
        std::string name=argv[i++],file=data+"/media/"+name;
        if(cmd=="upload") {
            if(argc-i!=2)return fail("upload requires offset and base64");
            char* end=nullptr;uint64_t offset=strtoull(argv[i++],&end,10);
            if(!end||*end||offset>512ULL*1024*1024)return fail("invalid offset");
            std::string encoded=argv[i];if(encoded.size()>114688)return fail("chunk too large");
            auto bytes=base64(encoded);if(bytes.empty()&&!encoded.empty())return fail("invalid base64");
            int flags=O_WRONLY|O_CLOEXEC|O_NOFOLLOW;
            if(offset==0)flags|=O_CREAT|O_TRUNC;
            int fd=open((file+".part").c_str(),flags,0600);if(fd<0)return fail(strerror(errno));
            struct stat st{};fstat(fd,&st);
            bool ok=uint64_t(st.st_size)==offset&&lseek(fd,off_t(offset),SEEK_SET)>=0&&fullWrite(fd,bytes.data(),bytes.size());
            close(fd);if(!ok)return fail("upload offset mismatch or write failed");
        } else if(cmd=="commit") {
            int fd=open((file+".part").c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return fail("upload missing");
            struct stat st{};fstat(fd,&st);bool ok=S_ISREG(st.st_mode)&&st.st_size>0&&fsync(fd)==0;close(fd);
            if(!ok||rename((file+".part").c_str(),file.c_str()))return fail("commit failed");
        }
        puts("{\"ok\":true}");return 0;
    }
    Request r;
    if(cmd=="status")r.op=Op::Status;
    else if(cmd=="load"||cmd=="delete"){r.op=cmd=="load"?Op::Load:Op::Delete;if(i>=argc)return fail("filename required");text=argv[i];}
    else if(cmd=="play")r.op=Op::Play;else if(cmd=="pause")r.op=Op::Pause;
    else if(cmd=="real")r.op=Op::Real;else if(cmd=="silence")r.op=Op::Silence;
    else if(cmd=="stop")r.op=Op::Stop;
    else if(cmd=="targets"){r.op=Op::Targets;for(;i<argc;++i){if(!text.empty())text+='\n';text+=argv[i];}}
    else if(cmd=="seek"||cmd=="gain"||cmd=="speed"||cmd=="loop") {
        if(i>=argc)return fail("value required");
        char* end=nullptr;double v=strtod(argv[i],&end);
        if(!end||*end||!std::isfinite(v)||fabs(v)>1e10)return fail("invalid number");
        if(cmd=="seek"){r.op=Op::Seek;r.value=int64_t(std::clamp(v,0.0,86400000.0));}
        if(cmd=="gain"){r.op=Op::Gain;r.value=int64_t(v*100);}
        if(cmd=="speed"){r.op=Op::Speed;r.value=int64_t(v*1000);}
        if(cmd=="loop"){r.op=Op::Loop;r.value=v!=0;}
    } else return fail("unknown command");
    if(text.size()>=sizeof(r.text))return fail("argument too long");
    memcpy(r.text,text.data(),text.size());
    int fd=connectDaemon(data);if(fd<0)return fail("audio service unavailable");
    if(!fullWrite(fd,&r,sizeof(r))){close(fd);return fail("request failed");}
    char buffer[4096];ssize_t n;bool received=false;
    while((n=read(fd,buffer,sizeof(buffer)))>0){fwrite(buffer,1,size_t(n),stdout);received=true;}
    close(fd);return received?0:fail("empty response");
}

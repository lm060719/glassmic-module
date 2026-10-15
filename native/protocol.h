#pragma once
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/un.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

namespace gm {
constexpr uint32_t MAGIC = 0x474D3031, VERSION = 1;
constexpr uint32_t RATE = 48000, CAPACITY = 48000, BLOCK = 480, CUSHION = 960;
constexpr const char* DEFAULT_DATA = "/data/adb/glassmic";
inline bool labelSharedFile(int fd) {
    constexpr char context[]="u:object_r:glassmic_shm:s0";
    if(fsetxattr(fd,"security.selinux",context,sizeof(context),0)==0)return true;
    return false;
}
enum class Op : uint32_t { Status, Load, Play, Pause, Real, Silence, Seek, Gain, Speed, Loop, Targets, Attach, Stop, Delete };
enum class Mode : uint32_t { Real, File, Silence };
struct Request {
    uint32_t magic = MAGIC;
    Op op = Op::Status;
    int64_t value = 0;
    uint32_t uid = 0, pid = 0;
    char text[4096]{};
};
static_assert(std::atomic<uint64_t>::is_always_lock_free);
static_assert(std::atomic<uint32_t>::is_always_lock_free);
struct alignas(64) SharedAudio {
    uint32_t magic = MAGIC, version = VERSION;
    std::atomic<uint32_t> mode{0}, generation{1};
    std::atomic<uint64_t> heartbeatNs{0}, writeFrame{0}, floorFrame{0};
    std::atomic<uint32_t> samples[CAPACITY]{};
};
struct alignas(64) ClientStats {
    std::atomic<uint64_t> captureNs{0}, reads{0}, frames{0}, missing{0}, skipped{0};
    std::atomic<uint32_t> hookMask{0}, allowed{1};
    // Use v1's reserved tail padding so existing readers keep their ABI.
    std::atomic<uint32_t> installStage{0}, hookError{0}, metadataReady{0};
};
static_assert(sizeof(ClientStats)==64);
inline uint64_t nowNs() {
    timespec ts{}; clock_gettime(CLOCK_MONOTONIC, &ts);
    return uint64_t(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
}
inline bool fullWrite(int fd, const void* ptr, size_t n) {
    auto p = static_cast<const uint8_t*>(ptr);
    while(n) { ssize_t k = ::write(fd,p,n); if(k<0 && errno==EINTR) continue; if(k<=0) return false; p+=k; n-=size_t(k); }
    return true;
}
inline bool fullRead(int fd, void* ptr, size_t n) {
    auto p = static_cast<uint8_t*>(ptr);
    while(n) { ssize_t k=::read(fd,p,n); if(k<0 && errno==EINTR) continue; if(k<=0) return false; p+=k; n-=size_t(k); }
    return true;
}
inline int connectDaemon(const std::string& data) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(fd < 0) return -1;
    sockaddr_un a{}; a.sun_family=AF_UNIX;
    std::string path=data+"/control.sock";
    if(path.size()>=sizeof(a.sun_path)) { close(fd); return -1; }
    memcpy(a.sun_path,path.c_str(),path.size()+1);
    timeval timeout{3,0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    if(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a))) {close(fd); return -1;}
    return fd;
}
inline int memoryFile(const char* name, size_t size) {
    int fd = int(syscall(__NR_memfd_create,name,1u /*CLOEXEC*/ | 2u /*ALLOW_SEALING*/));
    if(fd>=0 && ftruncate(fd,off_t(size))) {close(fd); return -1;}
    return fd;
}
inline bool sendFds(int sock, int first, int second) {
    char value = 1;
    iovec io{&value,1};
    char control[CMSG_SPACE(sizeof(int)*2)]{};
    msghdr msg{}; msg.msg_iov=&io; msg.msg_iovlen=1;
    msg.msg_control=control; msg.msg_controllen=sizeof(control);
    cmsghdr* c=CMSG_FIRSTHDR(&msg); c->cmsg_level=SOL_SOCKET; c->cmsg_type=SCM_RIGHTS; c->cmsg_len=CMSG_LEN(sizeof(int)*2);
    int fds[]{first,second}; memcpy(CMSG_DATA(c),fds,sizeof(fds));
    return sendmsg(sock,&msg,MSG_NOSIGNAL)==1;
}
inline bool receiveFds(int sock, int (&fds)[2]) {
    fds[0]=fds[1]=-1;
    char value=0, control[CMSG_SPACE(sizeof(int)*2)]{};
    iovec io{&value,1}; msghdr msg{};
    msg.msg_iov=&io; msg.msg_iovlen=1; msg.msg_control=control; msg.msg_controllen=sizeof(control);
    if(recvmsg(sock,&msg,MSG_CMSG_CLOEXEC)!=1 || value!=1 || (msg.msg_flags&MSG_CTRUNC)) return false;
    for(auto* c=CMSG_FIRSTHDR(&msg);c;c=CMSG_NXTHDR(&msg,c)) {
        if(c->cmsg_level==SOL_SOCKET && c->cmsg_type==SCM_RIGHTS && c->cmsg_len==CMSG_LEN(sizeof(int)*2)) {
            memcpy(fds,CMSG_DATA(c),sizeof(fds)); return true;
        }
    }
    return false;
}
inline float sampleLoad(const SharedAudio* s,uint64_t i) {
    uint32_t u=s->samples[i%CAPACITY].load(std::memory_order_relaxed); float f; memcpy(&f,&u,4); return f;
}
inline void sampleStore(SharedAudio* s,uint64_t i,float f) {
    uint32_t u; memcpy(&u,&f,4); s->samples[i%CAPACITY].store(u,std::memory_order_relaxed);
}
inline std::string json(const std::string& s) {
    std::string out="\"";
    const char* hex="0123456789abcdef";
    for(unsigned char ch:s) {
        if(ch=='"' || ch=='\\') {out+='\\';out+=char(ch);}
        else if(ch<32) {out+="\\u00";out+=hex[ch>>4];out+=hex[ch&15];}
        else out+=char(ch);
    }
    return out+'"';
}
inline bool mediaName(const std::string& s) {
    if(s.empty() || s.size()>180 || s=="." || s=="..") return false;
    for(unsigned char ch:s) if(ch=='/' || ch=='\\' || ch<32 || ch==127) return false;
    return true;
}
inline bool packageName(const std::string& s) {
    if(s.size()<3 || s.size()>220 || s.find('.')==std::string::npos) return false;
    for(char c:s) if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='.')) return false;
    return true;
}
}

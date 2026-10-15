#include "reader.h"
#include "decoder.h"
#include "media_files.h"
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <new>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <fcntl.h>
#include <grp.h>
static void check(bool good,const char* name){if(!good){fprintf(stderr,"FAIL: %s\n",name);exit(1);}printf("PASS: %s\n",name);}
// The module ships ShadowHook's helper library next to the backend's directory.
static int openNothing(const char* backend) {
    std::string path=backend;path=path.substr(0,path.rfind('/')+1)+"libshadowhook_nothing.so";
    return open(path.c_str(),O_RDONLY|O_CLOEXEC);
}
static std::string request(const std::string& data,gm::Op op,const char* text="",int64_t value=0) {
    gm::Request r;r.op=op;r.value=value;strncpy(r.text,text,sizeof(r.text)-1);
    int fd=gm::connectDaemon(data);check(fd>=0,"control socket connected");
    check(gm::fullWrite(fd,&r,sizeof(r)),"control request sent");std::string response;char b[4096];ssize_t n;
    while((n=read(fd,b,sizeof(b)))>0)response.append(b,size_t(n));close(fd);
    check(response.find("\"ok\":true")!=std::string::npos,"control request accepted");return response;
}
struct Connection {
    gm::SharedAudio* audio=nullptr;gm::ClientStats* stats=nullptr;
    Connection(const std::string& data) {
        gm::Request r;r.op=gm::Op::Attach;r.uid=10001;r.pid=uint32_t(getpid());strcpy(r.text,"io.mo.glassmic.probe");
        int fd=gm::connectDaemon(data),fds[2];check(fd>=0&&gm::fullWrite(fd,&r,sizeof(r))&&gm::receiveFds(fd,fds),"consumer FD transfer");close(fd);
        void* denied=mmap(nullptr,sizeof(gm::SharedAudio),PROT_READ|PROT_WRITE,MAP_SHARED,fds[0],0);
        check(denied==MAP_FAILED,"consumer cannot write master audio");
        audio=static_cast<gm::SharedAudio*>(mmap(nullptr,sizeof(gm::SharedAudio),PROT_READ,MAP_SHARED,fds[0],0));
        stats=static_cast<gm::ClientStats*>(mmap(nullptr,sizeof(gm::ClientStats),PROT_READ|PROT_WRITE,MAP_SHARED,fds[1],0));
        close(fds[0]);close(fds[1]);check(audio!=MAP_FAILED&&stats!=MAP_FAILED,"consumer shared mappings");
    }
    ~Connection(){munmap(audio,sizeof(gm::SharedAudio));munmap(stats,sizeof(gm::ClientStats));}
};
// Model the actual FD path: KSU -> Zygote -> an ordinary app domain. No APK,
// Android component or real microphone is started by this probe.
static int selinuxProbe(const char* data,const char* backend) {
    using namespace gm;
    void* selinux=dlopen("libselinux.so",RTLD_NOW);
    auto setContext=reinterpret_cast<int(*)(const char*)>(selinux?dlsym(selinux,"setcon"):nullptr);
    check(setContext!=nullptr,"SELinux context API available");
    void* lib=dlopen(backend,RTLD_NOW);check(lib!=nullptr,"sandbox backend preloaded");
    auto bind=reinterpret_cast<void(*)(SharedAudio*,ClientStats*)>(dlsym(lib,"glassmic_bind"));
    auto install=reinterpret_cast<void(*)()>(dlsym(lib,"glassmic_install"));
    auto prepare=reinterpret_cast<int(*)(int)>(dlsym(lib,"glassmic_prepare"));
    check(bind&&install&&prepare,"sandbox backend entry points");
    int nothing=openNothing(backend);check(nothing>=0,"ShadowHook helper library present");
    request(data,Op::Loop,"",1);request(data,Op::Play);
    Request r;r.op=Op::Attach;r.uid=10123;r.pid=uint32_t(getpid());strcpy(r.text,"io.mo.glassmic.probe");
    int control=connectDaemon(data),fds[2];
    check(control>=0&&fullWrite(control,&r,sizeof(r))&&receiveFds(control,fds),"root probe descriptors received");close(control);
    auto* observed=static_cast<ClientStats*>(mmap(nullptr,sizeof(ClientStats),PROT_READ,MAP_SHARED,fds[1],0));
    check(observed!=MAP_FAILED,"root can observe sandbox statistics");
    int sockets[2];check(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sockets)==0,"sandbox private socket pair");
    timeval timeout{2,0};setsockopt(sockets[1],SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    pid_t pid=fork();check(pid>=0,"SELinux probe child fork");
    if(!pid) {
        close(sockets[0]);close(fds[0]);close(fds[1]);munmap(observed,sizeof(ClientStats));
        if(setContext("u:r:zygote:s0"))_exit(10);
        int received[2];if(!receiveFds(sockets[1],received))_exit(11);close(sockets[1]);
        auto* audio=static_cast<SharedAudio*>(mmap(nullptr,sizeof(SharedAudio),PROT_READ,MAP_SHARED,received[0],0));
        auto* stats=static_cast<ClientStats*>(mmap(nullptr,sizeof(ClientStats),PROT_READ|PROT_WRITE,MAP_SHARED,received[1],0));
        if(audio==MAP_FAILED||stats==MAP_FAILED)_exit(12);
        void* denied=mmap(nullptr,sizeof(SharedAudio),PROT_READ|PROT_WRITE,MAP_SHARED,received[0],0);
        if(denied!=MAP_FAILED)_exit(13);
        close(received[0]);close(received[1]);
        // As in Zygisk preAppSpecialize: ShadowHook initializes in the Zygote domain.
        if(prepare(nothing))_exit(19);
        close(nothing);
        if(setgroups(0,nullptr)||setgid(10123)||setuid(10123))_exit(14);
        // Android 15 and earlier have no untrusted_app_34; targetSdk 34+ apps use untrusted_app.
        if(setContext("u:r:untrusted_app_34:s0:c123,c256,c512,c768")&&setContext("u:r:untrusted_app:s0:c123,c256,c512,c768"))_exit(15);
        bind(audio,stats);install();
        if((stats->hookMask.load()&11)!=11)_exit(16);
        usleep(50000);Reader reader;std::array<float,480> pcm{};
        if(!reader.fill(audio,stats,pcm.data(),true,1,48000,480))_exit(17);
        double energy=0;for(float value:pcm)energy+=value*value;
        _exit(energy>0.01?0:18);
    }
    close(nothing);close(sockets[1]);bool sent=sendFds(sockets[0],fds[0],fds[1]);close(sockets[0]);close(fds[0]);close(fds[1]);
    int code=0;waitpid(pid,&code,0);
    printf("SELinux child exit=%d, hooks=%u, stage=%u, error=%u\n",WIFEXITED(code)?WEXITSTATUS(code):-1,observed->hookMask.load(),observed->installStage.load(),observed->hookError.load());
    check(sent&&WIFEXITED(code)&&WEXITSTATUS(code)==0,"KSU FD receipt in Zygote and capture under untrusted_app_34");
    check(observed->reads.load()>0,"sandbox consumer statistics reached root daemon");
    munmap(observed,sizeof(ClientStats));puts("SELINUX PROBE PASSED");return 0;
}
static int integration(const char* data,const char* daemon) {
    using namespace gm;
    request(data,Op::Targets,"io.mo.glassmic.probe");request(data,Op::Load,"test.wav");
    request(data,Op::Gain,"",0);request(data,Op::Speed,"",1000);request(data,Op::Loop,"",1);
    // A press on Play must advance even before any consumer attaches.
    request(data,Op::Play);usleep(180000);
    auto position=[](const std::string& status){auto start=status.find("\"positionMs\":");check(start!=std::string::npos,"position reported");return strtoull(status.c_str()+start+13,nullptr,10);};
    check(position(request(data,Op::Status))>=100,"play advances without any recorder");
    auto pausedStatus=request(data,Op::Pause);auto pausedPosition=position(pausedStatus);usleep(100000);
    check(position(request(data,Op::Status))==pausedPosition,"pause freezes the source position");
    request(data,Op::Seek,"",0);
    Connection first(data),second(data);Reader a,b;
    request(data,Op::Play);
    std::array<float,960> mono{};std::array<float,640> stereo{};double energyA=0,energyB=0;
    for(int i=0;i<100;++i) {
        a.fill(first.audio,first.stats,mono.data(),true,1,48000,960);
        b.fill(second.audio,second.stats,stereo.data(),true,2,16000,320);
        if(i>5){for(float v:mono)energyA+=v*v;for(float v:stereo)energyB+=v*v;}
        usleep(20000);
    }
    double rmsA=sqrt(energyA/(94*960)),rmsB=sqrt(energyB/(94*640));
    printf("RMS: master=%f resampled=%f; missing=%llu/%llu\n",rmsA,rmsB,(unsigned long long)first.stats->missing.load(),(unsigned long long)second.stats->missing.load());
    check(rmsA>0.1&&rmsA<0.3&&fabs(rmsA-rmsB)<0.03,"two independent consumers receive decoded audio");
    check(first.stats->missing.load()<4000&&second.stats->missing.load()<1400,"bounded startup underrun");
    request(data,Op::Pause);a.fill(first.audio,first.stats,mono.data(),true,1,48000,960);
    check(mono[0]==0,"pause discards old buffered audio");
    request(data,Op::Seek,"",1000);request(data,Op::Play);usleep(30000);
    check(a.fill(first.audio,first.stats,mono.data(),true,1,48000,960),"seek resumes existing consumer");
    request(data,Op::Targets,"");
    check(!a.fill(first.audio,first.stats,mono.data(),true,1,48000,960),"target removal is immediate");
    request(data,Op::Targets,"io.mo.glassmic.probe");
    // Restart only this isolated development daemon, never Android services/apps.
    request(data,Op::Stop);usleep(150000);
    pid_t pid=fork();check(pid>=0,"isolated daemon restart fork");
    if(pid==0){execl(daemon,daemon,"--data",data,nullptr);_exit(127);}
    usleep(250000);
    std::string restored=request(data,Op::Status);
    check(restored.find("test.wav")!=std::string::npos,"selection survives daemon restart");
    check(!a.fill(first.audio,first.stats,mono.data(),true,1,48000,960),"restart defaults to real microphone");
    request(data,Op::Play);
    double after=0;
    for(int i=0;i<40;++i){a.fill(first.audio,first.stats,mono.data(),true,1,48000,960);if(i>5)for(float v:mono)after+=v*v;usleep(20000);}
    check(after>10,"existing mapping recovers after daemon restart");
    // The consumer retains only mappings, not privileged descriptors.
    pid_t sandbox=fork();check(sandbox>=0,"sandbox fork");
    if(!sandbox){if(setgid(10123)||setuid(10123))_exit(2);Reader reader;bool ok=reader.fill(first.audio,first.stats,mono.data(),true,1,48000,960);_exit(ok?0:3);}
    int code=0;waitpid(sandbox,&code,0);check(WIFEXITED(code)&&WEXITSTATUS(code)==0,"consumer works after UID drop without root descriptors");
    request(data,Op::Stop);waitpid(pid,&code,0);puts("INTEGRATION TESTS PASSED");return 0;
}
int main(int argc,char** argv) {
    using namespace gm;
    if(argc==4&&std::string(argv[1])=="--integration")return integration(argv[2],argv[3]);
    if(argc==4&&std::string(argv[1])=="--selinux")return selinuxProbe(argv[2],argv[3]);
    SharedAudio* audio=new SharedAudio;ClientStats stats;
    audio->heartbeatNs.store(nowNs());audio->mode.store(uint32_t(Mode::File));
    for(uint32_t i=0;i<4000;++i)sampleStore(audio,i,0.5f);
    audio->writeFrame.store(4000);
    Reader a,b;std::array<int16_t,480> out{};std::array<float,960> stereo{};
    check(a.fill(audio,&stats,out.data(),false,1,48000,480),"PCM16 capture");
    check(out[0]>=16380&&out[479]>=16380,"PCM16 amplitude");
    check(b.fill(audio,&stats,stereo.data(),true,2,16000,100),"independent resampled consumer");
    check(fabsf(stereo[0]-0.5f)<0.001f&&stereo[0]==stereo[1],"FIR resampling and channel conversion");
    check(stats.missing.load()==0,"no underrun with sufficient prebuffer");
    {
        ClientStats poolStats;ReaderPool<2> pool;int keyA=0,keyB=0,keyC=0;
        for(int i=0;i<3;++i)pool.fill(&keyA,audio,&poolStats,out.data(),false,1,48000,480);
        check(poolStats.missing.load()>=480,"pooled reader keeps its stream position");
        uint64_t before=poolStats.missing.load();
        check(pool.fill(&keyB,audio,&poolStats,out.data(),false,1,48000,480)&&poolStats.missing.load()==before,"second stream gets an independent reader");
        check(!pool.fill(&keyC,audio,&poolStats,out.data(),false,1,48000,480),"full reader pool falls back to real microphone");
        usleep(260000);audio->heartbeatNs.store(nowNs());
        check(pool.fill(&keyC,audio,&poolStats,out.data(),false,1,48000,480),"idle reader slot is reclaimed");
        ReaderPool<8> shared;std::atomic<int> failures{0};std::vector<std::thread> threads;int keys[4]{};
        for(auto& key:keys)threads.emplace_back([&,k=&key]{std::array<float,96> pcm{};
            for(int i=0;i<2000;++i)if(!shared.fill(k,audio,&poolStats,pcm.data(),true,2,48000,48))failures.fetch_add(1);});
        for(auto& t:threads)t.join();
        check(failures.load()==0,"concurrent callbacks share the reader pool");
        audio->heartbeatNs.store(nowNs());
    }
    audio->mode.store(uint32_t(Mode::Silence));
    check(a.fill(audio,&stats,out.data(),false,1,48000,480)&&out[0]==0,"explicit silence");
    audio->mode.store(uint32_t(Mode::Real));out.fill(123);
    check(!a.fill(audio,&stats,out.data(),false,1,48000,480)&&out[0]==123,"real mic passes through");
    audio->mode.store(uint32_t(Mode::File));audio->heartbeatNs.store(nowNs()-600000000);
    check(!a.fill(audio,&stats,out.data(),false,1,48000,480),"daemon failure passes through");
    audio->heartbeatNs.store(nowNs());stats.allowed.store(0);
    check(!a.fill(audio,&stats,out.data(),false,1,48000,480),"removed target passes through");
    stats.allowed.store(1);audio->floorFrame.store(4000);audio->generation.fetch_add(1);
    check(a.fill(audio,&stats,out.data(),false,1,48000,480)&&out[0]==0,"generation prevents stale audio");
    check(stats.missing.load()>=480,"underrun accounted");
    check(!mediaName("../evil")&&!mediaName("a/b")&&mediaName("语音.wav"),"media path validation");
    check(mediaExtension("无损.FLAC")==".flac"&&mediaExtension("x.MP3")==".mp3"&&mediaExtension("x.flac.exe").empty(),"case-insensitive audio file filtering");
    std::string storageRoot;check(storagePath("/",storageRoot).empty(),"file browser excludes root filesystem");
    check(packageName("io.mo.glassmic.probe")&&!packageName("a;reboot"),"package validation");
    if(argc>1) {
        Decoder decoder;check(decoder.open(argv[1]),"native file decoder");
        std::array<float,480> decoded{};check(decoder.read(decoded.data(),480)==480,"decoded audio frames");
        check(decoder.seek(0),"decoder seek");
    }
    if(argc>2) {
        void* lib=dlopen(argv[2],RTLD_NOW);if(!lib)fprintf(stderr,"%s\n",dlerror());
        check(lib!=nullptr,"self-contained Zygisk library loads");
        auto prepare=reinterpret_cast<int(*)(int)>(dlsym(lib,"glassmic_prepare"));
        int nothing=openNothing(argv[2]);check(nothing>=0,"ShadowHook helper library present");
        check(prepare&&prepare(nothing)==0,"embedded ShadowHook initializes");close(nothing);
        auto bind=reinterpret_cast<void(*)(SharedAudio*,ClientStats*)>(dlsym(lib,"glassmic_bind"));
        auto install=reinterpret_cast<void(*)()>(dlsym(lib,"glassmic_install"));
        check(bind&&install,"native backend exports");bind(audio,&stats);install();
        check((stats.hookMask.load()&11)==11,"AAudio and OpenSL hooks install in test process");
        // Do not dlclose after initializing ShadowHook's linker callbacks.
    }
    delete audio;puts("ALL TESTS PASSED");return 0;
}

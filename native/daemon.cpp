#include "protocol.h"
#include "decoder.h"
#include <array>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <sstream>
#include <thread>
#include <vector>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/xattr.h>

using namespace gm;
static std::atomic<bool> running{true};
static void signalHandler(int) {running.store(false);}
struct Client {
    uint32_t pid, uid; std::string package;
    int fd; ClientStats* stats; std::string backing;
    ~Client(){munmap(stats,sizeof(ClientStats));close(fd);}
};
class Engine {
public:
    std::mutex mutex;
    SharedAudio* audio=nullptr;
    int masterFd=-1, readFd=-1;
    std::string data, runtime, selected;
    std::set<std::string> targets;
    std::vector<std::unique_ptr<Client>> clients;
    std::unique_ptr<Decoder> decoder;
    bool paused=true, loop=false, eof=false;
    float gain=1, speed=1, peak=0;
    uint64_t sourcePosition=0, duration=0;
    std::array<float,BLOCK*4+64> cache{};
    size_t cached=0;
    double phase=0;
    explicit Engine(std::string path):data(std::move(path)),runtime(data==DEFAULT_DATA?"/dev/glassmic":data+"/runtime"){}
    ~Engine(){if(audio)munmap(audio,sizeof(SharedAudio));if(masterFd>=0)close(masterFd);if(readFd>=0)close(readFd);}
    bool init() {
        mkdir(runtime.c_str(),0700);mkdir((runtime+"/clients").c_str(),0700);
        masterFd=open((runtime+"/audio.shm").c_str(),O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
        if(masterFd<0||ftruncate(masterFd,sizeof(SharedAudio)))return false;
        if(!labelSharedFile(masterFd)){perror("GlassMic shared-file label (check sepolicy.rule)");return false;}
        void* p=mmap(nullptr,sizeof(SharedAudio),PROT_READ|PROT_WRITE,MAP_SHARED,masterFd,0);
        if(p==MAP_FAILED)return false;
        audio=static_cast<SharedAudio*>(p);
        if(audio->magic==MAGIC&&audio->version==VERSION) {
            audio->mode.store(uint32_t(Mode::Real),std::memory_order_release);
            audio->heartbeatNs.store(0,std::memory_order_release);
            audio->writeFrame.store(0);audio->floorFrame.store(0);audio->generation.fetch_add(1);
        } else audio=new(p) SharedAudio();
        readFd=open((runtime+"/audio.shm").c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
        if(readFd<0)return false;
        std::ifstream in(data+"/targets.txt");std::string s;
        while(std::getline(in,s))if(packageName(s)&&!protectedPackage(s))targets.insert(s);
        std::ifstream settings(data+"/settings.txt");
        float db=0; settings>>db>>speed>>loop;
        if(!std::isfinite(db))db=0;
        if(!std::isfinite(speed))speed=1;
        gain=powf(10,std::clamp(db,-24.0f,12.0f)/20);
        speed=std::clamp(speed,0.5f,2.0f);
        std::ifstream selection(data+"/selection.txt");std::string previous;uint64_t previousMs=0;
        if(std::getline(selection,previous)&&mediaName(previous)) {
            selection>>previousMs;auto restored=std::make_unique<Decoder>();
            if(restored->open(data+"/media/"+previous)) {
                selected=previous;duration=restored->length();sourcePosition=std::min(duration,previousMs*48);
                restored->seek(sourcePosition);decoder=std::move(restored);
            }
        }
        restoreClients();mirrorTargets();
        audio->heartbeatNs.store(nowNs(),std::memory_order_release);
        return true;
    }
    void mirrorTargets() {
        if(data!=DEFAULT_DATA)return;
        const std::string dest="/data/adb/modules/glassmic/targets.txt";
        std::ofstream f(dest+".tmp");if(!f)return;
        for(auto& p:targets)f<<p<<'\n';f.close();chmod((dest+".tmp").c_str(),0644);
        const char context[]="u:object_r:system_file:s0";
        setxattr((dest+".tmp").c_str(),"security.selinux",context,sizeof(context),0);
        rename((dest+".tmp").c_str(),dest.c_str());
    }
    void saveSelection() {
        std::ofstream f(data+"/selection.txt.tmp");f<<selected<<'\n'<<sourcePosition/48<<'\n';f.close();
        rename((data+"/selection.txt.tmp").c_str(),(data+"/selection.txt").c_str());
    }
    void restoreClients() {
        DIR* dir=opendir((runtime+"/clients").c_str());if(!dir)return;
        while(auto* e=readdir(dir)) {
            std::string file=e->d_name;
            if(file.size()<7||file.compare(file.size()-6,6,".stats")!=0||clients.size()>=128)continue;
            std::string path=runtime+"/clients/"+file;
            uint32_t pid=0,uid=0;std::string package;
            std::ifstream meta(path+".meta");meta>>pid>>uid>>package;
            if(!pid||uid<10000||(kill(pid_t(pid),0)<0&&errno==ESRCH)){unlink(path.c_str());unlink((path+".meta").c_str());continue;}
            int fd=open(path.c_str(),O_RDWR|O_NOFOLLOW|O_CLOEXEC);if(fd<0)continue;
            struct stat st{};if(fstat(fd,&st)||st.st_size!=sizeof(ClientStats)){close(fd);continue;}
            if(!labelSharedFile(fd)){close(fd);continue;}
            void* p=mmap(nullptr,sizeof(ClientStats),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
            if(p==MAP_FAILED){close(fd);continue;}
            auto c=std::make_unique<Client>();c->fd=fd;c->stats=static_cast<ClientStats*>(p);c->pid=pid;c->uid=uid;c->package=package;c->backing=path;
            c->stats->allowed.store(allowed(package));clients.push_back(std::move(c));
        }
        closedir(dir);
    }
    static bool protectedPackage(const std::string& p) {
        static const std::set<std::string> critical={
            "android","system","io.mo.glassmic","com.android.server.telecom","com.android.systemui",
            "com.android.settings","com.android.phone","com.android.incallui","com.android.dialer",
            "com.google.android.dialer","com.android.bluetooth","com.android.location.fused","com.android.nfc",
            "com.android.providers.settings","com.android.providers.media.module","com.android.providers.downloads",
            "com.android.providers.contacts","com.android.providers.telephony","com.android.providers.calendar",
            "com.android.shell","com.android.externalstorage","com.android.documentsui","com.android.printspooler",
            "com.android.launcher3","com.miui.home","com.miui.securitycenter","com.huawei.android.launcher",
            "com.oppo.launcher","com.coloros.launcher","com.vivo.launcher","com.sec.android.app.launcher",
            "com.realme.launcher","com.google.android.apps.nexuslauncher","com.google.android.gsf",
            "com.android.vending","com.sohu.inputmethod.sogou","com.baidu.input","com.iflytek.inputmethod",
            "com.google.android.inputmethod.latin","com.samsung.android.honeyboard","me.weishu.kernelsu","com.rifsxd.ksunext"
        };
        return critical.count(p)||p.rfind("com.google.android.gms",0)==0;
    }
    bool allowed(std::string p) const {
        p=p.substr(0,p.find(':'));
        return !protectedPackage(p)&&targets.count(p);
    }
    void resetGeneration() {
        cached=0;phase=0;eof=false;
        audio->floorFrame.store(audio->writeFrame.load(std::memory_order_acquire),std::memory_order_release);
        audio->generation.fetch_add(1,std::memory_order_release);
    }
    void saveSettings() {
        std::ofstream f(data+"/settings.txt.tmp");
        f<<20*log10f(gain)<<' '<<speed<<' '<<loop<<'\n';f.close();
        rename((data+"/settings.txt.tmp").c_str(),(data+"/settings.txt").c_str());
    }
    void reapClients() {
        for(auto it=clients.begin();it!=clients.end();) {
            auto& c=**it;
            if(kill(pid_t(c.pid),0)<0&&errno==ESRCH){unlink(c.backing.c_str());unlink((c.backing+".meta").c_str());it=clients.erase(it);continue;}
            ++it;
        }
    }
    void fill(std::array<float,BLOCK>& out) {
        out.fill(0);peak=0;
        if(paused||!decoder||audio->mode.load()!=uint32_t(Mode::File))return;
        const size_t need=size_t(std::ceil(BLOCK*speed+phase))+2;
        while(cached<need && !eof) {
            uint64_t got=decoder->read(cache.data()+cached,need-cached);
            cached+=size_t(got);
            if(!got) {
                if(loop&&duration>1&&decoder->seek(0)) {
                    uint64_t restarted=decoder->read(cache.data()+cached,need-cached);cached+=size_t(restarted);
                    if(!restarted)eof=true;
                } else eof=true;
            }
        }
        size_t made=0;
        for(;made<BLOCK;++made) {
            size_t index=size_t(phase);
            if(index>=cached)break;
            float next=(index+1<cached)?cache[index+1]:cache[index];
            float value=(cache[index]+float(phase-index)*(next-cache[index]))*gain;
            float a=fabsf(value);
            // Soft knee above -1 dBFS. Never wrap integer PCM.
            if(a>0.89f)value=copysignf(0.89f+0.11f*tanhf((a-0.89f)/0.11f),value);
            out[made]=value;peak=std::max(peak,fabsf(value));phase+=speed;
        }
        size_t used=std::min(size_t(phase),cached);
        cached-=used;memmove(cache.data(),cache.data()+used,cached*sizeof(float));phase-=used;
        sourcePosition+=used;
        if(loop&&duration)sourcePosition%=duration;
        if(eof&&!cached){paused=true;sourcePosition=duration;}
    }
    void pump() {
        pthread_setname_np(pthread_self(),"GlassMicEngine");
        // Best effort; correctness never depends on an RT scheduling grant.
        setpriority(PRIO_PROCESS,0,-10);
        timespec next{};clock_gettime(CLOCK_MONOTONIC,&next);
        while(running.load()) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                uint64_t now=nowNs();std::array<float,BLOCK> out{};
                // Play owns the timeline. A recorder joining later consumes the
                // current position; opening/closing a recorder never stalls it.
                fill(out);
                uint64_t write=audio->writeFrame.load(std::memory_order_relaxed);
                for(size_t i=0;i<BLOCK;++i)sampleStore(audio,write+i,out[i]);
                audio->writeFrame.store(write+BLOCK,std::memory_order_release);
                audio->heartbeatNs.store(now,std::memory_order_release);
            }
            next.tv_nsec+=10000000;
            if(next.tv_nsec>=1000000000){++next.tv_sec;next.tv_nsec-=1000000000;}
            if(uint64_t(next.tv_sec)*1000000000ULL+next.tv_nsec+100000000<nowNs())
                clock_gettime(CLOCK_MONOTONIC,&next);
            while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&next,nullptr)==EINTR&&running.load()){}
        }
        audio->mode.store(uint32_t(Mode::Real),std::memory_order_release);
        audio->heartbeatNs.store(0,std::memory_order_release);
    }
    std::string status() {
        reapClients();
        std::ostringstream o;
        o<<"{\"ok\":true,\"version\":\"0.1.0.9\",\"mode\":"<<audio->mode.load()
         <<",\"paused\":"<<(paused?"true":"false")<<",\"loop\":"<<(loop?"true":"false")
         <<",\"gainDb\":"<<20*log10f(gain)<<",\"speed\":"<<speed
         <<",\"positionMs\":"<<sourcePosition/48<<",\"durationMs\":"<<duration/48
         <<",\"peak\":"<<peak<<",\"selected\":"<<json(selected)<<",\"targets\":[";
        bool comma=false;for(auto& t:targets){if(comma)o<<',';comma=true;o<<json(t);}o<<"],\"clients\":[";
        comma=false;for(auto& c:clients){if(comma)o<<',';comma=true;
            o<<"{\"package\":"<<json(c->package)<<",\"pid\":"<<c->pid<<",\"hooks\":"<<c->stats->hookMask.load()
             <<",\"reads\":"<<c->stats->reads.load()<<",\"frames\":"<<c->stats->frames.load()
             <<",\"missing\":"<<c->stats->missing.load()<<",\"skipped\":"<<c->stats->skipped.load()
             <<",\"stage\":"<<c->stats->installStage.load()<<",\"hookError\":"<<c->stats->hookError.load()
             <<",\"metadataReady\":"<<c->stats->metadataReady.load()<<'}';}
        o<<"],\"files\":[";
        DIR* d=opendir((data+"/media").c_str());comma=false;
        if(d){while(auto* e=readdir(d)){std::string name=e->d_name;
            if(!mediaName(name)||name.front()=='.'||name.size()<5||name.compare(name.size()-5,5,".part")==0)continue;
            struct stat st{}; if(lstat((data+"/media/"+name).c_str(),&st)||!S_ISREG(st.st_mode))continue;
            if(comma)o<<',';comma=true;o<<"{\"name\":"<<json(name)<<",\"bytes\":"<<st.st_size<<'}';}
            closedir(d);}
        o<<"]}";return o.str();
    }
    std::string request(const Request& r,int socket) {
        if(r.magic!=MAGIC)return "{\"ok\":false,\"error\":\"protocol mismatch\"}";
        std::unique_ptr<Decoder> prepared;
        std::string text(r.text,strnlen(r.text,sizeof(r.text)));
        if(r.op==Op::Load) {
            if(!mediaName(text))return "{\"ok\":false,\"error\":\"invalid filename\"}";
            std::string file=data+"/media/"+text;
            struct stat st{};if(lstat(file.c_str(),&st)||!S_ISREG(st.st_mode))return "{\"ok\":false,\"error\":\"file missing\"}";
            prepared=std::make_unique<Decoder>();
            if(!prepared->open(file))return "{\"ok\":false,\"error\":\"unsupported audio; use WAV, MP3 or FLAC\"}";
        }
        std::lock_guard<std::mutex> lock(mutex);
        switch(r.op) {
        case Op::Status:break;
        case Op::Load:
            decoder=std::move(prepared);duration=decoder->length();sourcePosition=0;selected=text;
            paused=true;resetGeneration();audio->mode.store(uint32_t(Mode::File),std::memory_order_release);break;
        case Op::Play:
            if(!decoder)return "{\"ok\":false,\"error\":\"select an audio file first\"}";
            if(sourcePosition>=duration&&duration)sourcePosition=0;
            if(!decoder->seek(sourcePosition))return "{\"ok\":false,\"error\":\"resume seek failed\"}";
            paused=false;resetGeneration();audio->mode.store(uint32_t(Mode::File),std::memory_order_release);break;
        case Op::Pause:
            paused=true;resetGeneration();break;
        case Op::Real:
            audio->mode.store(uint32_t(Mode::Real),std::memory_order_release);paused=true;resetGeneration();break;
        case Op::Silence:
            audio->mode.store(uint32_t(Mode::Silence),std::memory_order_release);paused=true;resetGeneration();break;
        case Op::Seek:
            if(!decoder||!decoder->seek(std::min<uint64_t>(duration,uint64_t(std::max<int64_t>(0,r.value))*48)))
                return "{\"ok\":false,\"error\":\"seek failed\"}";
            sourcePosition=std::min<uint64_t>(duration,uint64_t(std::max<int64_t>(0,r.value))*48);resetGeneration();break;
        case Op::Gain:
            gain=powf(10,std::clamp(float(r.value)/100,-24.0f,12.0f)/20);saveSettings();break;
        case Op::Speed:
            speed=std::clamp(float(r.value)/1000,0.5f,2.0f);
            if(decoder)decoder->seek(sourcePosition);resetGeneration();saveSettings();break;
        case Op::Loop:loop=r.value!=0;saveSettings();break;
        case Op::Targets:{
            std::set<std::string> nextTargets;std::istringstream input(text);std::string p;
            while(input>>p){if(!packageName(p)||protectedPackage(p))return "{\"ok\":false,\"error\":\"invalid or protected package\"}";nextTargets.insert(p);}
            std::ofstream f(data+"/targets.txt.tmp");
            if(!f)return "{\"ok\":false,\"error\":\"cannot save targets\"}";
            for(auto& p:nextTargets)f<<p<<'\n';f.close();
            if(rename((data+"/targets.txt.tmp").c_str(),(data+"/targets.txt").c_str()))
                return "{\"ok\":false,\"error\":\"cannot save targets\"}";
            targets=std::move(nextTargets);mirrorTargets();for(auto& c:clients)c->stats->allowed.store(allowed(c->package));break;}
        case Op::Attach:{
            reapClients();
            if(r.uid<10000||!r.pid||!allowed(text)||clients.size()>=128)return "";
            std::string backing=runtime+"/clients/"+std::to_string(r.pid)+"-"+std::to_string(nowNs())+".stats";
            int fd=open(backing.c_str(),O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
            if(fd<0)return "";
            if(!labelSharedFile(fd)){close(fd);unlink(backing.c_str());return "";}
            if(ftruncate(fd,sizeof(ClientStats))){close(fd);unlink(backing.c_str());return "";}
            void* p=mmap(nullptr,sizeof(ClientStats),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
            if(p==MAP_FAILED){close(fd);return "";}
            auto c=std::make_unique<Client>();c->pid=r.pid;c->uid=r.uid;c->package=text;c->fd=fd;c->stats=new(p)ClientStats();
            c->backing=backing;
            if(!sendFds(socket,readFd,fd)){unlink(backing.c_str());return "";}
            std::ofstream meta(backing+".meta");meta<<c->pid<<' '<<c->uid<<' '<<c->package<<'\n';meta.close();
            clients.push_back(std::move(c));return "";}
        case Op::Stop:running.store(false);break;
        case Op::Delete:{
            if(!mediaName(text))return "{\"ok\":false,\"error\":\"invalid filename\"}";
            if(unlink((data+"/media/"+text).c_str()))return "{\"ok\":false,\"error\":\"cannot delete file\"}";
            if(selected==text){audio->mode.store(uint32_t(Mode::Real));paused=true;decoder.reset();selected.clear();sourcePosition=duration=0;resetGeneration();}
            break;}
        default:return "{\"ok\":false,\"error\":\"unknown command\"}";
        }
        if(r.op!=Op::Status)saveSelection();
        return status();
    }
};
int main(int argc,char** argv) {
    if(getuid()!=0){fprintf(stderr,"Root required\n");return 1;}
    std::string data=DEFAULT_DATA;
    if(argc==3&&std::string(argv[1])=="--data")data=argv[2];
    if(data.empty()||data.front()!='/'||data.size()>80)return 2;
    umask(0077);mkdir(data.c_str(),0700);mkdir((data+"/media").c_str(),0700);
    int lock=open((data+"/daemon.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if(lock<0||flock(lock,LOCK_EX|LOCK_NB))return 73;
    signal(SIGTERM,signalHandler);signal(SIGINT,signalHandler);signal(SIGPIPE,SIG_IGN);
    Engine engine(data);if(!engine.init()){perror("shared audio");return 1;}
    int server=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);
    sockaddr_un a{};a.sun_family=AF_UNIX;strcpy(a.sun_path,(data+"/control.sock").c_str());
    unlink(a.sun_path);
    if(server<0||bind(server,reinterpret_cast<sockaddr*>(&a),sizeof(a))||listen(server,16)){perror("listen");return 1;}
    timeval timeout{1,0};setsockopt(server,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    std::thread worker([&]{engine.pump();});
    while(running.load()) {
        int client=accept4(server,nullptr,nullptr,SOCK_CLOEXEC);if(client<0)continue;
        ucred peer{};socklen_t len=sizeof(peer);
        timeval ct{2,0};setsockopt(client,SOL_SOCKET,SO_RCVTIMEO,&ct,sizeof(ct));setsockopt(client,SOL_SOCKET,SO_SNDTIMEO,&ct,sizeof(ct));
        Request request{};
        if(!getsockopt(client,SOL_SOCKET,SO_PEERCRED,&peer,&len)&&peer.uid==0&&fullRead(client,&request,sizeof(request))) {
            std::string response=engine.request(request,client);
            if(!response.empty()){response+='\n';fullWrite(client,response.data(),response.size());}
        }
        close(client);
    }
    worker.join();close(server);unlink(a.sun_path);close(lock);return 0;
}

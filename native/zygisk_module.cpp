#include <sys/types.h>
#include "zygisk.hpp"
#include "capture.h"
#include <fcntl.h>
#include <new>
#include <thread>
#include <fstream>
#include <sstream>
#include <android/dlext.h>
#include <dlfcn.h>
using namespace gm;
static void companion(int socket) {
    Request request{};
    if(!fullRead(socket,&request,sizeof(request))||request.op!=Op::Attach||request.magic!=MAGIC)return;
    int server=connectDaemon(DEFAULT_DATA);if(server<0)return;
    timeval shortWait{0,200000};setsockopt(server,SOL_SOCKET,SO_RCVTIMEO,&shortWait,sizeof(shortWait));
    setsockopt(server,SOL_SOCKET,SO_SNDTIMEO,&shortWait,sizeof(shortWait));
    if(!fullWrite(server,&request,sizeof(request))){close(server);return;}
    int fds[2];
    if(receiveFds(server,fds)){sendFds(socket,fds[0],fds[1]);close(fds[0]);close(fds[1]);}
    close(server);
}
class GlassMic final:public zygisk::ModuleBase {
    zygisk::Api* api_=nullptr;JNIEnv* env_=nullptr;
    int fds_[2]{-1,-1};bool selected_=false;
    void* backend_=nullptr;
    void (*bind_)(SharedAudio*,ClientStats*)=nullptr;
    void (*metadata_)(JNIEnv*,void*)=nullptr;
    void (*install_)()=nullptr;
    SharedAudio* audio_=nullptr;ClientStats* stats_=nullptr;
public:
    void onLoad(zygisk::Api* api,JNIEnv* env) override {api_=api;env_=env;}
    void preServerSpecialize(zygisk::ServerSpecializeArgs*) override {api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);}
    void preAppSpecialize(zygisk::AppSpecializeArgs* args) override {
        if(args->uid<10000||!args->nice_name){api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;}
        const char* name=env_->GetStringUTFChars(args->nice_name,nullptr);
        if(!name){env_->ExceptionClear();api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;}
        Request r;r.op=Op::Attach;r.uid=uint32_t(args->uid);r.pid=uint32_t(getpid());
        strncpy(r.text,name,sizeof(r.text)-1);env_->ReleaseStringUTFChars(args->nice_name,name);
        // Read the public allowlist before contacting the root companion. Most app
        // launches therefore perform no IPC and immediately unload the module.
        int directory=api_->getModuleDir();bool listed=false;
        if(directory>=0) {
            int list=openat(directory,"targets.txt",O_RDONLY|O_CLOEXEC);
            if(list>=0) {
                char buffer[4096]{};ssize_t n=::read(list,buffer,sizeof(buffer)-1);close(list);
                if(n>0){std::istringstream input(std::string(buffer,size_t(n)));std::string package;
                    std::string base=r.text;base=base.substr(0,base.find(':'));
                    while(input>>package)if(package==base){listed=true;break;}}
            }
        }
        if(!listed){if(directory>=0)close(directory);api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;}
        struct Fd {int value=-1;~Fd(){if(value>=0)close(value);}} nothing;
        nothing.value=openat(directory,"lib/nothing/arm64-v8a/libshadowhook_nothing.so",O_RDONLY|O_CLOEXEC);
        int backendFd=openat(directory,"lib/arm64-v8a.so",O_RDONLY|O_CLOEXEC);close(directory);
        if(backendFd>=0){android_dlextinfo ext{};ext.flags=ANDROID_DLEXT_USE_LIBRARY_FD;ext.library_fd=backendFd;
            backend_=android_dlopen_ext("libglassmic_backend.so",RTLD_NOW,&ext);close(backendFd);}
        if(!backend_){api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;}
        bind_=reinterpret_cast<decltype(bind_)>(dlsym(backend_,"glassmic_bind"));
        metadata_=reinterpret_cast<decltype(metadata_)>(dlsym(backend_,"glassmic_metadata"));
        install_=reinterpret_cast<decltype(install_)>(dlsym(backend_,"glassmic_install"));
        auto prepare=reinterpret_cast<int(*)(int)>(dlsym(backend_,"glassmic_prepare"));
        if(!bind_||!metadata_||!install_||!prepare){dlclose(backend_);api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;}
        int sock=api_->connectCompanion();
        if(sock>=0){timeval wait{0,250000};setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&wait,sizeof(wait));
            setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&wait,sizeof(wait));
            if(fullWrite(sock,&r,sizeof(r)))selected_=receiveFds(sock,fds_);close(sock);}
        if(!selected_){dlclose(backend_);api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;}
        struct stat a{},b{};
        if(fstat(fds_[0],&a)||fstat(fds_[1],&b)||a.st_size!=sizeof(SharedAudio)||b.st_size!=sizeof(ClientStats)){
            close(fds_[0]);close(fds_[1]);selected_=false;api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;
        }
        void* audio=mmap(nullptr,sizeof(SharedAudio),PROT_READ,MAP_SHARED,fds_[0],0);
        void* stats=mmap(nullptr,sizeof(ClientStats),PROT_READ|PROT_WRITE,MAP_SHARED,fds_[1],0);
        close(fds_[0]);close(fds_[1]);
        if(audio==MAP_FAILED||stats==MAP_FAILED){
            if(audio!=MAP_FAILED)munmap(audio,sizeof(SharedAudio));
            if(stats!=MAP_FAILED)munmap(stats,sizeof(ClientStats));selected_=false;api_->setOption(zygisk::DLCLOSE_MODULE_LIBRARY);return;
        }
        audio_=static_cast<SharedAudio*>(audio);stats_=static_cast<ClientStats*>(stats);
        if(audio_->magic!=MAGIC||audio_->version!=VERSION){stats_->allowed.store(0);selected_=false;return;}
        // Mappings survive UID/SELinux specialization. No root FDs or IPC are
        // needed in the application's sandbox or its real-time audio callbacks.
        bind_(audio_,stats_);metadata_(env_,api_);
        // ShadowHook loads the module's helper library while it still has
        // Zygote's access to module files; installation reports any error.
        prepare(nothing.value);
    }
    void postAppSpecialize(const zygisk::AppSpecializeArgs*) override {
        if(!selected_)return;
        // Only the selected application's child installs hooks; never zygote/system_server.
        try {auto fn=install_;std::thread([fn]{fn();}).detach();}
        catch(...){stats_->installStage.store(4);stats_->hookError.store(1000);stats_->allowed.store(0);}
    }
};
REGISTER_ZYGISK_MODULE(GlassMic)
REGISTER_ZYGISK_COMPANION(companion)

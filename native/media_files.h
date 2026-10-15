#pragma once
#include "protocol.h"
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <vector>

namespace gm {
inline std::string mediaExtension(const std::string& name) {
    auto dot=name.rfind('.');if(dot==std::string::npos)return {};
    std::string ext=name.substr(dot);
    for(char& c:ext)c=char(std::tolower(static_cast<unsigned char>(c)));
    return ext==".wav"||ext==".mp3"||ext==".flac"?ext:std::string();
}
inline std::string fileError(const std::string& error) {
    return "{\"ok\":false,\"error\":"+json(error)+"}";
}
// The file browser only exposes shared user storage, never arbitrary root files.
inline std::string storagePath(const std::string& path,std::string& root) {
    char* resolved=realpath(path.c_str(),nullptr);if(!resolved)return {};
    std::string result=resolved;free(resolved);
    for(const char* prefix:{"/storage/emulated/","/data/media/"}) {
        std::string base=prefix;if(result.rfind(base,0)!=0)continue;
        size_t end=result.find('/',base.size());
        std::string user=result.substr(base.size(),end-base.size());
        if(user.empty()||!std::all_of(user.begin(),user.end(),[](unsigned char c){return std::isdigit(c);}))continue;
        root=base+user;return result;
    }
    return {};
}
inline std::string browseMedia(const std::string& requested) {
    std::string root,path=storagePath(requested,root);
    if(path.empty())return fileError("无法打开目录，请从手机内部存储选择");
    DIR* dir=opendir(path.c_str());if(!dir)return fileError(strerror(errno));
    struct Entry{std::string name;bool directory;int64_t bytes;};
    std::vector<Entry> entries;bool truncated=false;
    while(auto* e=readdir(dir)) {
        std::string name=e->d_name;if(name.empty()||name.front()=='.')continue;
        struct stat st{};
        if(fstatat(dirfd(dir),name.c_str(),&st,AT_SYMLINK_NOFOLLOW))continue;
        bool directory=S_ISDIR(st.st_mode);
        if(!directory&&(!S_ISREG(st.st_mode)||mediaExtension(name).empty()))continue;
        if(entries.size()>=10000){truncated=true;break;}
        entries.push_back({name,directory,st.st_size});
    }
    closedir(dir);
    std::sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b){return a.directory!=b.directory?a.directory>b.directory:a.name<b.name;});
    std::string parent=path==root?"":path.substr(0,path.rfind('/'));
    std::string out="{\"ok\":true,\"path\":"+json(path)+",\"parent\":"+json(parent)+",\"truncated\":"+(truncated?"true":"false")+",\"entries\":[";
    bool comma=false;for(auto& e:entries){if(comma)out+=',';comma=true;
        out+="{\"name\":"+json(e.name)+",\"directory\":"+(e.directory?"true":"false")+",\"bytes\":"+std::to_string(e.bytes)+"}";}
    return out+"]}";
}
inline std::string importMedia(const std::string& data,const std::string& requested) {
    std::string root,path=storagePath(requested,root);
    if(path.empty())return fileError("只能导入手机内部存储中的音频文件");
    std::string name=path.substr(path.rfind('/')+1),ext=mediaExtension(name);
    if(ext.empty())return fileError("当前支持 WAV、MP3 和 FLAC");
    int source=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(source<0)return fileError(strerror(errno));
    struct stat st{};
    if(fstat(source,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>512LL*1024*1024){close(source);return fileError("文件大小必须在 1 B 到 512 MB 之间");}
    if(!mediaName(name)||name.size()>160)name="Audio-"+std::to_string(nowNs())+ext;
    uint64_t started=nowNs();
    std::string part=data+"/media/.import-"+std::to_string(getpid())+"-"+std::to_string(started)+".part";
    int dest=open(part.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(dest<0){close(source);return fileError(strerror(errno));}
    auto buffer=std::make_unique<std::array<char,256*1024>>();
    int64_t copied=0;bool ok=true;
    while(copied<st.st_size){
        ssize_t got=read(source,buffer->data(),size_t(std::min<int64_t>(buffer->size(),st.st_size-copied)));
        if(got<0&&errno==EINTR)continue;
        if(got<=0||!fullWrite(dest,buffer->data(),size_t(got))){ok=false;break;}
        copied+=got;
    }
    if(ok)ok=fsync(dest)==0;
    int error=errno;close(source);close(dest);
    // Publish atomically without overwriting an existing imported copy.
    if(ok){
        std::string original=name;
        for(unsigned retry=0;;++retry){
            if(link(part.c_str(),(data+"/media/"+name).c_str())==0)break;
            if(errno!=EEXIST||retry>=100){ok=false;error=errno;break;}
            name=std::to_string(started+retry)+"-"+original;
        }
    }
    unlink(part.c_str());
    if(!ok)return fileError(error?strerror(error):"源文件在复制时发生变化，请重试");
    return "{\"ok\":true,\"file\":"+json(name)+",\"bytes\":"+std::to_string(copied)+",\"elapsedMs\":"+std::to_string((nowNs()-started)/1000000)+"}";
}
}

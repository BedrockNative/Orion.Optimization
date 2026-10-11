#pragma once
#include <Windows.h>
#include <string>
#include <vector>
#include <utility>
namespace orion {
// Watches only pack roots; world saves outside those roots do not invalidate.
class PackChanges {
    std::vector<std::wstring> roots_;
    std::vector<HANDLE> handles_;
    bool healthy_=false;
public:
    ~PackChanges(){clear();}
    PackChanges()=default;
    PackChanges(const PackChanges&)=delete;
    void clear(){for(auto h:handles_)FindCloseChangeNotification(h);handles_.clear();roots_.clear();healthy_=false;}
    bool configure(const std::vector<std::wstring>& roots) {
        if(healthy_ && roots==roots_) return true;
        clear();
        if(roots.empty()) return false;
        try {
            handles_.reserve(roots.size()); roots_=roots;
            // Wine's recursive watch does not cover every pre-existing child
            // directory. Register each directory explicitly, once at setup/change.
            std::vector<std::wstring> pending=roots;
            while(!pending.empty()) {
                auto root=std::move(pending.back());pending.pop_back();
                if(handles_.size()>=32768){clear();return false;}
                auto h=FindFirstChangeNotificationW(root.c_str(),FALSE,FILE_NOTIFY_CHANGE_FILE_NAME|FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_SIZE|FILE_NOTIFY_CHANGE_LAST_WRITE|FILE_NOTIFY_CHANGE_ATTRIBUTES|FILE_NOTIFY_CHANGE_SECURITY);
                if(h==INVALID_HANDLE_VALUE){clear();return false;}
                try {handles_.push_back(h);} catch(...) {FindCloseChangeNotification(h);throw;}
                WIN32_FIND_DATAW data;
                auto search=FindFirstFileW((root+L"\\*").c_str(),&data);
                if(search==INVALID_HANDLE_VALUE){clear();return false;}
                bool valid=true;
                do {
                    if(!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L".."))continue;
                    if(data.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT){valid=false;break;}
                    if(data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) {
                        try {pending.push_back(root+L"\\"+data.cFileName);}
                        catch(...){FindClose(search);throw;}
                    }
                } while(FindNextFileW(search,&data));
                const auto error=GetLastError();FindClose(search);
                if(!valid || error!=ERROR_NO_MORE_FILES){clear();return false;}
            }
            healthy_=true;return true;
        } catch(...) {clear();throw;}
    }
    // A notification or a broken watch invalidates everything depending on this stack.
    bool changed() {
        if(!healthy_)return true;
        bool dirty=false;
        for(size_t at=0;at<handles_.size();at+=MAXIMUM_WAIT_OBJECTS) {
            const DWORD count=static_cast<DWORD>((handles_.size()-at)<MAXIMUM_WAIT_OBJECTS ? handles_.size()-at : MAXIMUM_WAIT_OBJECTS);
            auto result=WaitForMultipleObjects(count,handles_.data()+at,FALSE,0);
            if(result==WAIT_TIMEOUT)continue;
            dirty=true;
            if(result<WAIT_OBJECT_0 || result>=WAIT_OBJECT_0+count)healthy_=false;
            break;
        }
        if(dirty && healthy_) {
            auto roots=roots_; healthy_=false;
            configure(roots); // Discover directories created/renamed since setup.
        }
        return dirty;
    }
    bool healthy()const{return healthy_;}
    bool matches(const std::vector<std::wstring>& roots)const{return roots==roots_;}
};
}

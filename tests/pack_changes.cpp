#include <orion/PackChanges.hpp>
#include <cstdio>
#include <string>
#define CHECK(x) do {if(!(x)){std::printf("FAIL line %d error %lu\n",__LINE__,GetLastError());return 1;}}while(0)
bool waitChange(orion::PackChanges& watch){for(int i=0;i<200;++i){if(watch.changed())return true;Sleep(10);}return false;}
int main(){
 wchar_t temp[MAX_PATH];CHECK(GetTempPathW(MAX_PATH,temp));
 auto root=std::wstring(temp)+L"orion-pack-watch-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
 CHECK(CreateDirectoryW(root.c_str(),nullptr));
 auto sub=root+L"\\nested";CHECK(CreateDirectoryW(sub.c_str(),nullptr));
 {
 orion::PackChanges watch; CHECK(watch.configure({root}));CHECK(!watch.changed());
 auto file=sub+L"\\texture.json";
 auto h=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(h!=INVALID_HANDLE_VALUE);
 DWORD written;CHECK(WriteFile(h,"a",1,&written,nullptr));CloseHandle(h);
 CHECK(watch.configure({root})); // Identical recomposition must retain pending notifications.
 CHECK(waitChange(watch));
 watch.changed();
 h=CreateFileW(file.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(h!=INVALID_HANDLE_VALUE);
 CHECK(WriteFile(h,"b",1,&written,nullptr));CHECK(FlushFileBuffers(h));CloseHandle(h);
 CHECK(waitChange(watch)); // Same name and same size, different contents.
 watch.changed();
 auto renamed=sub+L"\\renamed.json";CHECK(MoveFileW(file.c_str(),renamed.c_str()));CHECK(waitChange(watch));
 CHECK(DeleteFileW(renamed.c_str()));CHECK(waitChange(watch));
 CHECK(!watch.configure({root+L"\\missing"}));CHECK(!watch.healthy());CHECK(watch.changed());
 }
 CHECK(RemoveDirectoryW(sub.c_str()));CHECK(RemoveDirectoryW(root.c_str()));
 std::puts("PASS recursive change, same-size overwrite, unchanged watch preserves notification, rename, deletion, failure");
}

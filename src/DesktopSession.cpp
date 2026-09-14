#include "DesktopSession.h"
#include "DesktopHost.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>
#include <shlguid.h>
#include <wrl/client.h>
#include <cstdio>
#include <thread>
#include <mutex>
#include <memory>
#include <atomic>
#pragma comment(lib,"uuid.lib")
using Microsoft::WRL::ComPtr;
namespace nestlone {
namespace {
HWND nativeIcons=nullptr;
DWORD explorerPid=0;
HWND watchedIcons=nullptr;
DWORD watchedExplorer=0;
constexpr wchar_t lease[]=L"nestlone-D.DesktopOwner";
bool Valid(HWND hwnd,DWORD pid) {
    DWORD actual=0;GetWindowThreadProcessId(hwnd,&actual);
    wchar_t cls[64]{};GetClassNameW(hwnd,cls,64);
    return actual==pid && wcscmp(cls,L"SysListView32")==0;
}
bool View(ComPtr<IFolderView2>& view) {
    ComPtr<IShellWindows> windows;
    if(FAILED(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&windows))))return false;
    VARIANT empty{};long handle=0;ComPtr<IDispatch> dispatch;
    if(FAILED(windows->FindWindowSW(&empty,&empty,SWC_DESKTOP,&handle,SWFO_NEEDDISPATCH,&dispatch))||!dispatch)return false;
    ComPtr<IServiceProvider> provider;ComPtr<IShellBrowser> browser;ComPtr<IShellView> shellView;
    return SUCCEEDED(dispatch.As(&provider)) &&
        SUCCEEDED(provider->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser))) &&
        SUCCEEDED(browser->QueryActiveShellView(&shellView)) && SUCCEEDED(shellView.As(&view));
}
struct Snapshot {
    std::atomic<bool> running{false};
    std::mutex mutex;
    bool ready=false;
    std::vector<DesktopEntry> entries;
};
auto snapshot=std::make_shared<Snapshot>();
}
bool ReadDesktop(std::vector<DesktopEntry>& entries) {
    ComPtr<IFolderView2> view;if(!View(view))return false;
    auto host=db::DiscoverDesktopHost();
    if(!host.listview)return false;
    const int virtualLeft=GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtualTop=GetSystemMetrics(SM_YVIRTUALSCREEN);
    ComPtr<IShellFolder> folder;ComPtr<IEnumIDList> items;
    if(FAILED(view->GetFolder(IID_PPV_ARGS(&folder)))||FAILED(view->Items(SVGIO_ALLVIEW,IID_PPV_ARGS(&items))))return false;
    POINT spacing{76,91};view->GetSpacing(&spacing);
    FOLDERVIEWMODE mode{};int iconSize=32;view->GetViewModeAndIconSize(&mode,&iconSize);
    std::vector<DesktopEntry> found;
    PITEMID_CHILD item=nullptr;
    while(items->Next(1,&item,nullptr)==S_OK) {
        STRRET parsed{},display{};wchar_t path[32768]{},name[1024]{};
        if(SUCCEEDED(folder->GetDisplayNameOf(item,SHGDN_FORPARSING,&parsed)) &&
           SUCCEEDED(StrRetToBufW(&parsed,item,path,32768))) {
            folder->GetDisplayNameOf(item,SHGDN_NORMAL,&display);StrRetToBufW(&display,item,name,1024);
            DesktopEntry entry;entry.path=path;entry.name=name;
            view->GetItemPosition(item,&entry.position);
            // IFolderView positions are in the native list-view client space;
            // the canvas is positioned at the virtual-desktop origin.
            POINT screen=entry.position;
            ClientToScreen(host.listview,&screen);
            entry.position={screen.x-virtualLeft,screen.y-virtualTop};
            // Shell item positions are icon origins; include their centered labels.
            const LONG padding=max(0L,(spacing.x-iconSize)/2);
            entry.bounds={entry.position.x-padding,entry.position.y,entry.position.x-padding+spacing.x,entry.position.y+spacing.y};
            for(int size:{16,32}) {
                SHFILEINFOW fi{};
                if(SHGetFileInfoW(path,0,&fi,sizeof(fi),SHGFI_ICON|(size==16?SHGFI_SMALLICON:SHGFI_LARGEICON))) {
                    auto pixels=IconPixels(fi.hIcon,size);DestroyIcon(fi.hIcon);
                    if(size==16)entry.smallIcon=std::move(pixels);else entry.largeIcon=std::move(pixels);
                }
            }
            found.push_back(std::move(entry));
        }
        CoTaskMemFree(item);
    }
    entries=std::move(found);return true;
}
bool PollDesktop(std::vector<DesktopEntry>& entries) {
    auto state=snapshot;bool updated=false;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if(state->ready){entries=std::move(state->entries);state->ready=false;updated=true;}
    }
    bool expected=false;
    if(state->running.compare_exchange_strong(expected,true)) {
        std::thread([state] {
            CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
            std::vector<DesktopEntry> entries;
            if(ReadDesktop(entries)) {std::lock_guard<std::mutex> lock(state->mutex);state->entries=std::move(entries);state->ready=true;}
            CoUninitialize();state->running=false;
        }).detach();
    }
    return updated;
}
bool BeginDesktopSession() {
    if(nativeIcons)return DesktopSessionAlive();
    auto host=db::DiscoverDesktopHost();
    if(!host.listview || !IsWindowVisible(host.listview))return false;
    HRGN existing=CreateRectRgn(0,0,0,0);
    int kind=GetWindowRgn(host.listview,existing);DeleteObject(existing);
    if(kind!=ERROR)return false; // Do not overwrite another application's region.
    nativeIcons=host.listview;GetWindowThreadProcessId(nativeIcons,&explorerPid);
    const DWORD pid=GetCurrentProcessId();
    if(!SetPropW(nativeIcons,lease,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pid)))){nativeIcons=nullptr;return false;}
    if(watchedIcons==nativeIcons && watchedExplorer==explorerPid)return true;
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    std::wstring readyName=L"Local\\nestlone-recovery-ready-"+std::to_wstring(pid);
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());
    if(ready)ResetEvent(ready);
    std::wstring command=L"\""+std::wstring(exe)+L"\" --restore-desktop "+std::to_wstring(pid)+L" "+
        std::to_wstring(reinterpret_cast<ULONG_PTR>(nativeIcons))+L" "+std::to_wstring(explorerPid);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    bool launched=ready && CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);
    bool armed=launched && WaitForSingleObject(ready,5000)==WAIT_OBJECT_0;
    if(launched){CloseHandle(process.hThread);CloseHandle(process.hProcess);}if(ready)CloseHandle(ready);
    if(!armed){RemovePropW(nativeIcons,lease);nativeIcons=nullptr;return false;}
    watchedIcons=nativeIcons;watchedExplorer=explorerPid;return true;
}
bool MaskDesktopItems(const std::vector<DesktopEntry>& entries,const std::vector<std::wstring>& paths) {
    if(paths.empty()){EndDesktopSession();return true;}
    if(!BeginDesktopSession())return false;
    RECT bounds{};GetWindowRect(nativeIcons,&bounds);
    HRGN region=CreateRectRgn(0,0,bounds.right-bounds.left,bounds.bottom-bounds.top);
    const int virtualLeft=GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtualTop=GetSystemMetrics(SM_YVIRTUALSCREEN);
    for(const auto& entry:entries)for(const auto& path:paths)if(_wcsicmp(entry.path.c_str(),path.c_str())==0) {
        POINT topLeft{entry.bounds.left+virtualLeft,entry.bounds.top+virtualTop};
        POINT bottomRight{entry.bounds.right+virtualLeft,entry.bounds.bottom+virtualTop};
        ScreenToClient(nativeIcons,&topLeft);ScreenToClient(nativeIcons,&bottomRight);
        HRGN hole=CreateRectRgn(topLeft.x,topLeft.y,bottomRight.x,bottomRight.y);
        CombineRgn(region,region,hole,RGN_DIFF);DeleteObject(hole);break;
    }
    if(!SetWindowRgn(nativeIcons,region,TRUE)){DeleteObject(region);EndDesktopSession();return false;}
    return true;
}
void EndDesktopSession() {
    if(nativeIcons && Valid(nativeIcons,explorerPid) &&
       GetPropW(nativeIcons,lease)==reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(GetCurrentProcessId()))) {
        SetWindowRgn(nativeIcons,nullptr,TRUE);RemovePropW(nativeIcons,lease);
    }
    nativeIcons=nullptr;
}
bool DesktopSessionAlive() {
    return nativeIcons && Valid(nativeIcons,explorerPid) && IsWindowVisible(nativeIcons);
}
void PlaceDesktopItem(const std::wstring& path,POINT point) {
    const int virtualLeft=GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int virtualTop=GetSystemMetrics(SM_YVIRTUALSCREEN);
    auto host=db::DiscoverDesktopHost();
    if(!host.listview)return;
    POINT nativePoint{point.x+virtualLeft,point.y+virtualTop};
    ScreenToClient(host.listview,&nativePoint);
    std::thread([path,nativePoint]() mutable {
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        {
        ComPtr<IFolderView2> view;
        if(View(view)) {
            ComPtr<IShellFolder> folder;view->GetFolder(IID_PPV_ARGS(&folder));
            ComPtr<IEnumIDList> items;view->Items(SVGIO_ALLVIEW,IID_PPV_ARGS(&items));
            PITEMID_CHILD item=nullptr;
            while(items && items->Next(1,&item,nullptr)==S_OK) {
                STRRET parsed{};wchar_t name[32768]{};
                folder->GetDisplayNameOf(item,SHGDN_FORPARSING,&parsed);StrRetToBufW(&parsed,item,name,32768);
                bool match=_wcsicmp(name,path.c_str())==0;
                if(match){PCUITEMID_CHILD array[]={item};view->SelectAndPositionItems(1,array,&nativePoint,SVSI_POSITIONITEM);}
                CoTaskMemFree(item);if(match)break;
            }
        }
        }
        CoUninitialize();
    }).detach();
}
int DesktopRecovery(const wchar_t* arguments) {
    DWORD owner=0,explorer=0;unsigned long long raw=0;
    if(swscanf_s(arguments,L"--restore-desktop %lu %llu %lu",&owner,&raw,&explorer)!=3)return 2;
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,owner);if(!process)return 3;
    std::wstring name=L"Local\\nestlone-recovery-ready-"+std::to_wstring(owner);
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());if(ready){SetEvent(ready);CloseHandle(ready);}
    WaitForSingleObject(process,INFINITE);CloseHandle(process);
    HWND hwnd=reinterpret_cast<HWND>(static_cast<ULONG_PTR>(raw));
    if(Valid(hwnd,explorer) && GetPropW(hwnd,lease)==reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(owner))) {
        SetWindowRgn(hwnd,nullptr,TRUE);RemovePropW(hwnd,lease);
    }
    return 0;
}
}

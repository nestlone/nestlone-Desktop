#include "DesktopSession.h"
#include "DesktopHost.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>
#include <shlguid.h>
#include <wrl/client.h>
#include <shellapi.h>
#include <cstdio>
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
    DWORD actual=0;
    GetWindowThreadProcessId(hwnd,&actual);
    wchar_t cls[64]{};
    GetClassNameW(hwnd,cls,64);
    return actual==pid && wcscmp(cls,L"SysListView32")==0;
}
}
bool ReadDesktop(std::vector<DesktopEntry>& entries) {
    ComPtr<IShellWindows> windows;
    if(FAILED(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&windows))))return false;
    VARIANT empty{};long handle=0;ComPtr<IDispatch> dispatch;
    if(FAILED(windows->FindWindowSW(&empty,&empty,SWC_DESKTOP,&handle,SWFO_NEEDDISPATCH,&dispatch))||!dispatch)return false;
    ComPtr<IServiceProvider> provider;ComPtr<IShellBrowser> browser;ComPtr<IShellView> shellView;ComPtr<IFolderView> view;
    if(FAILED(dispatch.As(&provider))||FAILED(provider->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser)))||
       FAILED(browser->QueryActiveShellView(&shellView))||FAILED(shellView.As(&view)))return false;
    ComPtr<IShellFolder> folder;ComPtr<IEnumIDList> items;
    if(FAILED(view->GetFolder(IID_PPV_ARGS(&folder)))||FAILED(view->Items(SVGIO_ALLVIEW,IID_PPV_ARGS(&items))))return false;
    std::vector<DesktopEntry> found;
    PITEMID_CHILD item=nullptr;
    while(items->Next(1,&item,nullptr)==S_OK) {
        STRRET parsed{},display{};wchar_t path[32768]{},name[1024]{};
        if(SUCCEEDED(folder->GetDisplayNameOf(item,SHGDN_FORPARSING,&parsed)) &&
           SUCCEEDED(StrRetToBufW(&parsed,item,path,32768))) {
            folder->GetDisplayNameOf(item,SHGDN_NORMAL,&display);
            StrRetToBufW(&display,item,name,1024);
            DesktopEntry entry{path,name,{}};
            view->GetItemPosition(item,&entry.position);
            HWND viewWindow=nullptr;shellView->GetWindow(&viewWindow);
            if(viewWindow)ClientToScreen(viewWindow,&entry.position);
            entry.position.x-=GetSystemMetrics(SM_XVIRTUALSCREEN);
            entry.position.y-=GetSystemMetrics(SM_YVIRTUALSCREEN);
            found.push_back(std::move(entry));
        }
        CoTaskMemFree(item);
    }
    entries=std::move(found);
    return true;
}
bool BeginDesktopSession() {
    if(nativeIcons)return DesktopSessionAlive();
    auto host=db::DiscoverDesktopHost();
    if(!host.listview || !IsWindowVisible(host.listview))return false;
    nativeIcons=host.listview;
    GetWindowThreadProcessId(nativeIcons,&explorerPid);
    const DWORD pid=GetCurrentProcessId();
    if(!SetPropW(nativeIcons,lease,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(pid)))) {nativeIcons=nullptr;return false;}
    if(watchedIcons==nativeIcons && watchedExplorer==explorerPid) {
        ShowWindow(nativeIcons,SW_HIDE);
        if(!IsWindowVisible(nativeIcons))return true;
        EndDesktopSession();return false;
    }
    wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
    std::wstring readyName=L"Local\\nestlone-recovery-ready-"+std::to_wstring(pid);
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,readyName.c_str());
    std::wstring command=L"\""+std::wstring(exe)+L"\" --restore-desktop "+std::to_wstring(pid)+L" "+
        std::to_wstring(reinterpret_cast<ULONG_PTR>(nativeIcons))+L" "+std::to_wstring(explorerPid);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    bool launched=ready && CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process);
    bool armed=launched && WaitForSingleObject(ready,5000)==WAIT_OBJECT_0;
    if(launched) {CloseHandle(process.hThread);CloseHandle(process.hProcess);}
    if(ready)CloseHandle(ready);
    if(!armed) {RemovePropW(nativeIcons,lease);nativeIcons=nullptr;return false;}
    watchedIcons=nativeIcons;watchedExplorer=explorerPid;
    ShowWindow(nativeIcons,SW_HIDE);
    if(!IsWindowVisible(nativeIcons))return true;
    EndDesktopSession();return false;
}
void EndDesktopSession() {
    if(nativeIcons && Valid(nativeIcons,explorerPid) &&
       GetPropW(nativeIcons,lease)==reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(GetCurrentProcessId()))) {
        ShowWindow(nativeIcons,SW_SHOW);
        RemovePropW(nativeIcons,lease);
    }
    nativeIcons=nullptr;
}
bool DesktopSessionAlive() {
    return nativeIcons && Valid(nativeIcons,explorerPid) && !IsWindowVisible(nativeIcons);
}
int DesktopRecovery(const wchar_t* arguments) {
    DWORD owner=0,explorer=0;unsigned long long raw=0;
    if(swscanf_s(arguments,L"--restore-desktop %lu %llu %lu",&owner,&raw,&explorer)!=3)return 2;
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,owner);
    if(!process)return 3;
    std::wstring name=L"Local\\nestlone-recovery-ready-"+std::to_wstring(owner);
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());
    if(ready){SetEvent(ready);CloseHandle(ready);}
    WaitForSingleObject(process,INFINITE);CloseHandle(process);
    HWND hwnd=reinterpret_cast<HWND>(static_cast<ULONG_PTR>(raw));
    if(Valid(hwnd,explorer) && GetPropW(hwnd,lease)==reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(owner))) {
        ShowWindow(hwnd,SW_SHOW);RemovePropW(hwnd,lease);
    }
    return 0;
}
}

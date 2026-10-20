#include "DesktopSession.h"
#include "DesktopHost.h"
#include <shlobj.h>
#include <shlwapi.h>
#include <exdisp.h>
#include <shlguid.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <memory>
#include <algorithm>
#include <cstdio>
#pragma comment(lib,"uuid.lib")
using Microsoft::WRL::ComPtr;
namespace nestlone {
namespace {
constexpr wchar_t lease[]=L"nestlone-D.DesktopOwner";
constexpr wchar_t hiddenLease[]=L"nestlone-D.HiddenListViewOwner";
bool ShellView(ComPtr<IShellView>& view) {
    ComPtr<IShellWindows> windows;
    if(FAILED(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&windows))))return false;
    VARIANT empty{};long handle=0;ComPtr<IDispatch> dispatch;
    if(FAILED(windows->FindWindowSW(&empty,&empty,SWC_DESKTOP,&handle,SWFO_NEEDDISPATCH,&dispatch))||!dispatch)return false;
    ComPtr<IServiceProvider> provider;ComPtr<IShellBrowser> browser;
    return SUCCEEDED(dispatch.As(&provider)) && SUCCEEDED(provider->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser))) &&
        SUCCEEDED(browser->QueryActiveShellView(&view));
}
bool View(ComPtr<IFolderView2>& view) {
    ComPtr<IShellView> shellView;
    return ShellView(shellView) && SUCCEEDED(shellView.As(&view));
}
struct Worker {
    std::mutex mutex;
    bool running=false,ready=false;
    uint64_t requested=0,applied=0;
    std::vector<DesktopMove> moves;
    DesktopSnapshot result;
};
auto state=std::make_shared<Worker>();
ComPtr<IContextMenu2> backgroundMenu2;
ComPtr<IContextMenu3> backgroundMenu3;
}
bool ShellBackgroundMenuAvailable() {
    ComPtr<IShellView> view;ComPtr<IContextMenu> menu;
    return ShellView(view) && SUCCEEDED(view->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&menu)));
}

bool ShowShellBackgroundMenu(HWND owner, POINT screenPoint, bool* createBox) {
    if(createBox)*createBox=false;
    ComPtr<IShellView> view;
    if(!ShellView(view))return false;
    ComPtr<IContextMenu> menu;
    if(FAILED(view->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&menu))))return false;
    HMENU popup=CreatePopupMenu();
    if(!popup)return false;
    constexpr UINT first=1,last=0x7fff;
    bool shown=false;
    if(SUCCEEDED(menu->QueryContextMenu(popup,0,first,last,CMF_NORMAL))) {
        constexpr UINT newBoxCommand=0x8000; // Outside Shell's command range.
        if(createBox) {
            InsertMenuW(popup,0,MF_BYPOSITION|MF_STRING,newBoxCommand,L"新建盒子(&B)");
            InsertMenuW(popup,1,MF_BYPOSITION|MF_SEPARATOR,0,nullptr);
        }
        // Explorer populates several desktop submenus only when they receive
        // their WM_INITMENUPOPUP / owner-draw messages.  Our layered canvas is
        // the menu owner, therefore retain the interfaces for the duration of
        // TrackPopupMenuEx and forward those messages from DecorationProc.
        backgroundMenu2.Reset();backgroundMenu3.Reset();
        if(FAILED(menu.As(&backgroundMenu3)))menu.As(&backgroundMenu2);
        int command=TrackPopupMenuEx(popup,TPM_RETURNCMD|TPM_RIGHTBUTTON,screenPoint.x,screenPoint.y,owner,nullptr);
        backgroundMenu3.Reset();backgroundMenu2.Reset();
        if(createBox&&command==newBoxCommand) {
            *createBox=true;shown=true;
        } else if(command>=static_cast<int>(first)&&command<=static_cast<int>(last)) {
            CMINVOKECOMMANDINFOEX invoke{};invoke.cbSize=sizeof(invoke);invoke.fMask=CMIC_MASK_UNICODE;
            invoke.hwnd=owner;invoke.lpVerb=MAKEINTRESOURCEA(command-first);invoke.lpVerbW=MAKEINTRESOURCEW(command-first);invoke.nShow=SW_SHOWNORMAL;
            shown=SUCCEEDED(menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke)));
        } else shown=true;
    }
    DestroyMenu(popup);return shown;
}
bool PasteDesktopClipboard(HWND owner) {
    ComPtr<IShellView> view;ComPtr<IContextMenu> menu;
    if(!ShellView(view)||FAILED(view->GetItemObject(SVGIO_BACKGROUND,IID_PPV_ARGS(&menu))))return false;
    HMENU popup=CreatePopupMenu();if(!popup)return false;
    constexpr UINT first=1,last=0x7fff;bool pasted=false;
    if(SUCCEEDED(menu->QueryContextMenu(popup,0,first,last,CMF_NORMAL))) {
        const int count=GetMenuItemCount(popup);
        for(int index=0;index<count;++index) {
            const UINT command=GetMenuItemID(popup,index);
            if(command==static_cast<UINT>(-1)||command<first||command>last)continue;
            char verb[64]{};
            if(FAILED(menu->GetCommandString(command-first,GCS_VERBA,nullptr,verb,sizeof(verb)))||_stricmp(verb,"paste")!=0)continue;
            CMINVOKECOMMANDINFOEX invoke{};invoke.cbSize=sizeof(invoke);invoke.fMask=CMIC_MASK_UNICODE;invoke.hwnd=owner;
            invoke.lpVerb=MAKEINTRESOURCEA(command-first);invoke.lpVerbW=MAKEINTRESOURCEW(command-first);invoke.nShow=SW_SHOWNORMAL;
            pasted=SUCCEEDED(menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke)));break;
        }
    }
    DestroyMenu(popup);return pasted;
}
bool ForwardShellContextMenuMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result) {
    if(message!=WM_INITMENUPOPUP&&message!=WM_DRAWITEM&&message!=WM_MEASUREITEM&&message!=WM_MENUCHAR)return false;
    if(backgroundMenu3) {
        LRESULT value=0;
        if(SUCCEEDED(backgroundMenu3->HandleMenuMsg2(message,wParam,lParam,&value))) {
            if(result)*result=value;return true;
        }
    }
    if(backgroundMenu2&&SUCCEEDED(backgroundMenu2->HandleMenuMsg(message,wParam,lParam))) {
        if(result)*result=0;return true;
    }
    return false;
}

bool ReadDesktop(DesktopSnapshot& result) {
    result={};
    auto host=db::DiscoverDesktopHost();
    if(!host.listview){result.error=L"等待 Windows 桌面恢复";return false;}
    result.listview=host.listview;GetWindowThreadProcessId(host.listview,&result.process);
    ComPtr<IFolderView2> view;
    if(!View(view)){result.error=L"无法读取桌面 Shell 视图";return false;}
    ComPtr<IShellFolder> folder;
    if(FAILED(view->GetFolder(IID_PPV_ARGS(&folder))))return false;
    DWORD flags=0;view->GetCurrentFolderFlags(&flags);
    result.autoArrange=(flags&FWF_AUTOARRANGE)!=0 || (GetWindowLongPtrW(host.listview,GWL_STYLE)&LVS_AUTOARRANGE)!=0;
    FOLDERVIEWMODE mode{};
    view->GetViewModeAndIconSize(&mode,&result.iconSize);
    result.iconMode=mode==FVM_ICON || mode==FVM_SMALLICON;
    view->GetSpacing(&result.spacing);
    result.spacing.x=max(result.spacing.x,result.iconSize+24L);
    result.spacing.y=max(result.spacing.y,result.iconSize+40L);
    POINT origin{};ClientToScreen(host.listview,&origin);
    origin.x-=GetSystemMetrics(SM_XVIRTUALSCREEN);origin.y-=GetSystemMetrics(SM_YVIRTUALSCREEN);
    int count=0;if(FAILED(view->ItemCount(SVGIO_ALLVIEW,&count)))return false;
    for(int i=0;i<count;++i) {
        PITEMID_CHILD item=nullptr;
        if(FAILED(view->Item(i,&item))||!item)continue;
        STRRET parsed{},display{};wchar_t path[32768]{},name[32768]{};POINT position{};
        if(SUCCEEDED(folder->GetDisplayNameOf(item,SHGDN_FORPARSING,&parsed)) &&
           SUCCEEDED(StrRetToBufW(&parsed,item,path,32768)) && SUCCEEDED(view->GetItemPosition(item,&position))) {
            DesktopEntry entry;entry.path=path;entry.index=i;
            // SHGDN_FORPARSING is an identifier used to locate the item again;
            // it is not a label.  In particular, Recycle Bin is represented
            // by a ::{GUID} namespace name rather than a file path.
            if(SUCCEEDED(folder->GetDisplayNameOf(item,SHGDN_NORMAL,&display)) &&
               SUCCEEDED(StrRetToBufW(&display,item,name,32768))) entry.displayName=name;
            if(entry.displayName.empty()) entry.displayName=entry.path;
            entry.position={position.x+origin.x,position.y+origin.y};
            LONG padding=max(0L,(result.spacing.x-result.iconSize)/2);
            entry.bounds={entry.position.x-padding,entry.position.y,entry.position.x-padding+result.spacing.x,entry.position.y+result.spacing.y};
            result.entries.push_back(std::move(entry));
        }
        CoTaskMemFree(item);
    }
    result.readable=true;return true;
}
HRESULT CreateDesktopDataObject(const std::vector<std::wstring>& paths,IDataObject** output) {
    if(!output)return E_POINTER;*output=nullptr;
    if(paths.empty())return E_INVALIDARG;
    ComPtr<IFolderView2> view;ComPtr<IShellFolder> folder;
    if(!View(view)||FAILED(view->GetFolder(IID_PPV_ARGS(&folder))))return E_FAIL;
    PIDLIST_ABSOLUTE parent=nullptr;
    HRESULT result=SHGetIDListFromObject(folder.Get(),&parent);
    if(FAILED(result)||!parent)return FAILED(result)?result:E_FAIL;
    std::vector<PITEMID_CHILD> children;children.reserve(paths.size());
    int count=0;result=view->ItemCount(SVGIO_ALLVIEW,&count);
    if(SUCCEEDED(result))for(int index=0;index<count&&children.size()<paths.size();++index) {
        PITEMID_CHILD item=nullptr;STRRET parsed{};wchar_t name[32768]{};
        if(SUCCEEDED(view->Item(index,&item))&&item) {
            const bool matches=SUCCEEDED(folder->GetDisplayNameOf(item,SHGDN_FORPARSING,&parsed))&&
                SUCCEEDED(StrRetToBufW(&parsed,item,name,32768))&&std::any_of(paths.begin(),paths.end(),[&](const auto& path){return _wcsicmp(path.c_str(),name)==0;});
            if(matches)if(auto copy=ILClone(item))children.push_back(copy);
            CoTaskMemFree(item);
        }
    }
    if(SUCCEEDED(result)&&children.size()==paths.size()) {
        std::vector<PCUITEMID_CHILD> childViews;childViews.reserve(children.size());
        for(const auto child:children)childViews.push_back(child);
        result=SHCreateDataObject(parent,static_cast<UINT>(childViews.size()),childViews.data(),nullptr,IID_PPV_ARGS(output));
    }
    else if(SUCCEEDED(result))result=HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    for(auto child:children)CoTaskMemFree(child);
    CoTaskMemFree(parent);return result;
}
bool ForwardDesktopShortcut(const std::vector<std::wstring>& selectedPaths,UINT virtualKey) {
    auto host=db::DiscoverDesktopHost();ComPtr<IFolderView2> view;ComPtr<IShellFolder> folder;
    if(!host.listview||!View(view)||FAILED(view->GetFolder(IID_PPV_ARGS(&folder))))return false;
    bool focused=false;int count=0;
    if(FAILED(view->ItemCount(SVGIO_ALLVIEW,&count)))return false;
    for(int index=0;index<count;++index) {
        PITEMID_CHILD item=nullptr;STRRET parsed{};wchar_t name[32768]{};
        if(SUCCEEDED(view->Item(index,&item))&&item) {
            const bool selected=SUCCEEDED(folder->GetDisplayNameOf(item,SHGDN_FORPARSING,&parsed))&&
                SUCCEEDED(StrRetToBufW(&parsed,item,name,32768))&&std::any_of(selectedPaths.begin(),selectedPaths.end(),[&](const auto& path){return _wcsicmp(path.c_str(),name)==0;});
            if(selected) {
                DWORD flags=SVSI_SELECT;
                if(!focused){flags|=SVSI_DESELECTOTHERS|SVSI_FOCUSED;focused=true;}
                view->SelectItem(index,flags);
            }
            CoTaskMemFree(item);
        }
    }
    if(!focused)view->SelectItem(-1,SVSI_DESELECTOTHERS);
    const LPARAM keyData=1|(static_cast<LPARAM>(MapVirtualKeyW(virtualKey,MAPVK_VK_TO_VSC))<<16);
    DWORD_PTR reply=0;
    const bool down=SendMessageTimeoutW(host.listview,WM_KEYDOWN,virtualKey,keyData,SMTO_ABORTIFHUNG,300,&reply)!=0;
    SendMessageTimeoutW(host.listview,WM_KEYUP,virtualKey,keyData|0xC0000000,SMTO_ABORTIFHUNG,300,&reply);
    return down;
}
HRESULT CreateDesktopBackgroundDropTarget(HWND owner,IDropTarget** output) {
    if(!output)return E_POINTER;*output=nullptr;
    // The active view owns the best target because it understands the
    // desktop's current namespace and view settings.  Some Explorer builds
    // do not expose SVGIO_BACKGROUND while the ListView is hidden, so retain
    // the desktop folder's documented CreateViewObject target as a fallback.
    ComPtr<IShellView> shellView;
    if(ShellView(shellView)) {
        const HRESULT fromView=shellView->GetItemObject(SVGIO_BACKGROUND,IID_IDropTarget,reinterpret_cast<void**>(output));
        if(SUCCEEDED(fromView)&&*output)return S_OK;
    }
    ComPtr<IFolderView2> view;ComPtr<IShellFolder> folder;
    if(!View(view)||FAILED(view->GetFolder(IID_PPV_ARGS(&folder))))return E_FAIL;
    return folder->CreateViewObject(owner,IID_IDropTarget,reinterpret_cast<void**>(output));
}
uint64_t QueueDesktopMoves(const std::vector<DesktopMove>& moves) {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->moves=moves;return ++state->requested;
}
void CancelDesktopMoves() {QueueDesktopMoves({});}
bool PollDesktop(DesktopSnapshot& output) {
    auto worker=state;bool updated=false;
    {
        std::lock_guard<std::mutex> lock(worker->mutex);
        if(worker->ready){output=std::move(worker->result);worker->ready=false;updated=true;}
        if(worker->running)return updated;
        worker->running=true;
    }
    std::thread([worker] {
        HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        DesktopSnapshot next;
        if(SUCCEEDED(com)) {
            uint64_t generation=0,previous=0;std::vector<DesktopMove> moves;
            {std::lock_guard<std::mutex> lock(worker->mutex);generation=worker->requested;previous=worker->applied;moves=worker->moves;}
            if(ReadDesktop(next) && generation!=previous && !moves.empty()) {
                if(next.autoArrange)next.error=L"请在桌面右键 → 查看中关闭“自动排列图标”，才能整理位置";
                else if(!next.iconMode)next.error=L"当前桌面视图不支持自由定位";
                else {
                    ComPtr<IFolderView2> view;ComPtr<IShellFolder> folder;
                    if(View(view))view->GetFolder(IID_PPV_ARGS(&folder));
                    for(const auto& move:moves) {
                        {std::lock_guard<std::mutex> lock(worker->mutex);if(worker->requested!=generation)break;}
                        auto found=std::find_if(next.entries.begin(),next.entries.end(),[&](const auto& e){return _wcsicmp(e.path.c_str(),move.path.c_str())==0;});
                        if(found==next.entries.end() || !view || !folder)continue;
                        // Verify the index still refers to the same Shell item immediately
                        // before posting a scalar-only cross-process list-view message.
                        PITEMID_CHILD pidl=nullptr;STRRET parsed{};wchar_t path[32768]{};
                        if(FAILED(view->Item(found->index,&pidl))||!pidl)continue;
                        bool same=SUCCEEDED(folder->GetDisplayNameOf(pidl,SHGDN_FORPARSING,&parsed)) &&
                            SUCCEEDED(StrRetToBufW(&parsed,pidl,path,32768)) && _wcsicmp(path,move.path.c_str())==0;
                        DWORD process=0;GetWindowThreadProcessId(next.listview,&process);
                        POINT point{move.position.x+GetSystemMetrics(SM_XVIRTUALSCREEN),move.position.y+GetSystemMetrics(SM_YVIRTUALSCREEN)};
                        ScreenToClient(next.listview,&point);
                        if(point.x<0 || point.x>32767 || point.y<0 || point.y>32767)next.error=L"目标超出原生图标定位范围，图标保留原位";
                        else if(same && process==next.process) {
                            DWORD_PTR reply=0;
                            if(!SendMessageTimeoutW(next.listview,LVM_SETITEMPOSITION,found->index,MAKELPARAM(point.x,point.y),SMTO_ABORTIFHUNG,300,&reply)||!reply)
                                next.error=L"图标定位被拒绝：请确认程序与 Explorer 权限一致";
                            else {
                                POINT actual{};
                                if(FAILED(view->GetItemPosition(pidl,&actual)) || abs(actual.x-point.x)>2 || abs(actual.y-point.y)>2)
                                    next.error=L"Windows 调整了图标位置，请检查对齐网格和显示器边界";
                            }
                        }
                        CoTaskMemFree(pidl);
                    }
                }
                std::wstring error=next.error;
                ReadDesktop(next);next.error=error;
            }
            next.applied=generation;
            {std::lock_guard<std::mutex> lock(worker->mutex);worker->applied=generation;}
            CoUninitialize();
        } else next.error=L"无法初始化桌面 Shell 服务";
        {std::lock_guard<std::mutex> lock(worker->mutex);worker->result=std::move(next);worker->ready=true;worker->running=false;}
    }).detach();
    return updated;
}
void ClaimHiddenDesktopListView(HWND listview) {
    if(listview)SetPropW(listview,hiddenLease,reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(GetCurrentProcessId())));
}
void ReleaseHiddenDesktopListView(HWND listview) {
    if(!listview)return;
    ShowWindow(listview,SW_SHOWNOACTIVATE);RemovePropW(listview,hiddenLease);
}
bool RestoreHiddenDesktopListView() {
    auto host=db::DiscoverDesktopHost();
    if(!host.listview)return false;
    HWND listview=host.listview;
    DWORD owner=static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(GetPropW(listview,hiddenLease)));
    if(!owner || owner==GetCurrentProcessId())return false;
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,owner);
    bool dead=process?WaitForSingleObject(process,0)==WAIT_OBJECT_0:GetLastError()==ERROR_INVALID_PARAMETER;
    if(process)CloseHandle(process);
    if(!dead)return false;
    ReleaseHiddenDesktopListView(listview);return true;
}
void RestoreLegacyMask(HWND listview) {
    DWORD owner=static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(GetPropW(listview,lease)));
    if(!owner)return;
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,owner);
    bool dead=process?WaitForSingleObject(process,0)==WAIT_OBJECT_0:GetLastError()==ERROR_INVALID_PARAMETER;
    if(process)CloseHandle(process);
    if(dead){SetWindowRgn(listview,nullptr,TRUE);RemovePropW(listview,lease);}
}
int DesktopRecovery(const wchar_t* args) {
    DWORD owner=0,pid=0;unsigned long long raw=0;
    if(swscanf_s(args,L"--restore-desktop %lu %llu %lu",&owner,&raw,&pid)!=3)return 2;
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,owner);if(!process)return 3;
    std::wstring name=L"Local\\nestlone-recovery-ready-"+std::to_wstring(owner);
    HANDLE ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,name.c_str());if(ready){SetEvent(ready);CloseHandle(ready);}
    WaitForSingleObject(process,INFINITE);CloseHandle(process);
    HWND hwnd=reinterpret_cast<HWND>(static_cast<ULONG_PTR>(raw));DWORD actual=0;GetWindowThreadProcessId(hwnd,&actual);
    if(actual==pid)RestoreLegacyMask(hwnd);
    return 0;
}
}

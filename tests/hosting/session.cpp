#include "../../src/DesktopCanvas.cpp"
#include <cstdio>
#include <commctrl.h>
#include <exdisp.h>
#include <shlguid.h>
#include <shlobj.h>
#include <wrl/client.h>
void PrintVisibilityCapabilities() {
    using Microsoft::WRL::ComPtr;
    ComPtr<IShellWindows> windows;ComPtr<IDispatch> dispatch;
    ComPtr<IServiceProvider> provider;ComPtr<IShellBrowser> browser;ComPtr<IShellView> view;
    VARIANT empty{};long handle=0;
    if(FAILED(CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&windows))) ||
       FAILED(windows->FindWindowSW(&empty,&empty,SWC_DESKTOP,&handle,SWFO_NEEDDISPATCH,&dispatch)) ||
       FAILED(dispatch.As(&provider)) || FAILED(provider->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser))) ||
       FAILED(browser->QueryActiveShellView(&view)))return;
    ComPtr<IFolderFilterSite> filter;ComPtr<IShellFolderView> legacy;
    printf("Desktop IFolderFilterSite QueryInterface=0x%08lx\n",static_cast<unsigned long>(view.As(&filter)));
    printf("Desktop IShellFolderView QueryInterface=0x%08lx\n",static_cast<unsigned long>(view.As(&legacy)));
}
int wmain(int argc,wchar_t** argv) {
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if(argc>1 && wcscmp(argv[1],L"--close-old")==0) {
        HWND old=FindWindowExW(HWND_MESSAGE,nullptr,L"nestlone-D.Control",L"nestlone-D");
        if(!old)old=FindWindowW(L"nestlone-D.Control",L"nestlone-D");
        if(old){DWORD_PTR result=0;SendMessageTimeoutW(old,WM_COMMAND,1004,0,SMTO_ABORTIFHUNG,3000,&result);Sleep(500);}
    }
    int failures=0;
    auto check=[&](bool ok,const char* what){printf("%s %s\n",ok?"PASS":"FAIL",what);fflush(stdout);if(!ok)++failures;};
    nestlone::DesktopSnapshot before;
    check(nestlone::ReadDesktop(before),"read native desktop through Shell");if(failures)return 1;
    if(argc>1 && wcscmp(argv[1],L"--capabilities")==0){PrintVisibilityCapabilities();CoUninitialize();return 0;}
    if(argc>1 && wcscmp(argv[1],L"--diagnose")==0) {
        printf("autoArrange=%d iconMode=%d count=%zu listview=%p\n",before.autoArrange,before.iconMode,before.entries.size(),before.listview);
        POINT sample{};GetCursorPos(&sample);
        if(argc>=4)sample={_wtol(argv[2]),_wtol(argv[3])};
        for(POINT p: {sample}) {
            HWND hit=WindowFromPoint(p);wchar_t name[128]{};GetClassNameW(hit,name,128);
            DWORD pid=0;GetWindowThreadProcessId(hit,&pid);
            printf("point=%ld,%ld hit=%p class=%ls pid=%lu\n",p.x,p.y,hit,name,pid);
            HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            if(process){wchar_t path[32768]{};DWORD size=32768;if(QueryFullProcessImageNameW(process,0,path,&size))wprintf(L"hit process=%ls\n",path);CloseHandle(process);}
        }
        CoUninitialize();return 0;
    }
    nestlone::RestoreLegacyMask(before.listview);
    HRGN nativeRegion=CreateRectRgn(0,0,0,0);
    check(GetWindowRgn(before.listview,nativeRegion)==ERROR,"native view has no mask");
    DWORD_PTR selectedBefore=0;SendMessageTimeoutW(before.listview,LVM_GETSELECTEDCOUNT,0,0,SMTO_ABORTIFHUNG,1000,&selectedBefore);
    nestlone::Layout layout;nestlone::Box box;box.id=L"native-test";box.title=L"Native desktop";box.rect={700,140,1100,500};layout.boxes.push_back(box);
    check(nestlone::CreateCanvas(GetModuleHandleW(nullptr),nullptr,&layout),"create decoration manager");
    check(nestlone::g_background && IsWindow(nestlone::g_background),"layered background created on real desktop");
    check((GetWindowLongPtrW(nestlone::g_background,GWL_EXSTYLE)&WS_EX_TRANSPARENT)==0,"owned surface receives icon input");
    check(GetParent(nestlone::g_background)==GetParent(nestlone::g_host.defview),"background is a sibling of the native Shell view");
    check(GetWindow(nestlone::g_background,GW_HWNDPREV)!=nestlone::g_host.defview,"owned surface is above the native view");
    SetWindowPos(nestlone::g_background,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    nestlone::KeepSurfaceAboveIcons();
    check(GetWindow(nestlone::g_background,GW_HWNDPREV)!=nestlone::g_host.defview,"owned surface z-order repairs above native view");
    check(nestlone::g_decorations.size()==1 && !nestlone::g_visualIcons.empty(),"owned title surface and shell icons created");
    check(nestlone::ShellBackgroundMenuAvailable(),"Explorer background context menu is available");
    check(GetWindowRgn(before.listview,nativeRegion)==ERROR && !IsWindowVisible(before.listview),"Explorer icons are hidden while owned view is active");
    nestlone::DesktopSnapshot after;nestlone::ReadDesktop(after);
    bool unchanged=before.entries.size()==after.entries.size();
    for(const auto& e:before.entries){auto* now=nestlone::FindEntry(after,e.path);if(!now||now->position.x!=e.position.x||now->position.y!=e.position.y)unchanged=false;}
    check(unchanged,"starting decorations does not move any real icon");
    DWORD_PTR selectedAfter=0;SendMessageTimeoutW(before.listview,LVM_GETSELECTEDCOUNT,0,0,SMTO_ABORTIFHUNG,1000,&selectedAfter);
    check(selectedAfter==selectedBefore,"Explorer selection state is preserved for restoration");
    if(argc>1 && wcscmp(argv[argc-1],L"--position-test")==0) {
        auto disposable=std::find_if(before.entries.begin(),before.entries.end(),[](const auto& e){return e.path.find(L"DeskBox-hosting-test-20260914")!=std::wstring::npos;});
        check(disposable!=before.entries.end(),"dedicated disposable desktop test item exists");
        if(disposable!=before.entries.end() && !before.autoArrange && before.iconMode) {
            POINT destination=disposable->position;
            bool free=false;
            for(LONG y=100;y<GetSystemMetrics(SM_CYSCREEN)-150 && !free;y+=before.spacing.y)for(LONG x=500;x<GetSystemMetrics(SM_CXSCREEN)-150 && !free;x+=before.spacing.x) {
                RECT candidate{x,y,x+before.spacing.x,y+before.spacing.y};bool collision=false;
                for(const auto& e:before.entries){RECT overlap{};if(IntersectRect(&overlap,&candidate,&e.bounds))collision=true;}
                if(!collision){destination={x+(before.spacing.x-before.iconSize)/2,y};free=true;}
            }
            check(free,"find an unoccupied test position");
            auto wait=[&](uint64_t generation,nestlone::DesktopSnapshot& result) {
                for(int n=0;n<200;++n){if(nestlone::PollDesktop(result)&&result.applied>=generation)return true;Sleep(30);}return false;
            };
            if(free) {
                nestlone::DesktopSnapshot moved;
                auto generation=nestlone::QueueDesktopMoves({{disposable->path,destination}});
                check(wait(generation,moved),"position worker completes");
                auto current=nestlone::FindEntry(moved,disposable->path);
                check(current && (current->position.x!=disposable->position.x || current->position.y!=disposable->position.y),"LVM_SETITEMPOSITION moves the real Explorer item");
                generation=nestlone::QueueDesktopMoves({{disposable->path,disposable->position}});
                nestlone::DesktopSnapshot restored;check(wait(generation,restored),"restore worker completes");
                current=nestlone::FindEntry(restored,disposable->path);
                check(current && current->position.x==disposable->position.x && current->position.y==disposable->position.y,"test item restored to its original position");
                bool others=true;
                for(const auto& e:before.entries)if(e.path!=disposable->path){auto item=nestlone::FindEntry(restored,e.path);if(!item||item->position.x!=e.position.x||item->position.y!=e.position.y)others=false;}
                check(others,"positioning leaves every other desktop icon unchanged");
            }
        } else if(disposable!=before.entries.end())check(false,"disable auto-arrange and use an icon view for the opt-in positioning test");
    }
    // Test adoption without issuing input to Explorer or moving user files.
    nestlone::g_snapshot=before;
    nestlone::DesktopSnapshot a=before,b=before;
    nestlone::DesktopEntry synthetic;synthetic.path=L"::test-only-entry";synthetic.position={20,20};
    a.entries.push_back(synthetic);synthetic.position={760,240};b.entries.push_back(synthetic);
    nestlone::Observe(a,b);check(nestlone::HasPath(layout.boxes[0],synthetic.path),"native movement into rectangle records membership");
    nestlone::Observe(b,a);check(!nestlone::HasPath(layout.boxes[0],synthetic.path),"native movement out releases membership");
    nestlone::DestroyCanvas();
    check(GetWindowRgn(before.listview,nativeRegion)==ERROR && IsWindowVisible(before.listview),"exit restores the original native view");
    DeleteObject(nativeRegion);CoUninitialize();return failures?1:0;
}

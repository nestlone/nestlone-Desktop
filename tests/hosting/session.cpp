#include "../../src/DesktopCanvas.cpp"
#include "../../src/DesktopHost.h"
#include <cstdio>
#include <shlobj.h>
#include <gdiplus.h>
int wmain(int argc,wchar_t** argv) {
    if(argc>1 && wcscmp(argv[1],L"--restore-desktop")==0) {
        std::wstring args;for(int i=1;i<argc;++i){if(i>1)args+=L" ";args+=argv[i];}
        return nestlone::DesktopRecovery(args.c_str());
    }
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;Gdiplus::GdiplusStartup(&token,&input,nullptr);
    int failures=0;
    auto check=[&](bool ok,const char* message){printf("%s %s\n",ok?"PASS":"FAIL",message);fflush(stdout);if(!ok)++failures;};
    auto host=db::DiscoverDesktopHost();
    auto noMask=[&] {HRGN r=CreateRectRgn(0,0,0,0);int result=GetWindowRgn(host.listview,r);DeleteObject(r);return result==ERROR;};
    if(argc>1 && wcscmp(argv[1],L"--verify-native")==0) {
        for(int i=0;i<40 && !noMask();++i)Sleep(50);
        check(IsWindowVisible(host.listview)&&noMask(),"native desktop visible and region restored");
        Gdiplus::GdiplusShutdown(token);CoUninitialize();return failures?1:0;
    }
    check(host.listview && IsWindowVisible(host.listview)&&noMask(),"native desktop initially visible with no custom region");
    if(failures)return 1;
    nestlone::Layout layout;nestlone::Box box;box.id=L"test";box.title=L"Mask test";box.rect={600,120,960,400};layout.boxes.push_back(box);
    check(nestlone::CreateCanvas(GetModuleHandleW(nullptr),host.wallpaperWorker?host.wallpaperWorker:GetDesktopWindow(),&layout),"create overlay without Shell work on paint thread");
    check(IsWindowVisible(host.listview)&&noMask(),"startup leaves native desktop untouched");
    std::vector<nestlone::DesktopEntry> entries;
    check(nestlone::ReadDesktop(entries),"read metadata for test");
    nestlone::g_desktop=entries;
    auto found=std::find_if(entries.begin(),entries.end(),[](const auto& e){return e.name==L"DeskBox-hosting-test-20260914";});
    check(found!=entries.end(),"disposable test folder exists");
    if(found!=entries.end() && !failures) {
        auto entry=*found;DWORD attrs=GetFileAttributesW(entry.path.c_str());
        HWND canvas=nestlone::g_canvas;
        const SIZE_T bytes=sizeof(DROPFILES)+(entry.path.size()+2)*sizeof(wchar_t);
        HGLOBAL drop=GlobalAlloc(GHND,bytes);
        auto data=static_cast<DROPFILES*>(GlobalLock(drop));data->pFiles=sizeof(DROPFILES);data->fWide=TRUE;data->pt={680,240};
        memcpy(reinterpret_cast<BYTE*>(data)+sizeof(DROPFILES),entry.path.c_str(),(entry.path.size()+1)*sizeof(wchar_t));
        GlobalUnlock(drop);
        SendMessageW(canvas,WM_DROPFILES,reinterpret_cast<WPARAM>(drop),0);
        check(layout.boxes[0].items.size()==1,"desktop drop is accepted");
        HRGN region=CreateRectRgn(0,0,0,0);int kind=GetWindowRgn(host.listview,region);
        check(kind!=ERROR && !PtInRegion(region,(entry.bounds.left+entry.bounds.right)/2,(entry.bounds.top+entry.bounds.bottom)/2),"only managed tile excluded from native view");
        check(IsWindowVisible(host.listview),"native icon window remains visible after drop");
        int otherVisible=0;
        for(const auto& e:entries)if(e.path!=entry.path &&
            PtInRegion(region,(e.bounds.left+e.bounds.right)/2,(e.bounds.top+e.bounds.bottom)/2))++otherVisible;
        check(otherVisible==static_cast<int>(entries.size())-1,"all other native icon areas remain visible");
        DeleteObject(region);
        if(argc>1 && wcscmp(argv[1],L"--crash-test")==0){printf("Intentional termination\n");fflush(stdout);TerminateProcess(GetCurrentProcess(),77);}
        auto item=nestlone::ItemRect(layout.boxes[0],0);
        POINT out{450,420};
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(item.left+10,item.top+10));
        SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(out.x,out.y));
        SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(out.x,out.y));
        check(layout.boxes[0].items.empty() && noMask(),"drag out restores native tile");
        Sleep(500);
        nestlone::PlaceDesktopItem(entry.path,entry.position);Sleep(500);
        check(GetFileAttributesW(entry.path.c_str())==attrs,"file attributes unchanged");
        std::vector<nestlone::DesktopEntry> after;nestlone::ReadDesktop(after);
        int same=0;
        for(const auto& before:entries)if(before.path!=entry.path)for(const auto& e:after)
            if(before.path==e.path && before.position.x==e.position.x && before.position.y==e.position.y)++same;
        check(same==static_cast<int>(entries.size())-1,"all other native icon positions unchanged");
    }
    nestlone::DestroyCanvas();check(IsWindowVisible(host.listview)&&noMask(),"normal exit restores original native region");
    Gdiplus::GdiplusShutdown(token);CoUninitialize();return failures?1:0;
}

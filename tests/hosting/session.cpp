#include "../../src/DesktopCanvas.cpp"
#include "../../src/DesktopHost.h"
#include <cstdio>
#include <gdiplus.h>

int wmain(int argc,wchar_t** argv) {
    if(argc>1 && wcscmp(argv[1],L"--restore-desktop")==0) {
        std::wstring args;
        for(int i=1;i<argc;++i){if(i>1)args+=L" ";args+=argv[i];}
        return nestlone::DesktopRecovery(args.c_str());
    }
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
    Gdiplus::GdiplusStartup(&token,&input,nullptr);
    int failures=0;
    auto check=[&](bool ok,const char* message){printf("%s %s\n",ok?"PASS":"FAIL",message);fflush(stdout);if(!ok)++failures;};
    auto host=db::DiscoverDesktopHost();
    if(argc>1 && wcscmp(argv[1],L"--verify-native")==0) {
        for(int i=0;i<40 && !IsWindowVisible(host.listview);++i)Sleep(50);
        check(IsWindowVisible(host.listview),"recovery restores native icons after forced termination");
        Gdiplus::GdiplusShutdown(token);CoUninitialize();return failures?1:0;
    }
    check(host.listview && IsWindowVisible(host.listview),"native desktop initially visible");
    if(failures)return 1;
    nestlone::Layout layout;nestlone::Box box;
    box.id=L"test-box";box.title=L"Hosting test";box.rect={600,120,960,400};
    layout.boxes.push_back(box);
    check(nestlone::CreateCanvas(GetModuleHandleW(nullptr),host.wallpaperWorker ? host.wallpaperWorker : GetDesktopWindow(),&layout),"create hosting canvas and arm recovery");
    check(!IsWindowVisible(host.listview),"native icons suppressed");
    if(argc>1 && wcscmp(argv[1],L"--crash-test")==0) {
        printf("Intentional termination to test recovery\n");fflush(stdout);
        TerminateProcess(GetCurrentProcess(),77);
    }
    if(!failures && !nestlone::g_desktop.empty()) {
        auto entry=nestlone::g_desktop.front();
        for(const auto& candidate:nestlone::g_desktop)
            if(candidate.name.find(L"DeskBox-hosting-test")!=std::wstring::npos){entry=candidate;break;}
        DWORD attributes=GetFileAttributesW(entry.path.c_str());
        auto source=nestlone::DesktopRect(entry);
        POINT p{source.left+24,source.top+20},inside{680,240},outside{450,420};
        HWND canvas=nestlone::g_canvas;
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(p.x,p.y));
        SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(inside.x,inside.y));
        SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(inside.x,inside.y));
        check(layout.boxes[0].items.size()==1 && nestlone::InBox(entry.path),"desktop to box changes ownership exactly once");
        check(nestlone::HitDesktop(p).empty(),"boxed icon is absent from desktop hit testing");
        auto item=nestlone::ItemRect(layout.boxes[0],0);
        POINT boxPoint{item.left+10,item.top+10};
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(boxPoint.x,boxPoint.y));
        SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(outside.x,outside.y));
        SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(outside.x,outside.y));
        check(layout.boxes[0].items.empty() && !nestlone::InBox(entry.path),"box to desktop clears ownership");
        check(nestlone::HitDesktop(outside)==entry.path,"desktop icon placed at drop location");
        SendMessageW(canvas,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(outside.x,outside.y));
        SendMessageW(canvas,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(inside.x,inside.y));
        SendMessageW(canvas,WM_KEYDOWN,VK_ESCAPE,0);
        SendMessageW(canvas,WM_LBUTTONUP,0,MAKELPARAM(inside.x,inside.y));
        check(layout.boxes[0].items.empty(),"Escape cancels a drag without ownership changes");
        auto path=entry.path;
        nestlone::Box second;second.id=L"second";layout.boxes.push_back(second);
        nestlone::AssignDesktopItem(layout,path,0,{});
        nestlone::AssignDesktopItem(layout,path,1,{});
        check(layout.boxes[0].items.empty() && layout.boxes[1].items.size()==1,"box-to-box transfer has one owner");
        nestlone::AssignDesktopItem(layout,path,-1,{412,404});
        nestlone::SaveLayout(layout);
        check(GetFileAttributesW(entry.path.c_str())==attributes,"file attributes unchanged");
        auto loaded=nestlone::LoadLayout();
        check(!loaded.desktop.empty() && loaded.desktop.back().path==entry.path,"drop location persists to isolated test layout");
        nestlone::HandleCanvasCommand(nestlone::CanvasCommand::Toggle);
        check(IsWindowVisible(host.listview),"pause restores native desktop");
        nestlone::HandleCanvasCommand(nestlone::CanvasCommand::Toggle);
        check(!IsWindowVisible(host.listview),"resume hides native desktop");
    }
    nestlone::DestroyCanvas();
    check(IsWindowVisible(host.listview),"normal exit restores native desktop");
    Gdiplus::GdiplusShutdown(token);CoUninitialize();
    return failures ? 1 : 0;
}

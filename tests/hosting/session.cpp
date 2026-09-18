#include "../../src/DesktopCanvas.cpp"
#include <cstdio>
#include <commctrl.h>
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
    nestlone::RestoreLegacyMask(before.listview);
    HRGN nativeRegion=CreateRectRgn(0,0,0,0);
    check(GetWindowRgn(before.listview,nativeRegion)==ERROR,"native view has no mask");
    DWORD_PTR selectedBefore=0;SendMessageTimeoutW(before.listview,LVM_GETSELECTEDCOUNT,0,0,SMTO_ABORTIFHUNG,1000,&selectedBefore);
    nestlone::Layout layout;nestlone::Box box;box.id=L"native-test";box.title=L"Native desktop";box.rect={700,140,1100,500};layout.boxes.push_back(box);
    check(nestlone::CreateCanvas(GetModuleHandleW(nullptr),nullptr,&layout),"create decoration manager");
    check(nestlone::g_background && IsWindow(nestlone::g_background),"layered background created on real desktop");
    check((GetWindowLongPtrW(nestlone::g_background,GWL_EXSTYLE)&WS_EX_TRANSPARENT)!=0,"background is input transparent");
    check(GetParent(nestlone::g_background)==GetParent(nestlone::g_host.defview),"background is a sibling of the native Shell view");
    bool below=false;
    for(HWND h=GetWindow(nestlone::g_host.defview,GW_HWNDNEXT);h;h=GetWindow(h,GW_HWNDNEXT))if(h==nestlone::g_background)below=true;
    check(below,"background z-order is below native icons");
    check(nestlone::g_decorations.size()==1,"only title and corner handle receive box input");
    check(GetWindowRgn(before.listview,nativeRegion)==ERROR && IsWindowVisible(before.listview),"native icons stay visible and unmasked");
    nestlone::DesktopSnapshot after;nestlone::ReadDesktop(after);
    bool unchanged=before.entries.size()==after.entries.size();
    for(const auto& e:before.entries){auto* now=nestlone::FindEntry(after,e.path);if(!now||now->position.x!=e.position.x||now->position.y!=e.position.y)unchanged=false;}
    check(unchanged,"starting decorations does not move any real icon");
    DWORD_PTR selectedAfter=0;SendMessageTimeoutW(before.listview,LVM_GETSELECTEDCOUNT,0,0,SMTO_ABORTIFHUNG,1000,&selectedAfter);
    check(selectedAfter==selectedBefore,"native selection is untouched");
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
    check(GetWindowRgn(before.listview,nativeRegion)==ERROR && IsWindowVisible(before.listview),"exit leaves the original native view intact");
    DeleteObject(nativeRegion);CoUninitialize();return failures?1:0;
}

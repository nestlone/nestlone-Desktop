#include "../../src/DesktopCanvas.cpp"
#include "../../src/Settings.h"
#include <cstdio>
int main() {
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
    if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return 1;
    int failures=0;auto check=[&](bool ok,const char* text){printf("%s %s\n",ok?"PASS":"FAIL",text);if(!ok)++failures;};
    nestlone::Layout layout;nestlone::Box box;box.rect={20,20,300,220};box.title=L"Native";layout.boxes.push_back(box);
    std::vector<DWORD> low(320*240),high(low.size()),withItems(low.size());
    layout.opacity=20;check(nestlone::RenderPixels(low.data(),320,240,layout),"render background at 20%");
    layout.opacity=100;check(nestlone::RenderPixels(high.data(),320,240,layout),"render background at 100%");
    check((low[150*320+100]>>24)==51 && (high[150*320+100]>>24)==255,"only background opacity is rendered");
    layout.opacity=0;check(nestlone::RenderPixels(low.data(),320,240,layout),"render background at 0%");
    check((low[150*320+100]>>24)==0,"zero opacity removes only the box background");
    layout.opacity=100;layout.cornerRadius=0;check(nestlone::RenderPixels(low.data(),320,240,layout),"render square corners");
    // GDI+ uses edge coverage at the exact rectangle boundary; test one pixel inside it.
    check((low[21*320+21]>>24)==255,"zero corner setting fills the box corner");
    layout.cornerRadius=12;
    layout.boxes[0].items={L"C:\\anything.lnk",L"::virtual-item"};
    nestlone::RenderPixels(withItems.data(),320,240,layout);
    check(withItems==high,"adding icons to a box never draws their pixels or labels");
    check(low[0]==0 && high[0]==0,"outside boxes stays transparent");
    bool premultiplied=true;for(DWORD p:low){DWORD a=p>>24;if((p&255)>a||((p>>8)&255)>a||((p>>16)&255)>a)premultiplied=false;}
    check(premultiplied,"premultiplied alpha is valid");
    layout.boxes[0].collapsed=true;nestlone::RenderPixels(low.data(),320,240,layout);
    check(low[150*320+100]==0,"collapse removes only background body");
    nestlone::DesktopSnapshot snapshot;snapshot.readable=true;snapshot.iconMode=true;
    snapshot.spacing={80,96};snapshot.iconSize=32;
    auto beforeRefresh=snapshot;
    beforeRefresh.entries.push_back({L"item",L"Visible name"});
    auto afterRefresh=beforeRefresh;
    afterRefresh.applied=1;
    using nestlone::DesktopVisualChange;
    check(nestlone::CompareDesktopVisuals(beforeRefresh,afterRefresh)==DesktopVisualChange::None,
        "identical desktop contents skip repaint after move acknowledgement");
    afterRefresh.entries[0].position.x=24;
    check(nestlone::CompareDesktopVisuals(beforeRefresh,afterRefresh)==DesktopVisualChange::Geometry,
        "desktop item movement updates geometry");
    afterRefresh.entries[0].displayName=L"Updated name";
    check(nestlone::CompareDesktopVisuals(beforeRefresh,afterRefresh)==DesktopVisualChange::Items,
        "desktop display name change repaints even with identical paths");
    nestlone::Box grid;grid.rect={103,57,387,300};grid.iconView=true;
    for(int i=0;i<7;++i){nestlone::DesktopEntry e;e.path=std::to_wstring(i);snapshot.entries.push_back(e);grid.items.push_back(e.path);}
    grid.rect=nestlone::FitGrid(grid,snapshot);
    auto moves=nestlone::GridMoves(grid,snapshot);
    check(moves.size()==7,"grid retains every native item");
    check(moves[0].position.y==57+nestlone::HeaderHeight()+12,"grid starts below header with padding");
    check(moves[3].position.y==moves[0].position.y+96,"three columns wrap relative to box width");
    bool inside=true;
    for(const auto& m:moves)if(m.position.x-24<grid.rect.left+12 || m.position.x-24+80>grid.rect.right-12 || m.position.y+96>grid.rect.bottom-24)inside=false;
    check(inside,"complete icon and label cells stay inside padding and away from grip");
    grid.rect.right=grid.rect.left+184;grid.rect.bottom=grid.rect.top+100;
    grid.rect=nestlone::FitGrid(grid,snapshot);moves=nestlone::GridMoves(grid,snapshot);
    check(moves[2].position.y==moves[0].position.y+96,"narrow box adapts to two columns");
    check(grid.rect.bottom>=moves.back().position.y+96+24,"minimum height prevents clipped final row");
    RECT original=grid.rect;OffsetRect(&grid.rect,31,19);auto translated=nestlone::GridMoves(grid,snapshot);
    check(translated[0].position.x==moves[0].position.x+31 && translated[0].position.y==moves[0].position.y+19,"box grid is independent of desktop grid origin");

    nestlone::Layout ownership;nestlone::Box first;first.id=L"first";nestlone::Box second;second.id=L"second";
    first.items={L"C:\\DeskBox\\one.lnk"};second.items={L"C:\\DeskBox\\two.lnk"};
    ownership.boxes={first,second};ownership.desktop.push_back({L"C:\\DeskBox\\one.lnk",{10,20}});
    check(nestlone::AssignDesktopItem(ownership,L"C:\\DeskBox\\one.lnk",1,{30,40}),"move item into another box");
    check(ownership.boxes[0].items.empty() && ownership.boxes[1].items.size()==2,"item has one box owner after reassignment");
    check(ownership.desktop.empty(),"box assignment removes stale desktop placement");
    check(nestlone::AssignDesktopItem(ownership,L"C:\\DeskBox\\one.lnk",-1,{130,140}),"move item back to desktop");
    check(ownership.boxes[1].items.size()==1 && ownership.desktop.size()==1 && ownership.desktop[0].point.x==130,"desktop assignment removes box ownership and stores position");
    nestlone::Box list;list.rect={10,10,220,80};list.items={L"0",L"1"};list.iconView=false;auto listRect=nestlone::FitGrid(list,snapshot);check(listRect.bottom>=list.rect.top+nestlone::HeaderHeight()+16+2*nestlone::HeaderHeight(),"list view expands one row per item");check(nestlone::BoxSlot(list,{20,list.rect.top+nestlone::HeaderHeight()+10})==0,"list view hit testing uses rows");
    nestlone::Layout groups;
    nestlone::Box a,b,c;a.id=L"a";a.title=L"A";a.groupTitle=L"Group";a.activeTabId=L"c";
    b.id=L"b";b.groupId=L"a";c.id=L"c";c.groupId=L"a";groups.boxes={a,b,c};nestlone::g_layout=&groups;
    nestlone::ReorderTab(groups.boxes[0],3);
    auto ordered=nestlone::GroupTabs(groups.boxes[0]);
    check(ordered[0]->id==L"b"&&ordered[1]->id==L"c"&&ordered[2]->id==L"a","root tab can move to end without changing group root");
    check(groups.boxes[0].activeTabId==L"c"&&groups.boxes[1].groupId==L"a","tab sorting preserves active content and group ownership");
    nestlone::ReorderTab(groups.boxes[2],0);ordered=nestlone::GroupTabs(groups.boxes[0]);
    check(ordered[0]->id==L"c"&&ordered[1]->id==L"b"&&ordered[2]->id==L"a","member can move from middle to first");
    nestlone::ReorderTab(groups.boxes[2],1);ordered=nestlone::GroupTabs(groups.boxes[0]);
    check(ordered[0]->id==L"c","drop beside original slot keeps order");
    nestlone::Layout::Widget savedNote,legacyWeather;
    savedNote.id=L"saved-note";savedNote.text=L"保留这张便签";savedNote.locked=true;
    savedNote.closed=true;savedNote.color=RGB(248,231,233);
    legacyWeather.id=L"old-weather";legacyWeather.type=L"weather";
    groups.widgets={savedNote,legacyWeather};
    groups.autoOrganize=true;
    groups.autoRules={{L"a",false,L".pdf; DOCX;zip"},{L"b",true,L"*",false}};
    check(nestlone::MatchesAutoRule(groups.autoRules[0],false,L".docx")&&
        !nestlone::MatchesAutoRule(groups.autoRules[0],false,L".doc"),"auto rules match complete suffixes without case sensitivity");
    check(!nestlone::MatchesAutoRule(groups.autoRules[1],true,L"")&&
        !nestlone::MatchesAutoRule(groups.autoRules[0],true,L".pdf"),"automatic classification respects disabled rules and folder matching");
    check(nestlone::SaveLayout(groups),"sorted layout saves to isolated test file");
    groups=nestlone::LoadLayout();ordered=nestlone::GroupTabs(groups.boxes[0]);
    check(groups.widgets.size()==1&&groups.widgets[0].id==savedNote.id&&
        groups.widgets[0].text==savedNote.text&&groups.widgets[0].locked&&
        groups.widgets[0].closed&&groups.widgets[0].color==savedNote.color,
        "note text, color, lock and closed state survive reload");
    check(groups.autoOrganize&&groups.autoRules.size()==2&&groups.autoRules[0].enabled&&!groups.autoRules[1].enabled,
        "automatic classification and rule checkbox state survive reload");
    check(ordered[0]->id==L"c"&&ordered[2]->id==L"a"&&groups.boxes[0].activeTabId==L"c","saved tab order and active selection survive reload");
    nestlone::DetachTab(groups.boxes[0]);
    check(groups.boxes[2].groupId.empty()&&groups.boxes[1].groupId==L"c","detaching reordered root promotes first remaining visual tab");
    groups.boxes={a,b,c};
    nestlone::DetachTab(groups.boxes[0]);
    check(groups.boxes[0].groupId.empty()&&!nestlone::HasGroupTabs(groups.boxes[0]),"first tab detaches independently");
    check(groups.boxes[1].groupId.empty()&&groups.boxes[2].groupId==L"b","remaining tabs receive a new root");
    check(groups.boxes[1].groupTitle==L"Group"&&groups.boxes[1].activeTabId==L"c","root promotion preserves title and active tab");
    nestlone::DetachTab(groups.boxes[2]);
    check(groups.boxes[2].groupId.empty()&&groups.boxes[1].activeTabId.empty(),"active member detaches and remaining root becomes active");
    groups.boxes[0].items={L"cached-item"};
    nestlone::VisualIcon cached;cached.path=L"cached-item";
    nestlone::g_previewGroup=L"a";
    check(!nestlone::RenderIconInCurrentPass(cached),"moving group icons excluded from static desktop");
    nestlone::g_renderOnlyGroup=L"a";
    check(nestlone::RenderIconInCurrentPass(cached),"moving group icons included in drag cache");
    cached.path=L"loose-item";
    check(!nestlone::RenderIconInCurrentPass(cached),"loose desktop icons excluded from drag cache");
    nestlone::g_renderOnlyGroup.clear();nestlone::g_previewGroup.clear();
    check(nestlone::RenderIconInCurrentPass(cached),"normal rendering restored after drag");
    nestlone::g_layout=nullptr;
    RECT work{0,0,1920,1040};
    auto bounded=nestlone::ConstrainBoxToWorkArea({-100,-80,300,220},work,64);
    check(bounded.left==0&&bounded.top==0&&bounded.right==400,"offscreen title is recovered without resizing");
    bounded=nestlone::ConstrainBoxToWorkArea({1800,1000,2200,1300},work,64);
    check(bounded.right==1920&&bounded.top==976&&bounded.bottom==1276,"only title stays above taskbar while content extends below screen");
    bounded=nestlone::ConstrainBoxToWorkArea({100,300,500,1300},work,64);
    check(bounded.top==300&&bounded.bottom==1300,"near-screen-height box can move vertically without body clamping");
    bounded=nestlone::ConstrainBoxToWorkArea({-200,1100,2300,2600},work,64);
    check(bounded.left==0&&bounded.top==976,"oversized box retains accessible title");
    const RECT noteWork{0,0,1366,728},settingsBounds{200,80,940,700};
    const RECT beside=nestlone::NewNoteRect(noteWork,settingsBounds,20);
    RECT noteOverlap{};
    check(beside.left>settingsBounds.right&&!IntersectRect(&noteOverlap,&beside,&settingsBounds),
        "new note is positioned beside the settings window when space is available");
    const RECT smallerWork{0,0,800,600};
    const RECT fallback=nestlone::NewNoteRect(smallerWork,smallerWork,20);
    check(fallback.left>=0&&fallback.top>=0&&fallback.right<=800&&fallback.bottom<=600,
        "new note still fits the work area when settings fills the screen");
    // Exercise real note HWND creation without attaching to Explorer.
    nestlone::Layout widgets;
    nestlone::Layout::Widget firstNote;firstNote.id=L"first-note";firstNote.rect={20,20,280,170};
    widgets.widgets.push_back(firstNote);nestlone::g_layout=&widgets;
    nestlone::g_instance=GetModuleHandleW(nullptr);nestlone::g_visible=false;
    HWND host=CreateWindowExW(0,L"STATIC",L"test",WS_POPUP,0,0,800,600,nullptr,nullptr,nestlone::g_instance,nullptr);
    nestlone::g_host.defviewParent=host;
    HWND background=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"background",WS_POPUP,0,0,800,600,nullptr,nullptr,nestlone::g_instance,nullptr);
    nestlone::g_background=background;nestlone::g_host.defview=host;
    check(!nestlone::CanvasCreateNote(background)&&widgets.widgets.size()==1,
        "creating a note while the desktop is hidden does not add an invisible entry");
    HWND normal=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"STATIC",L"normal app",WS_POPUP,0,0,10,10,nullptr,nullptr,nestlone::g_instance,nullptr);
    SetWindowPos(host,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    HWND beforeDesktop=GetWindow(host,GW_HWNDPREV);
    SetWindowPos(normal,beforeDesktop,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    nestlone::g_visible=true;nestlone::UpdateSurfaceVisibility();
    check(GetWindow(background,GW_OWNER)==nullptr,"desktop canvas has no external Explorer owner");
    check(GetWindow(background,GW_HWNDNEXT)==host&&GetWindow(background,GW_HWNDPREV)==normal,"canvas stays below app and above desktop");
    check(IsWindowVisible(background),"foreground app does not hide desktop canvas");
    nestlone::g_visible=false;nestlone::UpdateSurfaceVisibility();
    check(!IsWindowVisible(background),"explicit hide still hides canvas");
    WNDCLASSW wc{};wc.hInstance=nestlone::g_instance;wc.lpfnWndProc=nestlone::WidgetProc;wc.lpszClassName=L"nestlone-D.Widget";RegisterClassW(&wc);
    nestlone::SyncWidgets();
    check(nestlone::g_widgets.size()==1,"first widget creates without another component");
    HWND firstWidget=nestlone::g_widgets.front()->window;
    check(GetParent(firstWidget)==nullptr&&GetAncestor(firstWidget,GA_ROOT)==firstWidget,
        "note has its own top-level paint surface");
    check((GetWindowLongPtrW(firstWidget,GWL_STYLE)&WS_VISIBLE)==0,"creating while hidden does not reveal widget");
    widgets.widgets[0].text=std::wstring(4500,L'便');
    nestlone::SyncWidgets();
    check(nestlone::NoteText(nestlone::g_widgets[0]->edit)==widgets.widgets[0].text&&
        !nestlone::g_widgets[0]->pendingSave,"restoring a long note updates the existing editor");
    SendMessageW(firstWidget,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(30,15));
    check(nestlone::g_widgets[0]->dragging&&GetCapture()==firstWidget,"first component title captures drag immediately");
    // Simulate cursor displacement without moving the user's physical pointer.
    nestlone::g_widgets[0]->mouse.x-=40;nestlone::g_widgets[0]->mouse.y-=30;
    SendMessageW(firstWidget,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(70,45));
    check(widgets.widgets[0].rect.left==60&&widgets.widgets[0].rect.top==50,"first component drag moves in both axes");
    nestlone::g_widgets[0]->dragging=false;ReleaseCapture();
    const HWND lock=GetDlgItem(firstWidget,nestlone::kNoteLock);
    SendMessageW(firstWidget,WM_COMMAND,MAKEWPARAM(nestlone::kNoteLock,BN_CLICKED),reinterpret_cast<LPARAM>(lock));
    SendMessageW(firstWidget,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(30,15));
    RECT firstBounds{};GetWindowRect(firstWidget,&firstBounds);
    const LPARAM resizePoint=MAKELPARAM(firstBounds.right-2,firstBounds.bottom-2);
    check(widgets.widgets[0].locked&&!nestlone::g_widgets[0]->dragging&&
        !IsWindowEnabled(GetDlgItem(firstWidget,nestlone::kNoteColor))&&
        SendMessageW(firstWidget,WM_NCHITTEST,0,resizePoint)==HTCLIENT,
        "locking blocks move, resize and color changes");
    SendMessageW(firstWidget,WM_COMMAND,MAKEWPARAM(nestlone::kNoteLock,BN_CLICKED),reinterpret_cast<LPARAM>(lock));
    check(SendMessageW(firstWidget,WM_NCHITTEST,0,resizePoint)==HTBOTTOMRIGHT,
        "unlocked note exposes native resize handles");
    SetWindowPos(firstWidget,nullptr,0,0,320,210,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    SendMessageW(firstWidget,WM_EXITSIZEMOVE,0,0);
    check(widgets.widgets[0].rect.right-widgets.widgets[0].rect.left==320&&
        widgets.widgets[0].rect.bottom-widgets.widgets[0].rect.top==210,
        "resized dimensions are stored in the note model");
    nestlone::Layout::Widget note;note.id=L"note-test";note.type=L"note";note.rect={300,20,560,250};widgets.widgets.push_back(note);
    nestlone::SyncWidgets();check(nestlone::g_widgets.size()==2,"second widget does not replace first");
    RECT editor{};GetClientRect(nestlone::g_widgets[1]->edit,&editor);check(editor.right>200&&editor.bottom>150,"note editor is sized on first creation");
    check(GetParent(firstWidget)==GetParent(nestlone::g_widgets[1]->window),"components remain same-level desktop siblings");
    nestlone::g_visible=true;nestlone::SyncWidgets();check((GetWindowLongPtrW(firstWidget,GWL_STYLE)&WS_VISIBLE)!=0,"hidden widget can be shown again");
    const HWND firstEditor=nestlone::g_widgets[0]->edit;
    const LONG_PTR editStyle=GetWindowLongPtrW(firstEditor,GWL_STYLE);
    check(!(editStyle&(WS_HSCROLL|WS_VSCROLL|ES_AUTOHSCROLL)),
        "note editor wraps text without standard scroll bars");
    const auto narrowLines=SendMessageW(firstEditor,EM_GETLINECOUNT,0,0);
    SetWindowPos(firstWidget,nullptr,0,0,450,210,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    check(SendMessageW(firstEditor,EM_GETLINECOUNT,0,0)<narrowLines,
        "widening a note immediately reflows long text");
    ValidateRect(firstWidget,nullptr);ValidateRect(firstEditor,nullptr);
    SetWindowPos(firstWidget,nullptr,0,0,324,214,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    check(GetUpdateRect(firstWidget,nullptr,FALSE)&&GetUpdateRect(firstEditor,nullptr,FALSE),
        "resizing invalidates both the note surface and the editor");
    SendMessageW(firstWidget,WM_EXITSIZEMOVE,0,0);
    const auto scroll=nestlone::ReadNoteScroll(*nestlone::g_widgets[0]);
    check(scroll.last>0&&scroll.thumb.right-scroll.thumb.left==3,
        "overflowing notes use a three-pixel vertical thumb");
    nestlone::ScrollNoteTo(*nestlone::g_widgets[0],scroll,scroll.last);
    check(SendMessageW(firstEditor,EM_GETFIRSTVISIBLELINE,0,0)>0,
        "the vertical thumb can scroll the wrapped editor");
    check(nestlone::ReadNoteScroll(*nestlone::g_widgets[1]).last==0,
        "short notes do not show a scroll thumb");
    nestlone::UpdateSurfaceVisibility();
    check(GetWindow(background,GW_HWNDPREV)==firstWidget&&GetWindow(firstWidget,GW_HWNDPREV)==nestlone::g_widgets[1]->window,
        "idle notes are positioned together directly above the desktop canvas");
    const HWND closeNote=GetDlgItem(firstWidget,nestlone::kNoteClose);
    SendMessageW(firstWidget,WM_COMMAND,MAKEWPARAM(nestlone::kNoteClose,BN_CLICKED),reinterpret_cast<LPARAM>(closeNote));
    nestlone::UpdateSurfaceVisibility();
    check(widgets.widgets[0].closed&&!IsWindowVisible(firstWidget)&&widgets.widgets[0].text.size()==4500,
        "closing a note hides it and keeps its text");
    check(nestlone::CanvasShowNote(firstNote.id)&&!widgets.widgets[0].closed&&IsWindowVisible(firstWidget),
        "a closed note can be shown again by ID");
    check(nestlone::CanvasCreateNote(normal)&&widgets.widgets.size()==3&&nestlone::g_widgets.size()==3,
        "new note command creates a visible editor on the desktop surface");
    nestlone::ShowSettings(nestlone::g_instance,host,&widgets);
    const HWND settings=GetActiveWindow();
    const bool settingsOpen=db::ClassOf(settings)==L"nestlone-D.Settings";
    check(settingsOpen,"settings window opens with the notes page");
    if(settingsOpen) {
        check(!(GetWindowLongPtrW(settings,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)&&
            GetWindow(settings,GW_OWNER)==host,"settings uses a standard caption and stays owned by the tray window");
        SetActiveWindow(settings);
        SendMessageW(firstWidget,WM_CLOSE,0,0);
        SendMessageW(GetDlgItem(settings,2205),BM_CLICK,0,0);
        const HWND createNote=GetDlgItem(settings,2601);
        const HWND noteList=GetDlgItem(settings,2605);
        check(IsWindowVisible(createNote)&&ListView_GetItemCount(noteList)==3,
            "notes page exposes the new note button and includes closed notes");
        SetDlgItemTextW(settings,2606,L"便便");
        check(ListView_GetItemCount(noteList)==1&&nestlone::NoteText(GetDlgItem(settings,2607)).size()==4500,
            "note search finds text and shows a full preview");
        SendMessageW(GetDlgItem(settings,2604),BM_CLICK,0,0);
        check(!widgets.widgets[0].closed&&IsWindowVisible(firstWidget),
            "show on desktop reopens the selected note");
        SetActiveWindow(settings);
        SendMessageW(createNote,BM_CLICK,0,0);
        check(widgets.widgets.size()==4&&nestlone::g_widgets.size()==4&&ListView_GetItemCount(noteList)==4,
            "new note button creates a desktop note and refreshes the list");
        SendMessageW(settings,WM_SYSCOMMAND,SC_CLOSE,0);
        check(!IsWindow(settings)&&nestlone::g_widgets.size()==4,
            "system close closes settings while keeping desktop notes");
    }
    nestlone::CloseSettings();
    widgets.widgets.clear();nestlone::SyncWidgets();check(nestlone::g_widgets.empty()&&!IsWindow(firstWidget)&&nestlone::g_paintBuffers.empty(),"clear removes component HWNDs and cached buffers");
    DestroyWindow(normal);DestroyWindow(background);DestroyWindow(host);nestlone::g_background=nullptr;nestlone::g_host={};nestlone::g_layout=nullptr;
    Gdiplus::GdiplusShutdown(token);return failures?1:0;
}

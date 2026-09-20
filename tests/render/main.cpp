#include "../../src/DesktopCanvas.cpp"
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
    Gdiplus::GdiplusShutdown(token);return failures?1:0;
}

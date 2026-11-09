#include "DesktopCanvas.h"
#include "NoteStyle.h"
#include "DesktopSession.h"
#include "DesktopHost.h"
#include "log.h"
#include <windowsx.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <ole2.h>
#include <wrl/client.h>
#include <algorithm>
#include <memory>
#include <vector>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace nestlone {
namespace {
using Microsoft::WRL::ComPtr;
Layout* g_layout=nullptr;
HWND g_manager=nullptr,g_background=nullptr,g_edit=nullptr;
HINSTANCE g_instance=nullptr;
ULONG_PTR g_token=0;
HHOOK g_keyboardHook=nullptr;
HHOOK g_renameMouseHook=nullptr;
HWINEVENTHOOK g_foregroundHook=nullptr;
bool g_visible=true,g_drag=false,g_resizing=false,g_resizeFromLeft=false,g_tabPending=false,g_tabDetached=false,g_nativeHidden=false;
int g_tabInsert=-1;
bool g_interactivePaintQueued=false;
bool g_visualGeometryDirty=false;
int g_shellRefreshAttempts=0;
HWND g_dragPreview=nullptr;
HWND g_iconDragPreview=nullptr;
POINT g_iconPreviewOrigin{};
std::unordered_set<std::wstring> g_iconPreviewPaths;
std::wstring g_previewGroup,g_renderOnlyGroup;
void EndDragPreview();
HWND g_hiddenListview=nullptr;
bool g_dropRegistered=false;
db::HostInfo g_host;
DesktopSnapshot g_snapshot;
uint64_t g_pending=0;
std::vector<DesktopMove> g_moves,g_dragStart;
POINT g_mouseStart{};
RECT g_boxStart{};
std::wstring g_dragId,g_editId,g_status;
bool g_editGroupTitle=false;
std::wstring g_editItemPath,g_editHiddenExtension;
std::vector<std::pair<std::wstring,RECT>> g_boxDragStart;
HFONT g_editFont=nullptr;
struct Decoration {std::wstring id;HWND header=nullptr,gripLeft=nullptr,grip=nullptr;};
struct WidgetWindow {
    std::wstring id;
    HWND window=nullptr,edit=nullptr;
    HWND buttons[3]{};
    bool dragging=false,resizing=false,pendingSave=false,syncingText=false;
    int scrollGrab=-1,wheelRemainder=0;
    POINT mouse{};
    RECT start{};
};
std::vector<std::unique_ptr<Decoration>> g_decorations;
std::vector<std::unique_ptr<WidgetWindow>> g_widgets;
struct VisualIcon {
    std::wstring path,name;
    std::shared_ptr<Gdiplus::Bitmap> image;
    POINT position{};
    RECT hit{},label{},clip{};
    int size=32;
    bool selected=false,list=false;
};
std::vector<VisualIcon> g_visualIcons;
int g_iconDrag=-1; POINT g_iconMouseStart{};
std::vector<std::pair<std::wstring,POINT>> g_iconDragStart;
int g_focusIcon=-1; bool g_iconMoved=false; int g_boxButton=0;
bool g_marquee=false,g_marqueeMoved=false,g_marqueeCtrl=false,g_marqueeShift=false;
POINT g_marqueeStart{};
RECT g_marqueeRect{};
std::vector<std::wstring> g_selectionStart;
std::vector<std::wstring> g_pendingDelete;
struct PendingShellDrop {std::wstring path;ULONGLONG started=0;};
std::vector<PendingShellDrop> g_pendingShellDrops;
bool g_visualDirty=true;
void CancelIconGesture(bool releaseCapture=true);
void BeginItemRename(const std::wstring& path);
void RebuildVisualIcons();
void RefreshVisualGeometry();
void DrawVisualIcons(Gdiplus::Graphics&,bool iconPreview=false);
LRESULT CALLBACK KeyboardProc(int code,WPARAM wParam,LPARAM lParam);
LRESULT CALLBACK WidgetProc(HWND,UINT,WPARAM,LPARAM);
void UpdateSurfaceVisibility();
constexpr int kHeader=32;
int HeaderHeight(){return MulDiv(kHeader,static_cast<int>(g_host.listview?GetDpiForWindow(g_host.listview):96),96);}
const wchar_t* CanvasFontFamily() { return g_layout&&!g_layout->fontFamily.empty()?g_layout->fontFamily.c_str():L"Microsoft YaHei UI"; }
Box* FindBox(const std::wstring& id) {
    if(g_layout)for(auto& box:g_layout->boxes)if(box.id==id)return &box;
    return nullptr;
}
Layout::Widget* FindWidget(const std::wstring& id) {
    if(g_layout)for(auto& widget:g_layout->widgets)if(widget.id==id)return &widget;
    return nullptr;
}
WidgetWindow* FindWidgetWindow(HWND hwnd) {
    for(auto& widget:g_widgets)if(widget->window==hwnd)return widget.get();
    return nullptr;
}
Box* GroupRoot(Box& box) {
    if(box.groupId.empty())return &box;
    if(auto* root=FindBox(box.groupId))return root;
    box.groupId.clear();return &box;
}
const Box* GroupRoot(const Box& box) {
    if(box.groupId.empty())return &box;
    for(const auto& candidate:g_layout->boxes)if(candidate.id==box.groupId)return &candidate;
    return &box;
}
bool IsGroupRoot(const Box& box) { return box.groupId.empty() || !FindBox(box.groupId); }
bool IsActiveTab(const Box& box) {
    const Box* root=GroupRoot(box);
    return root->activeTabId.empty()?box.id==root->id:box.id==root->activeTabId;
}
Box* ActiveBox(Box& root) {
    if(root.activeTabId.empty())return &root;
    if(auto* active=FindBox(root.activeTabId))return active;
    root.activeTabId.clear();return &root;
}
std::vector<Box*> GroupTabs(Box& root) {
    std::vector<Box*> tabs;tabs.push_back(&root);
    for(auto& box:g_layout->boxes)if(box.groupId==root.id)tabs.push_back(&box);
    std::stable_sort(tabs.begin(),tabs.end(),[](const Box* a,const Box* b){return a->tabOrder<b->tabOrder;});
    return tabs;
}
void ReorderTab(Box& tab,int insertion) {
    auto tabs=GroupTabs(*GroupRoot(tab));
    auto from=std::find(tabs.begin(),tabs.end(),&tab);
    if(from==tabs.end())return;
    int old=static_cast<int>(from-tabs.begin());
    insertion=std::clamp(insertion,0,static_cast<int>(tabs.size()));
    tabs.erase(from);if(insertion>old)--insertion;
    tabs.insert(tabs.begin()+insertion,&tab);
    for(size_t i=0;i<tabs.size();++i)tabs[i]->tabOrder=static_cast<int>(i);
}
bool HasGroupTabs(const Box& root) {
    return g_layout&&std::any_of(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const Box& box){return box.groupId==root.id;});
}
void DetachTab(Box& tab) {
    Box* root=GroupRoot(tab);
    auto members=GroupTabs(*root);
    if(root==&tab&&members.size()>1) {
        members.erase(std::remove(members.begin(),members.end(),&tab),members.end());
        Box* successor=members.front();
        const auto active=root->activeTabId;
        successor->groupId.clear();successor->groupTitle=root->groupTitle.empty()?root->title:root->groupTitle;
        successor->collapsed=root->collapsed;
        successor->activeTabId=(active.empty()||active==tab.id)?successor->id:active;
        for(size_t i=1;i<members.size();++i)members[i]->groupId=successor->id;
    } else if(root!=&tab&&root->activeTabId==tab.id)root->activeTabId.clear();
    tab.groupId.clear();tab.activeTabId.clear();tab.collapsed=false;
    tab.tabOrder=0;
}
int BoxHeaderHeight(const Box& box) {
    const Box* root=GroupRoot(box);
    return HeaderHeight()*(HasGroupTabs(*root)?2:1);
}
const std::wstring& GroupTitle(const Box& root) {
    return root.groupTitle.empty()?root.title:root.groupTitle;
}
void SyncGroupRect(Box& box) {
    Box* root=GroupRoot(box);
    if(!HasGroupTabs(*root))return;
    const RECT rect=box.rect;
    for(auto& candidate:g_layout->boxes)if(GroupRoot(candidate)==root)candidate.rect=rect;
}
bool HasPath(const Box& box,const std::wstring& path) {
    return std::any_of(box.items.begin(),box.items.end(),[&](const auto& p){return _wcsicmp(p.c_str(),path.c_str())==0;});
}
const DesktopEntry* FindEntry(const DesktopSnapshot& snapshot,const std::wstring& path) {
    for(const auto& e:snapshot.entries)if(_wcsicmp(e.path.c_str(),path.c_str())==0)return &e;
    return nullptr;
}
int ClampDesktopX(int x) { return static_cast<int>(std::clamp<LONG>(static_cast<LONG>(x),0L,max(0L,static_cast<LONG>(GetSystemMetrics(SM_CXVIRTUALSCREEN))-max(1L,g_snapshot.spacing.x)))); }
int ClampDesktopY(int y) { return static_cast<int>(std::clamp<LONG>(static_cast<LONG>(y),0L,max(0L,static_cast<LONG>(GetSystemMetrics(SM_CYVIRTUALSCREEN))-max(1L,g_snapshot.spacing.y)))); }
POINT ClampDesktopPosition(POINT point) { return {ClampDesktopX(point.x),ClampDesktopY(point.y)}; }
LONG NearestGridSlot(LONG pixel,LONG step) {
    if(pixel>=0)return (pixel+step/2)/step;
    return -((-pixel+step/2)/step);
}
uint64_t GridKey(LONG column,LONG row) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(column))<<32)|static_cast<uint32_t>(row);
}
std::vector<std::pair<std::wstring,POINT>> SnapDesktopDrop(const std::vector<std::pair<std::wstring,POINT>>& dragged) {
    const LONG sx=max(1L,g_snapshot.spacing.x),sy=max(1L,g_snapshot.spacing.y);
    const LONG padding=max(0L,(sx-max(16,g_snapshot.iconSize))/2);
    const LONG maxColumn=max(0L,(GetSystemMetrics(SM_CXVIRTUALSCREEN)-sx)/sx);
    const LONG maxRow=max(0L,(GetSystemMetrics(SM_CYVIRTUALSCREEN)-sy)/sy);
    std::unordered_set<uint64_t> occupied;
    for(const auto& icon:g_visualIcons) {
        const bool moving=std::any_of(dragged.begin(),dragged.end(),[&](const auto& item){return _wcsicmp(item.first.c_str(),icon.path.c_str())==0;});
        if(!moving)occupied.insert(GridKey(NearestGridSlot(icon.position.x-padding,sx),NearestGridSlot(icon.position.y,sy)));
    }
    std::vector<std::pair<std::wstring,POINT>> result;result.reserve(dragged.size());
    for(const auto& item:dragged) {
        const LONG preferredColumn=std::clamp(NearestGridSlot(item.second.x-padding,sx),0L,maxColumn);
        const LONG preferredRow=std::clamp(NearestGridSlot(item.second.y,sy),0L,maxRow);
        LONG column=preferredColumn,row=preferredRow;
        bool found=false;
        for(LONG radius=0;radius<=maxColumn+maxRow&&!found;++radius) {
            for(LONG y=max(0L,preferredRow-radius);y<=min(maxRow,preferredRow+radius)&&!found;++y) {
                for(LONG x=max(0L,preferredColumn-radius);x<=min(maxColumn,preferredColumn+radius);++x) {
                    if(abs(x-preferredColumn)+abs(y-preferredRow)!=radius)continue;
                    if(occupied.find(GridKey(x,y))==occupied.end()){column=x;row=y;found=true;break;}
                }
            }
        }
        occupied.insert(GridKey(column,row));result.push_back({item.first,{column*sx+padding,row*sy}});
    }
    return result;
}
RECT DisplayRect(const Box& box) {RECT r=box.rect;if(box.collapsed)r.bottom=r.top+BoxHeaderHeight(box);return r;}
int ContainingBox(const DesktopEntry& entry) {
    POINT center{entry.position.x+g_snapshot.iconSize/2,entry.position.y+g_snapshot.iconSize/2};
    for(int i=static_cast<int>(g_layout->boxes.size())-1;i>=0;--i) {
        auto& b=g_layout->boxes[i];RECT r=b.rect;r.top+=BoxHeaderHeight(b);
        if(!b.collapsed && PtInRect(&r,center))return i;
    }
    return -1;
}
int CornerDiameter(const Layout& layout) {
    const int dpi=static_cast<int>(g_host.listview?GetDpiForWindow(g_host.listview):96);
    return MulDiv(std::clamp(layout.cornerRadius,0,48),dpi,96);
}
void Rounded(Gdiplus::Graphics& g,const RECT& r,Gdiplus::Color color,int diameter=12) {
    if(r.right<=r.left||r.bottom<=r.top)return;
    if(diameter<=0){Gdiplus::SolidBrush brush(color);g.FillRectangle(&brush,static_cast<int>(r.left),static_cast<int>(r.top),static_cast<int>(r.right-r.left),static_cast<int>(r.bottom-r.top));return;}
    Gdiplus::GraphicsPath path;
    const float d=static_cast<float>(min(static_cast<LONG>(diameter),min(r.right-r.left,r.bottom-r.top))),x=static_cast<float>(r.left),y=static_cast<float>(r.top);
    const float w=static_cast<float>(r.right-r.left),h=static_cast<float>(r.bottom-r.top);
    path.AddArc(x,y,d,d,180,90);path.AddArc(x+w-d,y,d,d,270,90);
    path.AddArc(x+w-d,y+h-d,d,d,0,90);path.AddArc(x,y+h-d,d,d,90,90);path.CloseFigure();
    Gdiplus::SolidBrush brush(color);g.FillPath(&brush,&path);
}
Gdiplus::Color Background(COLORREF color,int opacity) {
    return Gdiplus::Color(static_cast<BYTE>(std::clamp(opacity,0,100)*255/100),GetRValue(color),GetGValue(color),GetBValue(color));
}
struct PaintBuffer {
    HDC dc=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;
    void* pixels=nullptr;int width=0,height=0;std::wstring decorationKey;
    ~PaintBuffer(){Reset();}
    void Reset(){if(dc&&previous)SelectObject(dc,previous);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);dc=nullptr;bitmap=nullptr;previous=nullptr;pixels=nullptr;width=height=0;decorationKey.clear();}
    bool Ensure(int w,int h){
        if(bitmap&&width==w&&height==h)return true;
        Reset();dc=CreateCompatibleDC(nullptr);if(!dc)return false;
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
        bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!bitmap){Reset();return false;}previous=SelectObject(dc,bitmap);width=w;height=h;return true;
    }
};
std::unordered_map<HWND,std::unique_ptr<PaintBuffer>> g_paintBuffers;
// Background rendering intentionally knows nothing about desktop items.
bool RenderPixels(void* pixels,int width,int height,const Layout& layout) {
    Gdiplus::Bitmap bitmap(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
    Gdiplus::Graphics g(&bitmap);g.Clear(Gdiplus::Color(0,0,0,0));g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    for(const auto& box:layout.boxes)if(IsGroupRoot(box)&&(g_previewGroup.empty()||box.id!=g_previewGroup))Rounded(g,DisplayRect(box),Background(box.color,layout.opacity),CornerDiameter(layout));
    g.Flush();return g.GetLastStatus()==Gdiplus::Ok;
}
void Label(Gdiplus::Graphics& g,const std::wstring& value,RECT r) {
    Gdiplus::FontFamily family(CanvasFontFamily());Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::GraphicsPath text;
    text.AddString(value.c_str(),-1,&family,Gdiplus::FontStyleBold,static_cast<float>(HeaderHeight()*0.4),
        Gdiplus::RectF(static_cast<float>(r.left),static_cast<float>(r.top),static_cast<float>(r.right-r.left),static_cast<float>(r.bottom-r.top)),&format);
    Gdiplus::Pen outline(Gdiplus::Color(230,22,38,49),1.4f);Gdiplus::SolidBrush ink(Gdiplus::Color(255,255,255,255));
    g.DrawPath(&outline,&text);g.FillPath(&ink,&text);
}
bool PaintWindow(HWND hwnd,Box* box=nullptr,bool grip=false,bool leftGrip=false) {
    RECT r{};if(!GetClientRect(hwnd,&r)||r.right<=0||r.bottom<=0)return false;
    auto& stored=g_paintBuffers[hwnd];if(!stored)stored=std::make_unique<PaintBuffer>();
    auto& buffer=*stored;if(!buffer.Ensure(r.right,r.bottom))return false;
    void* pixels=buffer.pixels;HDC dc=buffer.dc;
    std::wstring key;
    if(box){
        key=std::to_wstring(g_layout->opacity)+L":"+std::to_wstring(box->selected)+L":"+std::to_wstring(box->collapsed)+L":"+std::to_wstring(grip)+L":"+std::to_wstring(HeaderHeight());
        for(auto* tab:GroupTabs(*box))key+=L"|"+std::to_wstring(tab->title.size())+L":"+tab->title+L":"+std::to_wstring(IsActiveTab(*tab));
        key+=L"|"+GroupTitle(*box);
        key+=L"|"+std::to_wstring(g_tabPending?g_tabInsert:-1);
        for(auto* tab:GroupTabs(*box))key+=L"|"+tab->id;
        if(buffer.decorationKey==key)return true;
    }
    if(!box) {
        RenderPixels(pixels,r.right,r.bottom,*g_layout);
        Gdiplus::Bitmap surface(r.right,r.bottom,r.right*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
        Gdiplus::Graphics g(&surface);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);DrawVisualIcons(g);g.Flush();
        auto* hitPixels=static_cast<DWORD*>(pixels);for(int i=0;i<r.right*r.bottom;++i)if((hitPixels[i]>>24)==0)hitPixels[i]=0x01000000u;
    }
    else {
        Gdiplus::Bitmap surface(r.right,r.bottom,r.right*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
        Gdiplus::Graphics g(&surface);g.Clear(Gdiplus::Color(0,0,0,0));g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        // The canvas already paints the box background below this window.
        // A second opaque colour layer would make the header much darker.
        Rounded(g,r,Gdiplus::Color(7,255,255,255),CornerDiameter(*g_layout));
        // Selection and both bottom resize targets are intentionally invisible.
        // They are interaction-only overlays, so they do not introduce white
        // outlines or corner glyphs into the desktop composition.
        if(grip) { (void)leftGrip; }
        else {
            const int h=HeaderHeight(),headerHeight=BoxHeaderHeight(*box);
            auto tabs=GroupTabs(*box);const int contentRight=r.right-3*h;
            if(tabs.size()==1) Label(g,box->title,{h,0,contentRight,h});
            else {
                Label(g,GroupTitle(*box),{h,0,contentRight,h});
                for(size_t i=0;i<tabs.size();++i) {
                    const int left=static_cast<int>(i*r.right/tabs.size());
                    const int right=static_cast<int>((i+1)*r.right/tabs.size());
                    Label(g,tabs[i]->title,{left,h,right,headerHeight});
                    if(IsActiveTab(*tabs[i])) {Gdiplus::Pen line(Gdiplus::Color(220,255,255,255),1.0f);g.DrawLine(&line,left+8,headerHeight-3,right-8,headerHeight-3);}
                }
                if(g_tabPending&&g_tabInsert>=0&&FindBox(g_dragId)&&GroupRoot(*FindBox(g_dragId))==box) {
                    int x=std::clamp(g_tabInsert*static_cast<int>(r.right)/static_cast<int>(tabs.size()),2,static_cast<int>(r.right)-2);
                    Gdiplus::Pen insertion(Gdiplus::Color(255,119,220,255),2.0f);
                    g.DrawLine(&insertion,x,h+4,x,headerHeight-4);
                }
            }
            Label(g,L"▦",{r.right-3*h,0,r.right-2*h,h});
            Label(g,box->collapsed?L"+":L"−",{r.right-2*h,0,r.right-h,h});
            Label(g,L"×",{r.right-h,0,r.right,h});
        }
        g.Flush();
    }
    POINT source{};SIZE size{r.right,r.bottom};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    bool ok=UpdateLayeredWindow(hwnd,nullptr,nullptr,&size,dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
    if(ok&&box)buffer.decorationKey=key;
    return ok;
}
POINT ParentPoint(POINT point) { return point; }
HWND SurfaceParent() { return g_background ? g_background : g_host.defviewParent; }
DWORD SurfaceWindowStyle() { return WS_CHILD; }
HWND SurfaceControlInsertAfter() { return HWND_TOP; }
// The application owns one top-level surface and all its decorations are
// children. Keep the surface immediately above the desktop, below normal apps.
// Never infer desktop visibility from a foreground process ID (Explorer also
// owns ordinary folder windows), or hide the entire desktop on focus changes.
bool SurfacePresentationAllowed() {return g_visible;}
void UpdateSurfaceVisibility() {
    if(!IsWindow(g_background))return;
    if(!g_visible) {
        for(const auto& widget:g_widgets)ShowWindow(widget->window,SW_HIDE);
        ShowWindow(g_background,SW_HIDE);
        return;
    }
    HWND desktop=GetAncestor(g_host.defview,GA_ROOT);
    if(!IsWindow(desktop))return;
    HWND preceding=GetWindow(desktop,GW_HWNDPREV);
    while(preceding&&(preceding==g_background||FindWidgetWindow(preceding)))
        preceding=GetWindow(preceding,GW_HWNDPREV);
    // Inserting after a topmost window can itself promote our window.  Keep
    // the canvas in the normal window band even when the desktop is foremost.
    if(preceding&&(GetWindowLongPtrW(preceding,GWL_EXSTYLE)&WS_EX_TOPMOST))preceding=nullptr;
    const HWND insertAfter=preceding?preceding:HWND_TOP;
    constexpr UINT flags=SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW;
    SetWindowPos(g_background,insertAfter,0,0,0,0,flags);
    const HWND foreground=GetForegroundWindow();
    for(const auto& widget:g_widgets) {
        const auto* model=FindWidget(widget->id);
        if(!model||model->closed) {
            if(IsWindowVisible(widget->window))ShowWindow(widget->window,SW_HIDE);
            continue;
        }
        // An active note stays in front while the user types. Other notes
        // sit immediately above the canvas, below ordinary app windows.
        if(widget->window!=foreground)SetWindowPos(widget->window,insertAfter,0,0,0,0,flags);
    }
}
void CALLBACK ForegroundProc(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD) {
    if(g_manager)PostMessageW(g_manager,WM_APP+21,0,0);
}
std::wstring NoteText(HWND edit) {
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(edit))+1,L'\0');
    text.resize(GetWindowTextW(edit,text.data(),static_cast<int>(text.size())));
    return text;
}
constexpr int kNoteColor=2,kNoteLock=3,kNoteClose=4;
constexpr int kNoteButtonWidth=27,kNoteButtonInset=7;
RECT NoteScrollTrack(HWND window) {
    RECT r{};GetClientRect(window,&r);
    return {r.right-note::padding,note::headerHeight+note::padding,r.right-6,r.bottom-note::padding};
}
struct NoteScroll {
    RECT track{},thumb{};
    int first=0,last=0,page=1;
};
NoteScroll ReadNoteScroll(const WidgetWindow& state) {
    NoteScroll scroll;
    scroll.track=NoteScrollTrack(state.window);
    const LONG height=scroll.track.bottom-scroll.track.top;
    if(!state.edit||height<2)return scroll;
    RECT format{};
    SendMessageW(state.edit,EM_GETRECT,0,reinterpret_cast<LPARAM>(&format));
    TEXTMETRICW metric{};
    if(HDC dc=GetDC(state.edit)) {
        const auto font=reinterpret_cast<HFONT>(SendMessageW(state.edit,WM_GETFONT,0,0));
        const auto old=font?SelectObject(dc,font):nullptr;
        GetTextMetricsW(dc,&metric);
        if(old)SelectObject(dc,old);
        ReleaseDC(state.edit,dc);
    }
    scroll.page=max(1L,(format.bottom-format.top)/max(1L,metric.tmHeight));
    const int lines=static_cast<int>(SendMessageW(state.edit,EM_GETLINECOUNT,0,0));
    scroll.last=max(0,lines-scroll.page);
    if(!scroll.last)return scroll;
    scroll.first=static_cast<int>(SendMessageW(state.edit,EM_GETFIRSTVISIBLELINE,0,0));
    const LONG size=std::clamp(static_cast<LONG>(MulDiv(height,scroll.page,lines)),min(14L,height-1),height-1);
    const LONG y=scroll.track.top+MulDiv(height-size,min(scroll.first,scroll.last),scroll.last);
    scroll.thumb={scroll.track.left+1,y,scroll.track.left+4,y+size};
    return scroll;
}
void InvalidateNoteScroll(HWND window) {
    const RECT track=NoteScrollTrack(window);
    InvalidateRect(window,&track,FALSE);
}
void ScrollNoteTo(const WidgetWindow& state,const NoteScroll& scroll,int line) {
    const int delta=std::clamp(line,0,scroll.last)-scroll.first;
    if(delta)SendMessageW(state.edit,EM_LINESCROLL,0,delta);
    InvalidateNoteScroll(state.window);
}
void ScrollNoteWheel(WidgetWindow& state,WPARAM wheel) {
    const auto scroll=ReadNoteScroll(state);
    if(!scroll.last)return;
    UINT lines=3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
    if(!lines)return;
    state.wheelRemainder+=GET_WHEEL_DELTA_WPARAM(wheel);
    const int steps=state.wheelRemainder/WHEEL_DELTA;
    state.wheelRemainder%=WHEEL_DELTA;
    const int amount=lines==WHEEL_PAGESCROLL?scroll.page:static_cast<int>(min(lines,static_cast<UINT>(scroll.page)));
    if(steps)ScrollNoteTo(state,scroll,scroll.first-steps*amount);
}
LRESULT CALLBACK NoteEditProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data) {
    auto& state=*reinterpret_cast<WidgetWindow*>(data);
    if(message==WM_MOUSEWHEEL) {
        ScrollNoteWheel(state,wp);
        return 0;
    }
    if(message==WM_NCDESTROY)RemoveWindowSubclass(hwnd,NoteEditProc,id);
    const LRESULT result=DefSubclassProc(hwnd,message,wp,lp);
    if(message==WM_PAINT)InvalidateNoteScroll(state.window);
    return result;
}
void LayoutNoteControls(HWND window,const WidgetWindow& state) {
    RECT client{};GetClientRect(window,&client);
    const int top=note::headerHeight+note::padding;
    constexpr UINT flags=SWP_NOZORDER|SWP_NOACTIVATE|SWP_NOREDRAW;
    if(state.edit) {
        SetWindowPos(state.edit,nullptr,note::padding,top,max(1L,client.right-2*note::padding),
            max(1L,client.bottom-top-note::padding),flags);
        RECT format{};GetClientRect(state.edit,&format);
        // Reflow the multiline editor at the new width. The gutter is
        // outside the editor and the text never needs horizontal scrolling.
        SendMessageW(state.edit,EM_SETRECTNP,0,reinterpret_cast<LPARAM>(&format));
    }
    for(int i=0;i<3;++i)if(state.buttons[i])
        SetWindowPos(state.buttons[i],nullptr,client.right-kNoteButtonInset-(3-i)*kNoteButtonWidth,6,
            kNoteButtonWidth,note::headerHeight-10,flags);
}
void RefreshNoteControls(HWND window,const WidgetWindow& state,const Layout::Widget& widget) {
    EnableWindow(state.buttons[0],!widget.locked);
    SetWindowTextW(state.buttons[1],widget.locked?L"解锁便签":L"锁定便签");
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
POINT NoteScreenOrigin(const RECT& rect) {
    return {rect.left+GetSystemMetrics(SM_XVIRTUALSCREEN),rect.top+GetSystemMetrics(SM_YVIRTUALSCREEN)};
}
void SyncWidgets() {
    if(!g_layout||!g_host.defviewParent)return;
    for(auto it=g_widgets.begin();it!=g_widgets.end();) {
        if(!FindWidget((*it)->id)){if(IsWindow((*it)->window))DestroyWindow((*it)->window);g_paintBuffers.erase((*it)->window);it=g_widgets.erase(it);}else ++it;
    }
    for(auto& model:g_layout->widgets) {
        if(model.closed)continue;
        auto found=std::find_if(g_widgets.begin(),g_widgets.end(),[&](const auto& item){return item->id==model.id;});
        if(found==g_widgets.end()) {
            auto widget=std::make_unique<WidgetWindow>();widget->id=model.id;
            const POINT p=NoteScreenOrigin(model.rect);
            widget->window=CreateWindowExW(WS_EX_TOOLWINDOW,L"nestlone-D.Widget",L"",WS_POPUP|WS_THICKFRAME|WS_CLIPCHILDREN,
                p.x,p.y,model.rect.right-model.rect.left,model.rect.bottom-model.rect.top,nullptr,nullptr,g_instance,widget.get());
            if(!widget->window)continue;
            widget->edit=CreateWindowExW(0,L"EDIT",model.text.c_str(),
                WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL,0,0,1,1,
                widget->window,reinterpret_cast<HMENU>(1),g_instance,nullptr);
            if(!widget->edit) {
                DestroyWindow(widget->window);
                continue;
            }
            SetWindowSubclass(widget->edit,NoteEditProc,1,reinterpret_cast<DWORD_PTR>(widget.get()));
            SendMessageW(widget->edit,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
            constexpr const wchar_t* labels[]={L"便签颜色",L"锁定便签",L"关闭便签"};
            for(int i=0;i<3;++i) {
                widget->buttons[i]=CreateWindowW(L"BUTTON",labels[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,
                    0,0,1,1,widget->window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kNoteColor+i)),g_instance,nullptr);
            }
            LayoutNoteControls(widget->window,*widget);
            RefreshNoteControls(widget->window,*widget,model);
            g_widgets.push_back(std::move(widget));
        } else {
            auto& widget=**found;
            if(widget.edit&&NoteText(widget.edit)!=model.text) {
                KillTimer(widget.window,1);
                widget.pendingSave=false;
                widget.syncingText=true;
                SetWindowTextW(widget.edit,model.text.c_str());
                widget.syncingText=false;
            }
            const POINT p=NoteScreenOrigin(model.rect);
            SetWindowPos(widget.window,nullptr,p.x,p.y,model.rect.right-model.rect.left,model.rect.bottom-model.rect.top,
                SWP_NOACTIVATE|SWP_NOZORDER);
            RefreshNoteControls(widget.window,widget,model);
        }
    }
    UpdateSurfaceVisibility();
}
RECT ConstrainBoxToWorkArea(RECT rect,RECT work,int headerHeight) {
    const LONG width=rect.right-rect.left,height=rect.bottom-rect.top;
    // Only constrain the title strip. The content may extend below the work
    // area, even when the whole box would fit at another position.
    const LONG x=std::clamp(rect.left,work.left,max(work.left,work.right-width));
    const LONG y=std::clamp(rect.top,work.top,max(work.top,work.bottom-min(height,static_cast<LONG>(headerHeight))));
    OffsetRect(&rect,x-rect.left,y-rect.top);return rect;
}
RECT KeepBoxOnScreen(RECT rect,int headerHeight) {
    const LONG vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN);
    RECT screenRect=rect;screenRect.bottom=min(screenRect.bottom,screenRect.top+headerHeight);OffsetRect(&screenRect,vx,vy);
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if(!GetMonitorInfoW(MonitorFromRect(&screenRect,MONITOR_DEFAULTTONEAREST),&monitor))return rect;
    RECT work=monitor.rcWork;OffsetRect(&work,-vx,-vy);
    return ConstrainBoxToWorkArea(rect,work,headerHeight);
}
void RecoverBoxPositions() {
    for(auto& box:g_layout->boxes)if(IsGroupRoot(box)) {
        box.rect=KeepBoxOnScreen(box.rect,BoxHeaderHeight(box));SyncGroupRect(box);
    }
}
void RefreshVisualGeometry();
void PaintAll() {
    if(!g_layout)return;
    if(g_visualDirty){RebuildVisualIcons();g_visualDirty=false;}
    else if(g_visualGeometryDirty) {
        RefreshVisualGeometry();g_visualGeometryDirty=false;
        // Refresh can discover a newly collapsed/inactive owner and request a
        // complete visual rebuild instead of leaving stale icons on screen.
        if(g_visualDirty){RebuildVisualIcons();g_visualDirty=false;}
    }
    if(g_background&&!g_dragPreview)PaintWindow(g_background);
    for(auto& d:g_decorations)if(auto* box=FindBox(d->id)) {
        POINT top=ParentPoint({box->rect.left,box->rect.top});int h=BoxHeaderHeight(*box);
        SetWindowPos(d->header,nullptr,top.x,top.y,box->rect.right-box->rect.left,h,SWP_NOACTIVATE|SWP_NOZORDER);
        PaintWindow(d->header,box);
        POINT corner=ParentPoint({box->rect.right-18,box->rect.bottom-18});
        SetWindowPos(d->grip,nullptr,corner.x,corner.y,18,18,SWP_NOACTIVATE|SWP_NOZORDER);
        PaintWindow(d->grip,box,true);
        corner=ParentPoint({box->rect.left,box->rect.bottom-18});
        SetWindowPos(d->gripLeft,nullptr,corner.x,corner.y,18,18,SWP_NOACTIVATE|SWP_NOZORDER);
        PaintWindow(d->gripLeft,box,true,true);
        const bool show=SurfacePresentationAllowed();
        ShowWindow(d->header,show?SW_SHOWNOACTIVATE:SW_HIDE);
        ShowWindow(d->grip,show&&!box->collapsed?SW_SHOWNOACTIVATE:SW_HIDE);
        ShowWindow(d->gripLeft,show&&!box->collapsed?SW_SHOWNOACTIVATE:SW_HIDE);
    }
}
// Mouse messages can arrive much faster than the compositor can redraw the
// full layered desktop.  Keep only the latest geometry and present it at a
// display-friendly cadence; mouse-up always flushes immediately.
constexpr UINT_PTR kInteractivePaintTimer=2;
constexpr UINT_PTR kShellRefreshTimer=3;
constexpr UINT_PTR kLayoutSaveTimer=5;
bool g_layoutSavePending=false;
bool g_forceShellRepaint=false;

void FinishQueuedLayoutSave(bool save) {
    if(!g_layoutSavePending)return;
    g_layoutSavePending=false;
    if(g_manager)KillTimer(g_manager,kLayoutSaveTimer);
    if(save&&g_layout&&!SaveLayout(*g_layout))db::LogF("[canvas] could not save desktop layout");
}
void QueueLayoutSave() {
    g_layoutSavePending=true;
    if(!g_manager||!SetTimer(g_manager,kLayoutSaveTimer,120,nullptr))FinishQueuedLayoutSave(true);
}
void QueueInteractivePaint() {
    if(!g_manager||g_interactivePaintQueued)return;
    g_interactivePaintQueued=true;SetTimer(g_manager,kInteractivePaintTimer,16,nullptr);
}
enum class DesktopVisualChange {None,Geometry,Items};
DesktopVisualChange CompareDesktopVisuals(const DesktopSnapshot& before,const DesktopSnapshot& after) {
    if(!before.readable||before.listview!=after.listview||before.process!=after.process||
       before.entries.size()!=after.entries.size())return DesktopVisualChange::Items;
    bool geometryChanged=before.iconSize!=after.iconSize||before.iconMode!=after.iconMode||
        before.spacing.x!=after.spacing.x||before.spacing.y!=after.spacing.y;
    for(size_t i=0;i<after.entries.size();++i) {
        const auto& old=before.entries[i];
        const auto& entry=after.entries[i];
        if(old.path!=entry.path||old.displayName!=entry.displayName)return DesktopVisualChange::Items;
        geometryChanged|=old.position.x!=entry.position.x||old.position.y!=entry.position.y;
    }
    return geometryChanged?DesktopVisualChange::Geometry:DesktopVisualChange::None;
}
void FlushInteractivePaint() {
    if(g_interactivePaintQueued&&g_manager)KillTimer(g_manager,kInteractivePaintTimer);
    g_interactivePaintQueued=false;PaintAll();
}
void QueueMoves(const std::vector<DesktopMove>& moves) {
    for(const auto& move:moves) {
        auto clamped=move;clamped.position=ClampDesktopPosition(clamped.position);
        auto found=std::find_if(g_moves.begin(),g_moves.end(),[&](const auto& m){return _wcsicmp(m.path.c_str(),clamped.path.c_str())==0;});
        if(found==g_moves.end())g_moves.push_back(clamped);else *found=clamped;
    }
    g_pending=QueueDesktopMoves(g_moves);
}
// Reserve complete native cells (including labels), plus a separate footer for
// the resize handle. Grid origin belongs to the box, not Explorer's screen grid.
LONG BoxGridMinimumWidth(const DesktopSnapshot& snapshot) {
    const int dpi=static_cast<int>(g_host.listview?GetDpiForWindow(g_host.listview):96);
    // The desktop's spacing is intentionally generous. Box grids use a
    // denser, Explorer-like folder layout while keeping room for titles.
    return max(static_cast<LONG>(snapshot.iconSize+20),MulDiv(72,dpi,96));
}
int BoxGridColumns(const Box& box,const DesktopSnapshot& snapshot) {
    return max(1L,(box.rect.right-box.rect.left-16)/max(1L,BoxGridMinimumWidth(snapshot)));
}
LONG BoxGridCellWidth(const Box& box,const DesktopSnapshot& snapshot) {
    return max(1L,(box.rect.right-box.rect.left-16)/BoxGridColumns(box,snapshot));
}
LONG BoxGridLeft(const Box& box) { return box.rect.left+8; }
LONG BoxGridCellHeight(const DesktopSnapshot& snapshot) { return max(static_cast<LONG>(snapshot.iconSize+48),snapshot.spacing.y); }
RECT FitGrid(const Box& box,const DesktopSnapshot& snapshot) {
    RECT r=box.rect;
    if(!box.iconView){
        const LONG rowHeight=max(30,HeaderHeight());
        r.bottom=max(r.bottom,r.top+BoxHeaderHeight(box)+16+static_cast<LONG>(box.items.size())*rowHeight);
        return r;
    }
    const LONG sy=BoxGridCellHeight(snapshot);
    r.right=max(r.right,r.left+BoxGridMinimumWidth(snapshot)+16);
    Box fitted=box;fitted.rect=r;
    const LONG columns=BoxGridColumns(fitted,snapshot);
    LONG count=0;for(const auto& path:box.items)if(FindEntry(snapshot,path))++count;
    const LONG rows=max(1L,(count+columns-1)/columns);
    r.bottom=max(r.bottom,r.top+BoxHeaderHeight(box)+12+rows*sy+24);
    return r;
}
std::vector<DesktopMove> GridMoves(const Box& box,const DesktopSnapshot& snapshot) {
    if(!box.iconView)return {};
    const LONG sx=BoxGridCellWidth(box,snapshot),sy=BoxGridCellHeight(snapshot);
    const LONG columns=BoxGridColumns(box,snapshot),left=BoxGridLeft(box);
    std::vector<DesktopMove> moves;int slot=0;
    for(const auto& path:box.items)if(FindEntry(snapshot,path)) {
        LONG x=left+(slot%columns)*sx;
        LONG y=box.rect.top+BoxHeaderHeight(box)+12+(slot/columns)*sy;
        moves.push_back({path,{x+max(0L,(sx-snapshot.iconSize)/2),y}});++slot;
    }
    return moves;
}
std::shared_ptr<Gdiplus::Bitmap> DecodeIconWithMasks(HICON icon) {
    // Do not let GDI/GDI+ choose the HICON conversion.  Read the colour DIB
    // and the icon's AND mask ourselves, then write a standard premultiplied
    // 32-bit bitmap for UpdateLayeredWindow.
    ICONINFO info{};
    if(!GetIconInfo(icon,&info)||!info.hbmColor||!info.hbmMask) {
        if(info.hbmColor)DeleteObject(info.hbmColor);
        if(info.hbmMask)DeleteObject(info.hbmMask);
        return {};
    }
    BITMAP color{},mask{};
    if(!GetObjectW(info.hbmColor,sizeof(color),&color)||!GetObjectW(info.hbmMask,sizeof(mask),&mask)) {
        DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);return {};
    }
    const int width=color.bmWidth,height=abs(color.bmHeight);
    if(width<=0||height<=0) {DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);return {};}
    std::vector<DWORD> source(static_cast<size_t>(width)*height);
    BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bmi.bmiHeader.biWidth=width;bmi.bmiHeader.biHeight=-height;bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
    HDC screen=GetDC(nullptr);
    if(!screen) {DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);return {};}
    const int lines=GetDIBits(screen,info.hbmColor,0,height,source.data(),&bmi,DIB_RGB_COLORS);
    if(lines!=height) {ReleaseDC(nullptr,screen);DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);return {};}
    HDC maskDc=CreateCompatibleDC(screen);HGDIOBJ oldMask=SelectObject(maskDc,info.hbmMask);
    const int andHeight=mask.bmHeight;
    bool hasZero=false,hasFull=false,hasPartial=false;
    for(DWORD pixel:source) {const BYTE alpha=static_cast<BYTE>(pixel>>24);hasZero|=alpha==0;hasFull|=alpha==255;hasPartial|=alpha!=0&&alpha!=255;}
    const bool sourceHasAlpha=hasPartial||(hasZero&&hasFull);
    auto result=std::make_shared<Gdiplus::Bitmap>(width,height,PixelFormat32bppPARGB);
    Gdiplus::Rect area(0,0,width,height);Gdiplus::BitmapData output{};
    const bool locked=result->GetLastStatus()==Gdiplus::Ok&&result->LockBits(&area,Gdiplus::ImageLockModeWrite,PixelFormat32bppPARGB,&output)==Gdiplus::Ok;
    if(locked)for(int y=0;y<height;++y) {
        auto* row=reinterpret_cast<DWORD*>(static_cast<BYTE*>(output.Scan0)+y*output.Stride);
        const int my=min(andHeight-1,y*andHeight/max(1,height));
        for(int x=0;x<width;++x) {
            const DWORD pixel=source[static_cast<size_t>(y)*width+x];
            BYTE alpha=sourceHasAlpha?static_cast<BYTE>(pixel>>24):255;
            if(GetPixel(maskDc,min(mask.bmWidth-1,x*mask.bmWidth/max(1,width)),my)!=RGB(0,0,0))alpha=0;
            const BYTE blue=static_cast<BYTE>(pixel),green=static_cast<BYTE>(pixel>>8),red=static_cast<BYTE>(pixel>>16);
            const DWORD premultiplied=alpha==255?(0xff000000u|(pixel&0x00ffffffu)):alpha?((alpha<<24)|((red*alpha+127)/255<<16)|((green*alpha+127)/255<<8)|((blue*alpha+127)/255)):0;
            row[x]=premultiplied;
        }
    }
    if(locked)result->UnlockBits(&output);
    SelectObject(maskDc,oldMask);DeleteDC(maskDc);ReleaseDC(nullptr,screen);DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);
    return locked?result:std::shared_ptr<Gdiplus::Bitmap>();
}
std::shared_ptr<Gdiplus::Bitmap> DecodeIconImage(const std::wstring& path,int size) {
    SHFILEINFOW info{};
    // A desktop item may be a Shell namespace object (Recycle Bin, This PC,
    // control-panel extensions) rather than a filesystem path.  Resolve its
    // parsing name to a PIDL first; SHGFI_PIDL preserves that distinction.
    PIDLIST_ABSOLUTE pidl=nullptr;
    bool gotIcon=false;
    if(SUCCEEDED(SHParseDisplayName(path.c_str(),nullptr,&pidl,0,nullptr))&&pidl) {
        gotIcon=SHGetFileInfoW(reinterpret_cast<LPCWSTR>(pidl),0,&info,sizeof(info),SHGFI_PIDL|SHGFI_ICON|SHGFI_LARGEICON)!=0&&info.hIcon;
        CoTaskMemFree(pidl);
    }
    if(!gotIcon && (!SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON)||!info.hIcon))return {};
    HICON icon=info.hIcon;
    auto decoded=DecodeIconWithMasks(icon);
    if(!decoded) {
        auto source=std::make_unique<Gdiplus::Bitmap>(icon);
        if(source->GetLastStatus()==Gdiplus::Ok)decoded.reset(source->Clone(0,0,source->GetWidth(),source->GetHeight(),PixelFormat32bppPARGB));
    }
    DestroyIcon(icon);
    if(!decoded||decoded->GetLastStatus()!=Gdiplus::Ok)return {};
    if(static_cast<int>(decoded->GetWidth())==size&&static_cast<int>(decoded->GetHeight())==size)return decoded;
    auto result=std::make_shared<Gdiplus::Bitmap>(size,size,PixelFormat32bppPARGB);
    if(result->GetLastStatus()!=Gdiplus::Ok)return {};
    Gdiplus::Graphics graphics(result.get());
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(decoded.get(),0,0,size,size);
    return graphics.GetLastStatus()==Gdiplus::Ok?result:std::shared_ptr<Gdiplus::Bitmap>();
}
std::unordered_map<std::wstring,std::shared_ptr<Gdiplus::Bitmap>> g_iconCache;
std::shared_ptr<Gdiplus::Bitmap> LoadIconImage(const std::wstring& path,int size) {
    const auto key=std::to_wstring(size)+L":"+path;
    auto found=g_iconCache.find(key);if(found!=g_iconCache.end())return found->second;
    auto image=DecodeIconImage(path,size);
    if(image){if(g_iconCache.size()>=1024)g_iconCache.clear();g_iconCache.emplace(key,image);}
    return image;
}
int BoxRowHeight(const Box& box) { return max(30,HeaderHeight())+(box.iconView?0:0); }
RECT BoxContentRect(const Box& box) {
    const int header=BoxHeaderHeight(box);
    return {box.rect.left+8,box.rect.top+header+8,box.rect.right-8,box.rect.bottom-8};
}
int ListScrollMaximum(const Box& box) {
    if(box.iconView)return 0;
    const RECT content=BoxContentRect(box);
    const int contentHeight=max(0L,content.bottom-content.top);
    return max(0,static_cast<int>(box.items.size())*BoxRowHeight(box)-contentHeight);
}
RECT BoxItemRect(const Box& box,int slot) {
    const int h=BoxHeaderHeight(box);
    if(!box.iconView){int rowHeight=BoxRowHeight(box),scroll=std::clamp(box.listScroll,0,ListScrollMaximum(box));return {box.rect.left+8,box.rect.top+h+8+slot*rowHeight-scroll,box.rect.right-8,box.rect.top+h+8+(slot+1)*rowHeight-scroll};}
    const LONG sx=BoxGridCellWidth(box,g_snapshot),sy=BoxGridCellHeight(g_snapshot),columns=BoxGridColumns(box,g_snapshot),left=BoxGridLeft(box);
    return {left+(slot%columns)*sx,box.rect.top+h+12+(slot/columns)*sy,left+(slot%columns)*sx+sx,box.rect.top+h+12+(slot/columns)*sy+sy};
}
int BoxSlot(const Box& box,POINT point) {
    const int h=BoxHeaderHeight(box);
    if(!box.iconView)return max(0,(point.y-box.rect.top-h-8+std::clamp(box.listScroll,0,ListScrollMaximum(box)))/BoxRowHeight(box));
    const LONG sx=BoxGridCellWidth(box,g_snapshot),sy=BoxGridCellHeight(g_snapshot),columns=BoxGridColumns(box,g_snapshot),left=BoxGridLeft(box);
    LONG column=(point.x-left)/sx,row=(point.y-box.rect.top-h-12)/sy;return max(0L,row*columns+column);
}
void SetVisualGeometry(VisualIcon& visual,const Box* box,int slot) {
    if(!box){
        visual.list=false;visual.size=max(16,g_snapshot.iconSize);
        // IFolderView positions are the icon's top-left point. Centre the
        // clickable cell and title around that point; the old fixed -8 offset
        // only happened to work for one particular Windows icon spacing.
        const LONG padding=max(0L,(g_snapshot.spacing.x-visual.size)/2);
        visual.hit={visual.position.x-padding,visual.position.y,visual.position.x-padding+g_snapshot.spacing.x,visual.position.y+g_snapshot.spacing.y};
        visual.label={visual.hit.left,visual.position.y+visual.size+3,visual.hit.right,visual.hit.bottom};return;
    }
    visual.list=!box->iconView;visual.size=visual.list?min(24,HeaderHeight()):max(16,g_snapshot.iconSize);RECT row=BoxItemRect(*box,slot);visual.hit=row;visual.clip=BoxContentRect(*box);visual.position={visual.list?row.left+8:row.left+max(0L,((row.right-row.left)-visual.size)/2),visual.list?row.top+(row.bottom-row.top-visual.size)/2:row.top};
    // Explorer's icon view reserves the lower part of each grid cell for the
    // (potentially two-line) title.  Do not vertically centre it over the icon.
    const LONG titleHeight=MulDiv(32,static_cast<int>(g_host.listview?GetDpiForWindow(g_host.listview):96),96);
    visual.label=visual.list
        ? RECT{visual.position.x+visual.size+8,row.top,row.right-8,row.bottom}
        : RECT{row.left,visual.position.y+visual.size+3,row.right,min(row.bottom,visual.position.y+visual.size+3+titleHeight)};
}
void ReleaseVisualIcons() { g_visualIcons.clear(); }
bool IsPendingShellDrop(const std::wstring& path) {return std::any_of(g_pendingShellDrops.begin(),g_pendingShellDrops.end(),[&](const auto& pending){return _wcsicmp(pending.path.c_str(),path.c_str())==0;});}
void MarkPendingShellDrops(const std::vector<std::wstring>& paths) {
    const ULONGLONG now=GetTickCount64();
    for(const auto& path:paths)if(!IsPendingShellDrop(path))g_pendingShellDrops.push_back({path,now});
}
void ClearPendingShellDrops(const std::vector<std::wstring>& paths) {
    g_pendingShellDrops.erase(std::remove_if(g_pendingShellDrops.begin(),g_pendingShellDrops.end(),[&](const auto& pending){return std::any_of(paths.begin(),paths.end(),[&](const auto& path){return _wcsicmp(path.c_str(),pending.path.c_str())==0;});}),g_pendingShellDrops.end());
}
void ReconcilePendingShellDrops(const DesktopSnapshot& snapshot) {
    const ULONGLONG now=GetTickCount64();const auto previous=g_pendingShellDrops.size();
    g_pendingShellDrops.erase(std::remove_if(g_pendingShellDrops.begin(),g_pendingShellDrops.end(),[&](const auto& pending){
        // A missing desktop item confirms a move/delete. A target that did
        // not ultimately consume it is restored after a short safety window.
        return !FindEntry(snapshot,pending.path)||now-pending.started>2500;
    }),g_pendingShellDrops.end());
    if(previous!=g_pendingShellDrops.size())g_visualDirty=true;
}
POINT VisualPosition(const std::wstring& path) {
    for(const auto& box:g_layout->boxes)if(HasPath(box,path)&&!box.collapsed) {
        int slot=0;for(const auto& item:box.items){if(_wcsicmp(item.c_str(),path.c_str())==0)break;if(FindEntry(g_snapshot,item))++slot;}
        if(!box.iconView){RECT row=BoxItemRect(box,slot);return {row.left+8,row.top+(row.bottom-row.top-min(24,HeaderHeight()))/2};}
        RECT cell=BoxItemRect(box,slot);return {cell.left+max(0L,((cell.right-cell.left)-g_snapshot.iconSize)/2),cell.top};
    }
    for(const auto& placement:g_layout->desktop)
        if(_wcsicmp(placement.path.c_str(),path.c_str())==0)return placement.point;
    if(const auto* entry=FindEntry(g_snapshot,path))return entry->position;
    return {};
}
void RebuildVisualIcons() {
    std::vector<std::wstring> selected;for(const auto& item:g_visualIcons)if(item.selected)selected.push_back(item.path);ReleaseVisualIcons();if(!g_snapshot.readable)return;
    for(const auto& entry:g_snapshot.entries) {
        if(IsPendingShellDrop(entry.path))continue;
        const Box* owner=nullptr;int slot=0;
        for(const auto& box:g_layout->boxes)if(HasPath(box,entry.path)){owner=&box;for(const auto& item:box.items){if(_wcsicmp(item.c_str(),entry.path.c_str())==0)break;if(FindEntry(g_snapshot,item))++slot;}break;}
        if(owner&&(!IsActiveTab(*owner)||GroupRoot(*owner)->collapsed))continue;
        VisualIcon visual;visual.path=entry.path;visual.name=entry.displayName.empty()?entry.path:entry.displayName;
        if(entry.displayName.empty()) {auto slash=visual.name.find_last_of(L"\\/");if(slash!=std::wstring::npos)visual.name=visual.name.substr(slash+1);}
        visual.position=VisualPosition(entry.path);visual.image=LoadIconImage(entry.path,owner&& !owner->iconView?min(24,HeaderHeight()):max(16,g_snapshot.iconSize));visual.selected=std::any_of(selected.begin(),selected.end(),[&](const auto& path){return _wcsicmp(path.c_str(),entry.path.c_str())==0;});SetVisualGeometry(visual,owner,slot);g_visualIcons.push_back(std::move(visual));
    }
    if(g_focusIcon>=static_cast<int>(g_visualIcons.size()))g_focusIcon=-1;
}
void RefreshVisualGeometry() {
    // Box dragging changes coordinates only. Retain the already decoded shell
    // icons instead of reopening every item from disk on each mouse message.
    for(auto& visual:g_visualIcons) {
        const Box* owner=nullptr;int slot=0;
        for(const auto& box:g_layout->boxes)if(HasPath(box,visual.path)) {
            owner=&box;
            for(const auto& item:box.items) {
                if(_wcsicmp(item.c_str(),visual.path.c_str())==0)break;
                if(FindEntry(g_snapshot,item))++slot;
            }
            break;
        }
        if(owner&&owner->collapsed) {g_visualDirty=true;return;}
        visual.position=VisualPosition(visual.path);
        SetVisualGeometry(visual,owner,slot);
    }
}
bool RenderIconInCurrentPass(const VisualIcon& icon) {
    if(g_previewGroup.empty()&&g_renderOnlyGroup.empty())return true;
    std::wstring owner;
    for(const auto& box:g_layout->boxes)if(HasPath(box,icon.path)){owner=GroupRoot(box)->id;break;}
    return g_renderOnlyGroup.empty()?owner!=g_previewGroup:owner==g_renderOnlyGroup;
}
void DrawVisualIcons(Gdiplus::Graphics& g,bool iconPreview) {
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::FontFamily family(CanvasFontFamily());
    for(auto& item:g_visualIcons) {
        if((iconPreview||g_iconDragPreview)&&g_iconPreviewPaths.contains(item.path)!=iconPreview)continue;
        if(!RenderIconInCurrentPass(item))continue;
        const auto state=g.Save();
        if(item.list)g.SetClip(Gdiplus::Rect(item.clip.left,item.clip.top,item.clip.right-item.clip.left,item.clip.bottom-item.clip.top));
        if(item.selected){Gdiplus::SolidBrush selected(Gdiplus::Color(72,55,133,190));Gdiplus::RectF r(static_cast<float>(item.hit.left),static_cast<float>(item.hit.top),static_cast<float>(item.hit.right-item.hit.left),static_cast<float>(item.hit.bottom-item.hit.top));g.FillRectangle(&selected,r);}
        if(item.image)g.DrawImage(item.image.get(),item.position.x,item.position.y,item.size,item.size);
        Gdiplus::StringFormat format;format.SetAlignment(item.list?Gdiplus::StringAlignmentNear:Gdiplus::StringAlignmentCenter);format.SetLineAlignment(item.list?Gdiplus::StringAlignmentCenter:Gdiplus::StringAlignmentNear);format.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);if(item.list)format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);else format.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);
        Gdiplus::SolidBrush text(Gdiplus::Color(255,255,255,255)),shadow(Gdiplus::Color(190,0,0,0));Gdiplus::Font font(&family,item.list?Gdiplus::REAL(12):Gdiplus::REAL(12),Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::RectF label(static_cast<float>(item.label.left),static_cast<float>(item.label.top),static_cast<float>(max(1L,item.label.right-item.label.left)),static_cast<float>(max(1L,item.label.bottom-item.label.top))),offset=label;offset.X+=1.0f;offset.Y+=1.0f;g.DrawString(item.name.c_str(),-1,&font,offset,&format,&shadow);g.DrawString(item.name.c_str(),-1,&font,label,&format,&text);g.Restore(state);
    }
    if(!iconPreview&&g_marquee&&g_marqueeMoved){Gdiplus::SolidBrush fill(Gdiplus::Color(45,80,160,230));Gdiplus::Pen edge(Gdiplus::Color(190,130,190,255),1.0f);Gdiplus::RectF r(static_cast<float>(g_marqueeRect.left),static_cast<float>(g_marqueeRect.top),static_cast<float>(g_marqueeRect.right-g_marqueeRect.left),static_cast<float>(g_marqueeRect.bottom-g_marqueeRect.top));g.FillRectangle(&fill,r);g.DrawRectangle(&edge,r);}
}
HWND CreateDragPreviewWindow(const RECT& bounds,PaintBuffer& buffer) {
    WNDCLASSW wc{};
    wc.hInstance=g_instance;
    wc.lpfnWndProc=DefWindowProcW;
    wc.lpszClassName=L"nestlone-D.DragPreview";
    RegisterClassW(&wc);
    const POINT top=ParentPoint({bounds.left,bounds.top});
    SIZE size{bounds.right-bounds.left,bounds.bottom-bounds.top};
    HWND preview=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
        wc.lpszClassName,L"",SurfaceWindowStyle(),top.x,top.y,size.cx,size.cy,SurfaceParent(),nullptr,g_instance,nullptr);
    if(!preview)return nullptr;
    POINT source{};
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if(UpdateLayeredWindow(preview,nullptr,nullptr,&size,buffer.dc,&source,0,&blend,ULW_ALPHA))return preview;
    DestroyWindow(preview);
    return nullptr;
}
void EndIconDragPreview() {
    HWND preview=g_iconDragPreview;
    g_iconDragPreview=nullptr;
    g_iconPreviewPaths.clear();
    if(preview)DestroyWindow(preview);
}
void BeginIconDragPreview() {
    for(const auto& start:g_iconDragStart)g_iconPreviewPaths.insert(start.first);
    RECT bounds{};
    for(const auto& item:g_visualIcons) {
        if(!g_iconPreviewPaths.contains(item.path))continue;
        RECT visible=item.hit;
        if(item.list&&!IntersectRect(&visible,&item.hit,&item.clip))continue;
        UnionRect(&bounds,&bounds,&visible);
    }
    if(!IsRectEmpty(&bounds)) {
        InflateRect(&bounds,2,2);
        const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        PaintBuffer buffer;
        if(buffer.Ensure(width,height)) {
            Gdiplus::Bitmap surface(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(buffer.pixels));
            Gdiplus::Graphics graphics(&surface);
            graphics.Clear(Gdiplus::Color(0,0,0,0));
            graphics.TranslateTransform(static_cast<float>(-bounds.left),static_cast<float>(-bounds.top));
            DrawVisualIcons(graphics,true);
            graphics.Flush();
            g_iconDragPreview=CreateDragPreviewWindow(bounds,buffer);
        }
    }
    if(g_iconDragPreview) {
        g_iconPreviewOrigin=ParentPoint({bounds.left,bounds.top});
        PaintWindow(g_background);
    } else {
        g_iconPreviewPaths.clear();
    }
}
void EndDragPreview() {
    HWND preview=g_dragPreview;g_dragPreview=nullptr;g_previewGroup.clear();g_renderOnlyGroup.clear();
    if(preview)DestroyWindow(preview);
}
bool BeginDragPreview(Box& box) {
    if(g_dragPreview)return true;
    if(!g_nativeHidden||g_resizing)return false;
    // Multiple separately selected groups continue to use the normal path.
    for(const auto& start:g_boxDragStart)if(auto* member=FindBox(start.first))if(GroupRoot(*member)!=&box)return false;
    if(g_visualDirty||g_visualGeometryDirty)PaintAll();
    RECT bounds=DisplayRect(box);const int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
    PaintBuffer buffer;if(width<=0||height<=0||!buffer.Ensure(width,height))return false;
    {
        Gdiplus::Bitmap surface(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(buffer.pixels));
        Gdiplus::Graphics graphics(&surface);graphics.Clear(Gdiplus::Color(0,0,0,0));
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.TranslateTransform(static_cast<float>(-bounds.left),static_cast<float>(-bounds.top));
        Rounded(graphics,bounds,Background(box.color,g_layout->opacity),CornerDiameter(*g_layout));
        g_renderOnlyGroup=box.id;DrawVisualIcons(graphics);g_renderOnlyGroup.clear();graphics.Flush();
    }
    HWND preview=CreateDragPreviewWindow(bounds,buffer);
    if(!preview)return false;
    g_dragPreview=preview;g_previewGroup=box.id;
    // Remove only the moving group's pixels from the static desktop once.
    PaintWindow(g_background);
    SetWindowPos(preview,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    return true;
}
void MoveDragPreview(Box& box) {
    const POINT top=ParentPoint({box.rect.left,box.rect.top});
    const POINT right=ParentPoint({box.rect.right-18,box.rect.bottom-18});
    const POINT left=ParentPoint({box.rect.left,box.rect.bottom-18});
    const auto decoration=std::find_if(g_decorations.begin(),g_decorations.end(),
        [&](const auto& item){return item->id==box.id;});
    constexpr UINT flags=SWP_NOSIZE|SWP_NOACTIVATE;
    if(decoration!=g_decorations.end()) {
        const auto& d=**decoration;
        // Present the cached body and its controls as one position update.
        HDWP batch=BeginDeferWindowPos(4);
        if(batch)batch=DeferWindowPos(batch,g_dragPreview,HWND_TOP,top.x,top.y,0,0,flags);
        if(batch)batch=DeferWindowPos(batch,d.header,HWND_TOP,top.x,top.y,0,0,flags);
        if(batch)batch=DeferWindowPos(batch,d.grip,HWND_TOP,right.x,right.y,0,0,flags);
        if(batch)batch=DeferWindowPos(batch,d.gripLeft,HWND_TOP,left.x,left.y,0,0,flags);
        if(batch&&EndDeferWindowPos(batch))return;
        SetWindowPos(g_dragPreview,HWND_TOP,top.x,top.y,0,0,flags);
        SetWindowPos(d.header,HWND_TOP,top.x,top.y,0,0,flags);
        SetWindowPos(d.grip,HWND_TOP,right.x,right.y,0,0,flags);
        SetWindowPos(d.gripLeft,HWND_TOP,left.x,left.y,0,0,flags);
        return;
    }
    SetWindowPos(g_dragPreview,HWND_TOP,top.x,top.y,0,0,flags);
}
void Arrange(Box& box,const DesktopSnapshot& snapshot=g_snapshot) {
    if(!snapshot.readable || box.collapsed)return;
    if(snapshot.autoArrange){g_status=L"请先关闭桌面“自动排列图标”";return;}
    if(!snapshot.iconMode){g_status=L"当前桌面视图不支持自由定位";return;}
    Box fitted=box;fitted.rect=FitGrid(box,snapshot);
    if(fitted.rect.bottom>GetSystemMetrics(SM_CYVIRTUALSCREEN) || fitted.rect.right>GetSystemMetrics(SM_CXVIRTUALSCREEN)) {
        g_status=L"空间不足，请加宽盒子或移到更大的可用区域";return;
    }
    auto moves=GridMoves(fitted,snapshot);
    for(const auto& move:moves) {
        LONG x=move.position.x-max(0L,(snapshot.spacing.x-snapshot.iconSize)/2),y=move.position.y;
        const LONG sx=snapshot.spacing.x,sy=snapshot.spacing.y;
        RECT target{x,y,x+sx,y+sy};bool occupied=false;
        for(const auto& e:snapshot.entries)if(!HasPath(box,e.path)) {RECT overlap{};if(IntersectRect(&overlap,&target,&e.bounds)){occupied=true;break;}}
        if(occupied){g_status=L"目标位置有未收纳图标，请先移开或使用“收纳区域内图标”";return;}
    }
    box.rect=fitted.rect;
    SyncGroupRect(box);
    if(g_nativeHidden){g_visualDirty=true;PaintAll();SaveLayout(*g_layout);return;}
    if(!moves.empty())QueueMoves(moves);
    PaintAll();SaveLayout(*g_layout);
}
void Observe(const DesktopSnapshot& before,const DesktopSnapshot& after) {
    if(!before.readable||before.listview!=after.listview||before.process!=after.process)return;
    bool changed=false;std::vector<bool> arrange(g_layout->boxes.size(),false);
    for(const auto& entry:after.entries) {
        auto old=FindEntry(before,entry.path);
        if(old && old->position.x==entry.position.x && old->position.y==entry.position.y)continue;
        int owner=-1;for(int i=0;i<static_cast<int>(g_layout->boxes.size());++i)if(HasPath(g_layout->boxes[i],entry.path)){owner=i;break;}
        int target=ContainingBox(entry);
        if(target>=0)arrange[target]=true;
        if(owner>=0)arrange[owner]=true;
        if(target==owner)continue;
        for(auto& box:g_layout->boxes)box.items.erase(std::remove_if(box.items.begin(),box.items.end(),[&](const auto& p){return _wcsicmp(p.c_str(),entry.path.c_str())==0;}),box.items.end());
        if(target>=0)AddItem(g_layout->boxes[target],entry.path);
        changed=true;
    }
    if(changed)SaveLayout(*g_layout);
    for(size_t i=0;i<arrange.size();++i)if(arrange[i])Arrange(g_layout->boxes[i],after);
}
void PlaceNewDesktopEntries(const DesktopSnapshot& before,const DesktopSnapshot& after) {
    if(!g_layout||!before.readable||before.listview!=after.listview)return;
    std::vector<std::pair<std::wstring,POINT>> added;
    bool organized=false;
    for(const auto& entry:after.entries) {
        if(FindEntry(before,entry.path))continue;
        auto& boxes=g_layout->boxes;
        if(std::any_of(boxes.begin(),boxes.end(),[&](const Box& box){return HasPath(box,entry.path);}))continue;
        int target=-1;
        if(g_layout->autoOrganize&&!g_layout->autoRules.empty()) {
            const DWORD attributes=GetFileAttributesW(entry.path.c_str());
            if(attributes!=INVALID_FILE_ATTRIBUTES) {
                const bool directory=(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
                const auto extension=std::filesystem::path(entry.path).extension().wstring();
                for(const auto& rule:g_layout->autoRules) {
                    if(!MatchesAutoRule(rule,directory,extension))continue;
                    const auto box=std::find_if(boxes.begin(),boxes.end(),[&](const Box& item){return item.id==rule.boxId;});
                    if(box!=boxes.end()){target=static_cast<int>(box-boxes.begin());break;}
                }
            }
        }
        if(target>=0)organized|=AssignDesktopItem(*g_layout,entry.path,target,entry.position);
        else added.push_back({entry.path,entry.position});
    }
    if(added.empty()&&!organized)return;
    // Explorer can place a just-created item at a coordinate occupied by our
    // presentation layer.  Retain its intended vicinity, but resolve it to a
    // vacant desktop grid cell before the next paint.
    const auto placed=SnapDesktopDrop(added);
    for(const auto& item:placed)AssignDesktopItem(*g_layout,item.first,-1,item.second);
    std::vector<DesktopMove> moves;for(const auto& item:placed)moves.push_back({item.first,item.second});
    if(!moves.empty())QueueMoves(moves);
    QueueLayoutSave();
}
void ApplyDesktopSnapshot(DesktopSnapshot&& next,bool forceRepaint=false) {
    const auto change=CompareDesktopVisuals(g_snapshot,next);
    if(change==DesktopVisualChange::Items) {
        PlaceNewDesktopEntries(g_snapshot,next);
        g_iconCache.clear();
    }
    ReconcilePendingShellDrops(next);
    g_snapshot=std::move(next);
    if(g_nativeHidden&&(forceRepaint||g_visualDirty||change!=DesktopVisualChange::None)) {
        g_visualDirty=true;
        PaintAll();
    }
}
constexpr UINT kFinishRenameMessage=WM_APP+10;
void RequestFinishRename(HWND edit) {
    PostMessageW(g_manager,kFinishRenameMessage,0,reinterpret_cast<LPARAM>(edit));
}
LRESULT CALLBACK RenameMouseProc(int code,WPARAM message,LPARAM data) {
    // The desktop canvas uses MA_NOACTIVATE, so clicking it can leave the
    // editor focused. Observe outside presses without consuming them.
    if(code==HC_ACTION&&g_edit&&(message==WM_LBUTTONDOWN||message==WM_RBUTTONDOWN||
        message==WM_MBUTTONDOWN||message==WM_XBUTTONDOWN)) {
        const auto* mouse=reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
        const HWND clicked=WindowFromPoint(mouse->pt);
        if(clicked!=g_edit&&!IsChild(g_edit,clicked))RequestFinishRename(g_edit);
    }
    return CallNextHookEx(nullptr,code,message,data);
}
void FinishRename(bool save) {
    HWND edit=g_edit;if(!edit)return;g_edit=nullptr;
    if(g_renameMouseHook) {
        UnhookWindowsHookEx(g_renameMouseHook);
        g_renameMouseHook=nullptr;
    }
    const bool modified=SendMessageW(edit,EM_GETMODIFY,0,0)!=0;
    std::wstring text(static_cast<size_t>(GetWindowTextLengthW(edit))+1,L'\0');
    text.resize(GetWindowTextW(edit,text.data(),static_cast<int>(text.size())));
    ShowWindow(edit,SW_HIDE);
    const auto first=text.find_first_not_of(L" \t\r\n");
    if(save&&modified&&first!=std::wstring::npos) {
        const std::wstring trimmed=text.substr(first,text.find_last_not_of(L" \t\r\n")-first+1);
        if(!g_editItemPath.empty()) {
            const std::wstring newName=trimmed+g_editHiddenExtension;
            ComPtr<IShellItem> item;ComPtr<IFileOperation> operation;
            const HRESULT itemResult=SHCreateItemFromParsingName(g_editItemPath.c_str(),nullptr,IID_PPV_ARGS(&item));
            const HRESULT operationResult=SUCCEEDED(itemResult)?CoCreateInstance(CLSID_FileOperation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&operation)):itemResult;
            if(SUCCEEDED(operationResult) && SUCCEEDED(operation->SetOperationFlags(FOF_NOCONFIRMATION)) &&
               SUCCEEDED(operation->RenameItem(item.Get(),newName.c_str(),nullptr)) && SUCCEEDED(operation->PerformOperations())) {
                g_shellRefreshAttempts=10;SetTimer(g_manager,kShellRefreshTimer,120,nullptr);
            } else g_status=L"无法重命名该桌面项目";
        } else if(auto* box=FindBox(g_editId)) {
            if(g_editGroupTitle)box->groupTitle=trimmed;else box->title=trimmed;
            SaveLayout(*g_layout);
        }
    }
    DestroyWindow(edit);g_editId.clear();g_editItemPath.clear();g_editHiddenExtension.clear();g_editGroupTitle=false;PaintAll();
}
LRESULT CALLBACK EditProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_KEYDOWN&&(wp==VK_RETURN||wp==VK_ESCAPE)){FinishRename(wp==VK_RETURN);return 0;}
    if(msg==WM_KILLFOCUS){RequestFinishRename(hwnd);return 0;}
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(hwnd,EditProc,1);
    return DefSubclassProc(hwnd,msg,wp,lp);
}
void ShowRenameEditor(int selectionEnd) {
    SetWindowSubclass(g_edit,EditProc,1,0);
    SendMessageW(g_edit,EM_SETMODIFY,FALSE,0);
    g_renameMouseHook=SetWindowsHookExW(WH_MOUSE_LL,RenameMouseProc,g_instance,0);
    ShowWindow(g_edit,SW_SHOW);
    SetForegroundWindow(g_edit);
    SetFocus(g_edit);
    SendMessageW(g_edit,EM_SETSEL,0,selectionEnd);
}
void BeginRename(Box& box,bool groupTitle=false) {
    FinishRename(true);g_editId=box.id;g_editGroupTitle=groupTitle;
    POINT p{box.rect.left+HeaderHeight(),box.rect.top+HeaderHeight()/4};
    p.x+=GetSystemMetrics(SM_XVIRTUALSCREEN);p.y+=GetSystemMetrics(SM_YVIRTUALSCREEN);
    const std::wstring& value=groupTitle?GroupTitle(box):box.title;
    g_edit=CreateWindowExW(WS_EX_TOOLWINDOW,L"EDIT",value.c_str(),WS_POPUP|ES_CENTER|ES_AUTOHSCROLL,p.x,p.y,
        max(40L,box.rect.right-box.rect.left-4*HeaderHeight()),HeaderHeight()*3/4,nullptr,nullptr,g_instance,nullptr);
    if(!g_edit)return;
    if(g_editFont)DeleteObject(g_editFont);
    g_editFont=CreateFontW(-HeaderHeight()*2/5,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    SendMessageW(g_edit,WM_SETFONT,reinterpret_cast<WPARAM>(g_editFont),TRUE);SendMessageW(g_edit,EM_SETLIMITTEXT,80,0);
    ShowRenameEditor(-1);
}
void BeginItemRename(const std::wstring& requestedPath) {
    const std::wstring path=requestedPath;
    FinishRename(true);
    auto found=std::find_if(g_visualIcons.begin(),g_visualIcons.end(),[&](const auto& icon){return _wcsicmp(icon.path.c_str(),path.c_str())==0;});
    if(found==g_visualIcons.end())return;
    RECT label=found->label;
    if(found->list&&!IntersectRect(&label,&found->label,&found->clip))return;
    if(IsRectEmpty(&label))return;
    POINT top{label.left,label.top};
    top.x+=GetSystemMetrics(SM_XVIRTUALSCREEN);top.y+=GetSystemMetrics(SM_YVIRTUALSCREEN);
    const DWORD alignment=found->list?ES_LEFT:ES_CENTER;
    g_edit=CreateWindowExW(WS_EX_TOOLWINDOW,L"EDIT",found->name.c_str(),WS_POPUP|alignment|ES_AUTOHSCROLL,
        top.x,top.y,label.right-label.left,label.bottom-label.top,nullptr,nullptr,g_instance,nullptr);
    if(!g_edit)return;
    g_editItemPath=path;
    int selectEnd=-1;
    const DWORD attributes=GetFileAttributesW(path.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY)) {
        const auto filename=std::filesystem::path(path).filename();
        const auto extension=filename.extension().wstring();
        if(!extension.empty()&&filename.stem().wstring()==found->name) {
            // Explorer's display name can omit extensions, including .lnk.
            g_editHiddenExtension=extension;
        } else if(!extension.empty()&&filename.wstring()==found->name) {
            selectEnd=static_cast<int>(found->name.size()-extension.size());
        }
    }
    if(g_editFont)DeleteObject(g_editFont);
    g_editFont=CreateFontW(-max(12,HeaderHeight()*2/5),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,CanvasFontFamily());
    SendMessageW(g_edit,WM_SETFONT,reinterpret_cast<WPARAM>(g_editFont),TRUE);SendMessageW(g_edit,EM_SETLIMITTEXT,260,0);
    ShowRenameEditor(selectEnd);
}
void DestroyDecorations() {
    EndDragPreview();
    EndIconDragPreview();
    g_iconDrag=-1;g_iconMoved=false;g_iconDragStart.clear();
    g_marquee=false;g_marqueeMoved=false;g_selectionStart.clear();
    FinishRename(false);
    for(auto& widget:g_widgets){if(IsWindow(widget->window))DestroyWindow(widget->window);g_paintBuffers.erase(widget->window);}
    g_widgets.clear();
    for(auto& d:g_decorations){if(IsWindow(d->header))DestroyWindow(d->header);if(IsWindow(d->gripLeft))DestroyWindow(d->gripLeft);if(IsWindow(d->grip))DestroyWindow(d->grip);}
    g_decorations.clear();if(g_dropRegistered&&IsWindow(g_background)){RevokeDragDrop(g_background);g_dropRegistered=false;}if(IsWindow(g_background))DestroyWindow(g_background);g_background=nullptr;
    ReleaseVisualIcons();if(IsWindow(g_hiddenListview))ReleaseHiddenDesktopListView(g_hiddenListview);g_hiddenListview=nullptr;g_nativeHidden=false;
}
LRESULT CALLBACK DecorationProc(HWND,UINT,WPARAM,LPARAM);
void FillNoteRect(HDC dc,const RECT& rect,COLORREF color) {
    SetDCBrushColor(dc,color);
    FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
COLORREF NoteInk(COLORREF color) {
    return GetRValue(color)*299+GetGValue(color)*587+GetBValue(color)*114>=145000?note::ink:note::paper;
}
COLORREF NoteMix(COLORREF base,COLORREF ink,int percent) {
    const auto mix=[percent](int a,int b){return (a*(100-percent)+b*percent)/100;};
    return RGB(mix(GetRValue(base),GetRValue(ink)),mix(GetGValue(base),GetGValue(ink)),mix(GetBValue(base),GetBValue(ink)));
}
void PaintNote(HWND hwnd,HDC dc,const Layout::Widget& widget,const WidgetWindow& state) {
    RECT client{};GetClientRect(hwnd,&client);
    const COLORREF ink=NoteInk(widget.color),edge=NoteMix(widget.color,ink,17);
    FillNoteRect(dc,client,widget.color);
    RECT header=client;header.bottom=note::headerHeight;
    FillNoteRect(dc,header,NoteMix(widget.color,ink,5));
    FillNoteRect(dc,{0,header.bottom-1,client.right,header.bottom},edge);
    SetDCBrushColor(dc,edge);
    FrameRect(dc,&client,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,ink);
    const auto font=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
    RECT title{note::padding,0,client.right-3*kNoteButtonWidth-kNoteButtonInset-6,note::headerHeight};
    DrawTextW(dc,L"便签",-1,&title,DT_SINGLELINE|DT_VCENTER|DT_LEFT|DT_END_ELLIPSIS);
    SelectObject(dc,font);
    const auto scroll=ReadNoteScroll(state);
    if(scroll.last)FillNoteRect(dc,scroll.thumb,NoteMix(widget.color,ink,state.scrollGrab>=0?48:27));
}
void DrawNoteButton(const DRAWITEMSTRUCT& draw,const Layout::Widget& widget) {
    const HDC dc=draw.hDC;
    const RECT r=draw.rcItem;
    const auto ink=NoteInk(widget.color);
    const bool pressed=(draw.itemState&ODS_SELECTED)!=0;
    const bool disabled=(draw.itemState&ODS_DISABLED)!=0;
    const bool locked=draw.CtlID==kNoteLock&&widget.locked;
    FillNoteRect(dc,r,NoteMix(widget.color,ink,pressed?24:locked?16:5));
    const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
    const auto oldPen=SelectObject(dc,GetStockObject(DC_PEN));
    const auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    SetDCPenColor(dc,disabled?NoteMix(widget.color,ink,35):ink);
    if(draw.CtlID==kNoteClose) {
        MoveToEx(dc,x-5,y-5,nullptr);LineTo(dc,x+6,y+6);
        MoveToEx(dc,x-5,y+5,nullptr);LineTo(dc,x+6,y-6);
    } else if(draw.CtlID==kNoteLock) {
        RoundRect(dc,x-6,y-1,x+7,y+8,3,3);
        Arc(dc,x-4,y-8,x+5,y+1,x+5,y-4,x-4,y-4);
        MoveToEx(dc,x-4,y-4,nullptr);LineTo(dc,x-4,y-1);
        if(locked){MoveToEx(dc,x+4,y-4,nullptr);LineTo(dc,x+4,y-1);}
    } else {
        RECT sample{x-6,y-6,x+7,y+7};
        FillNoteRect(dc,sample,widget.color);
        RoundRect(dc,sample.left,sample.top,sample.right,sample.bottom,4,4);
    }
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);
    if(draw.itemState&ODS_FOCUS){RECT focus=r;InflateRect(&focus,-2,-2);DrawFocusRect(dc,&focus);}
}
LRESULT NoteResizeHit(const RECT& r,POINT point,bool locked) {
    if(locked)return HTCLIENT;
    constexpr LONG edge=6;
    const bool left=point.x<r.left+edge,right=point.x>=r.right-edge;
    if(point.y<r.top+edge)return left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP;
    if(point.y>=r.bottom-edge)return left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM;
    return left?HTLEFT:right?HTRIGHT:HTCLIENT;
}
void PickNoteColor(HWND hwnd,const std::wstring& id,COLORREF color) {
    static COLORREF recent[16]={RGB(255,255,255),RGB(255,249,213),RGB(248,231,233),RGB(227,242,232),RGB(226,237,250)};
    CHOOSECOLORW picker{};
    picker.lStructSize=sizeof(picker);
    picker.hwndOwner=hwnd;
    picker.rgbResult=color;
    picker.lpCustColors=recent;
    picker.Flags=CC_RGBINIT|CC_FULLOPEN;
    if(!ChooseColorW(&picker))return;
    auto* model=FindWidget(id);
    auto* state=FindWidgetWindow(hwnd);
    if(!model||!state||model->locked)return;
    model->color=picker.rgbResult;
    RefreshNoteControls(hwnd,*state,*model);
    QueueLayoutSave();
}
LRESULT CALLBACK WidgetProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<WidgetWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE) {
        auto* create=reinterpret_cast<CREATESTRUCTW*>(lp);
        state=static_cast<WidgetWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    Layout::Widget* widget=state?FindWidget(state->id):nullptr;
    if(!widget)return DefWindowProcW(hwnd,message,wp,lp);
    switch(message) {
    case WM_NCCALCSIZE:
    case WM_NCPAINT:return 0;
    case WM_NCACTIVATE:return TRUE;
    case WM_NCHITTEST: {
        RECT bounds{};GetWindowRect(hwnd,&bounds);
        return NoteResizeHit(bounds,{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)},widget->locked);
    }
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lp);
        info->ptMinTrackSize={160,100};
        return 0;
    }
    case WM_ENTERSIZEMOVE:state->resizing=true;return 0;
    case WM_EXITSIZEMOVE:
        state->resizing=false;
        if(!widget->locked&&GetWindowRect(hwnd,&widget->rect)) {
            OffsetRect(&widget->rect,-GetSystemMetrics(SM_XVIRTUALSCREEN),-GetSystemMetrics(SM_YVIRTUALSCREEN));
            QueueLayoutSave();
        }
        RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
        return 0;
    case WM_MOUSEACTIVATE:return MA_ACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        PaintNote(hwnd,BeginPaint(hwnd,&paint),*widget,*state);
        EndPaint(hwnd,&paint);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        const auto dc=reinterpret_cast<HDC>(wp);
        SetTextColor(dc,NoteInk(widget->color));
        SetBkColor(dc,widget->color);
        SetDCBrushColor(dc,widget->color);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_SIZE: {
        const int width=max(1,LOWORD(lp)),height=max(1,HIWORD(lp));
        if(HRGN region=CreateRoundRectRgn(0,0,width+1,height+1,12,12))
            if(!SetWindowRgn(hwnd,region,FALSE))DeleteObject(region);
        LayoutNoteControls(hwnd,*state);
        // Paint the exposed paper and the moved editor/buttons together.
        RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
        return 0;
    }
    case WM_DRAWITEM: {
        const auto& draw=*reinterpret_cast<const DRAWITEMSTRUCT*>(lp);
        if(draw.CtlType!=ODT_BUTTON)return FALSE;
        DrawNoteButton(draw,*widget);
        return TRUE;
    }
    case WM_CLOSE:
        widget->closed=true;
        ShowWindow(hwnd,SW_HIDE);
        QueueLayoutSave();
        return 0;
    case WM_COMMAND:
        if(HIWORD(wp)==BN_CLICKED) {
            switch(LOWORD(wp)) {
            case kNoteClose:SendMessageW(hwnd,WM_CLOSE,0,0);return 0;
            case kNoteLock:
                widget->locked=!widget->locked;
                RefreshNoteControls(hwnd,*state,*widget);
                QueueLayoutSave();
                return 0;
            case kNoteColor:
                if(!widget->locked)PickNoteColor(hwnd,std::wstring(widget->id),widget->color);
                return 0;
            }
        }
        if(state->edit&&reinterpret_cast<HWND>(lp)==state->edit&&!state->syncingText) {
            if(HIWORD(wp)==EN_CHANGE) {
                widget->text=NoteText(state->edit);
                InvalidateNoteScroll(hwnd);
                state->pendingSave=true;
                SetTimer(hwnd,1,450,nullptr);
            } else if(HIWORD(wp)==EN_KILLFOCUS&&state->pendingSave) {
                KillTimer(hwnd,1);
                state->pendingSave=false;
                QueueLayoutSave();
            }
        }
        return 0;
    case WM_MOUSEWHEEL:
        ScrollNoteWheel(*state,wp);
        return 0;
    case WM_LBUTTONDOWN:
        if(GET_Y_LPARAM(lp)>=note::headerHeight) {
            const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            const auto scroll=ReadNoteScroll(*state);
            if(scroll.last&&PtInRect(&scroll.track,point)) {
                if(point.y>=scroll.thumb.top&&point.y<scroll.thumb.bottom) {
                    state->scrollGrab=point.y-scroll.thumb.top;
                    SetCapture(hwnd);
                    InvalidateNoteScroll(hwnd);
                } else {
                    ScrollNoteTo(*state,scroll,scroll.first+(point.y<scroll.thumb.top?-scroll.page:scroll.page));
                }
            }
            return 0;
        }
        if(!widget->locked) {
            SetWindowPos(hwnd,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            state->dragging=true;
            state->start=widget->rect;
            GetCursorPos(&state->mouse);
            SetCapture(hwnd);
        }
        return 0;
    case WM_MOUSEMOVE:
        if(state->scrollGrab>=0&&GetCapture()==hwnd) {
            const auto scroll=ReadNoteScroll(*state);
            const LONG travel=(scroll.track.bottom-scroll.track.top)-(scroll.thumb.bottom-scroll.thumb.top);
            if(scroll.last&&travel>0) {
                const LONG y=std::clamp(GET_Y_LPARAM(lp)-state->scrollGrab-scroll.track.top,0L,travel);
                ScrollNoteTo(*state,scroll,MulDiv(y,scroll.last,travel));
            }
            return 0;
        }
        if(state->dragging&&GetCapture()==hwnd) {
            POINT cursor{};GetCursorPos(&cursor);
            widget->rect=state->start;
            OffsetRect(&widget->rect,cursor.x-state->mouse.x,cursor.y-state->mouse.y);
            widget->rect=KeepBoxOnScreen(widget->rect,note::headerHeight);
            const POINT top=NoteScreenOrigin(widget->rect);
            SetWindowPos(hwnd,nullptr,top.x,top.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);
            return 0;
        }
        break;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
        if(state->scrollGrab>=0) {
            state->scrollGrab=-1;
            if(message==WM_LBUTTONUP)ReleaseCapture();
            InvalidateNoteScroll(hwnd);
            return 0;
        }
        if(state->dragging) {
            state->dragging=false;
            if(message==WM_LBUTTONUP)ReleaseCapture();
            QueueLayoutSave();
        }
        return 0;
    case WM_CANCELMODE:
        if(GetCapture()==hwnd)ReleaseCapture();
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd,1);
        if(state->pendingSave&&g_layout)SaveLayout(*g_layout);
        return 0;
    case WM_TIMER:
        if(wp==1&&state->pendingSave) {
            KillTimer(hwnd,1);
            state->pendingSave=false;
            SaveLayout(*g_layout);
        }
        return 0;
    case WM_RBUTTONUP: {
        POINT cursor{};GetCursorPos(&cursor);
        HMENU menu=CreatePopupMenu();
        if(!menu)return 0;
        AppendMenuW(menu,MF_STRING|(widget->locked?MF_GRAYED:0),1,L"删除便签");
        const UINT command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,cursor.x,cursor.y,0,hwnd,nullptr);
        DestroyMenu(menu);
        if(command==1) {
            const auto id=state->id;
            std::erase_if(g_layout->widgets,[&](const auto& item){return item.id==id&&!item.locked;});
            QueueLayoutSave();
            PostMessageW(g_manager,WM_APP+11,0,0);
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}

int HitVisualIcon(int x,int y) { for(int i=static_cast<int>(g_visualIcons.size())-1;i>=0;--i)if(PtInRect(&g_visualIcons[i].hit,POINT{x,y}))return i;return -1; }
ComPtr<IDropTarget> ShellDropTargetFor(const std::wstring& path) {
    PIDLIST_ABSOLUTE pidl=nullptr;ComPtr<IShellFolder> parent;PCUITEMID_CHILD child=nullptr;ComPtr<IDropTarget> target;
    if(SUCCEEDED(SHParseDisplayName(path.c_str(),nullptr,&pidl,0,nullptr))&&pidl) {
        if(SUCCEEDED(SHBindToParent(pidl,IID_PPV_ARGS(&parent),&child)))parent->GetUIObjectOf(g_host.defviewParent?g_host.defviewParent:g_background,1,&child,IID_IDropTarget,nullptr,reinterpret_cast<void**>(target.GetAddressOf()));
        CoTaskMemFree(pidl);
    }
    return target;
}
// The canvas owns icon hit testing, so it must also be an OLE drop target.
// Delegate the actual operation to the selected item's Shell IDropTarget: this
// retains Explorer's behavior for programs, folders, Recycle Bin and extensions.
class CanvasDropTarget final : public IDropTarget {
    LONG references_=1;ComPtr<IDropTarget> target_;ComPtr<IDataObject> data_;std::wstring path_;
    void Clear(bool leave) {if(target_&&leave)target_->DragLeave();target_.Reset();data_.Reset();path_.clear();}
    HRESULT Select(IDataObject* data,DWORD keys,POINTL point,DWORD* effect) {
        const int index=HitVisualIcon(point.x-GetSystemMetrics(SM_XVIRTUALSCREEN),point.y-GetSystemMetrics(SM_YVIRTUALSCREEN));
        const std::wstring next=index>=0?g_visualIcons[index].path:L"";
        const std::wstring key=next.empty()?L"<desktop-background>":next;
        if(!target_||_wcsicmp(key.c_str(),path_.c_str())!=0) {Clear(true);if(next.empty())CreateDesktopBackgroundDropTarget(g_host.defviewParent?g_host.defviewParent:g_background,target_.GetAddressOf());else target_=ShellDropTargetFor(next);path_=key;}
        if(!target_) {if(effect)*effect=DROPEFFECT_NONE;return S_OK;}
        data_=data;return target_->DragEnter(data,keys,point,effect);
    }
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** value) override {if(!value)return E_POINTER;*value=nullptr;if(iid==IID_IUnknown||iid==IID_IDropTarget){*value=static_cast<IDropTarget*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef() override {return static_cast<ULONG>(InterlockedIncrement(&references_));}
    ULONG STDMETHODCALLTYPE Release() override {const LONG count=InterlockedDecrement(&references_);if(!count)delete this;return static_cast<ULONG>(count);}
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data,DWORD keys,POINTL point,DWORD* effect) override {return Select(data,keys,point,effect);}
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys,POINTL point,DWORD* effect) override {
        const int index=HitVisualIcon(point.x-GetSystemMetrics(SM_XVIRTUALSCREEN),point.y-GetSystemMetrics(SM_YVIRTUALSCREEN));
        const std::wstring next=index>=0?g_visualIcons[index].path:L"";
        const std::wstring key=next.empty()?L"<desktop-background>":next;
        if(key!=path_)return Select(data_.Get(),keys,point,effect);
        if(!target_){if(effect)*effect=DROPEFFECT_NONE;return S_OK;}return target_->DragOver(keys,point,effect);
    }
    HRESULT STDMETHODCALLTYPE DragLeave() override {Clear(true);return S_OK;}
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data,DWORD keys,POINTL point,DWORD* effect) override {
        const HRESULT result=target_?target_->Drop(data,keys,point,effect):S_OK;
        if(!target_&&effect)*effect=DROPEFFECT_NONE;Clear(false);PostMessageW(g_manager,WM_APP+12,0,0);return result;
    }
};
void RefreshIconHit(VisualIcon& item) {
    const LONG padding=max(0L,(g_snapshot.spacing.x-item.size)/2);
    item.hit={item.position.x-padding,item.position.y,item.position.x-padding+g_snapshot.spacing.x,item.position.y+g_snapshot.spacing.y};
    item.label={item.hit.left,item.position.y+item.size+3,item.hit.right,item.hit.bottom};
}
void ApplyIconDragOffset(POINT offset) {
    for(auto& item:g_visualIcons) {
        const auto start=std::find_if(g_iconDragStart.begin(),g_iconDragStart.end(),
            [&](const auto& value){return value.first==item.path;});
        if(start==g_iconDragStart.end())continue;
        item.position=ClampDesktopPosition({start->second.x+offset.x,start->second.y+offset.y});
        RefreshIconHit(item);
    }
}
void ClearSelections() {
    for(auto& item:g_visualIcons)item.selected=false;
    for(auto& box:g_layout->boxes)box.selected=false;
    g_focusIcon=-1;
}
bool DesktopPointerActive() {
    if(!g_background||!IsWindowVisible(g_background))return false;
    // The owned desktop does not activate on click. Keep keyboard shortcuts
    // with the app's focused settings controls or widget editors.
    if(HWND focus=GetFocus()) {
        if(IsChild(g_background,focus))return false;
        const HWND root=GetAncestor(focus,GA_ROOT);
        if(FindWidgetWindow(root)||db::ClassOf(root)==L"nestlone-D.Settings")return false;
    }
    POINT cursor{};GetCursorPos(&cursor);
    HWND underCursor=WindowFromPoint(cursor);
    return underCursor==g_background||IsChild(g_background,underCursor);
}
void RequestDeleteSelection() {
    if(!g_pendingDelete.empty())return;
    for(const auto& item:g_visualIcons)if(item.selected&&GetFileAttributesW(item.path.c_str())!=INVALID_FILE_ATTRIBUTES)g_pendingDelete.push_back(item.path);
    if(!g_pendingDelete.empty())PostMessageW(g_manager,WM_APP+13,0,0);
}
std::vector<std::wstring> SelectedVisualPaths() {
    std::vector<std::wstring> paths;for(const auto& item:g_visualIcons)if(item.selected)paths.push_back(item.path);return paths;
}
void DeleteSelection() {
    std::vector<std::wstring> paths=std::move(g_pendingDelete);g_pendingDelete.clear();
    if(paths.empty())return;
    std::wstring source;for(const auto& path:paths){source+=path;source.push_back(L'\0');}source.push_back(L'\0');
    SHFILEOPSTRUCTW operation{};operation.hwnd=g_background;operation.wFunc=FO_DELETE;operation.pFrom=source.c_str();operation.fFlags=FOF_ALLOWUNDO|FOF_WANTNUKEWARNING;
    if(SHFileOperationW(&operation)==0&&!operation.fAnyOperationsAborted)PostMessageW(g_manager,WM_APP+12,0,0);
}
LRESULT CALLBACK KeyboardProc(int code,WPARAM wParam,LPARAM lParam) {
    if(code==HC_ACTION&&(wParam==WM_KEYDOWN||wParam==WM_SYSKEYDOWN)&&!g_edit&&g_visible&&DesktopPointerActive()) {
        const auto* key=reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
        const bool ctrl=(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
        const bool nativeShortcut=key->vkCode==VK_DELETE||(ctrl&&(key->vkCode=='A'||key->vkCode=='C'||key->vkCode=='X'||key->vkCode=='D'||key->vkCode=='Z'||key->vkCode=='Y'));
        if(nativeShortcut&&ForwardDesktopShortcut(SelectedVisualPaths(),key->vkCode)) {if(ctrl&&key->vkCode=='A'){for(auto& icon:g_visualIcons)icon.selected=true;PaintAll();}return 1;}
        if(key->vkCode==VK_F2) {
            auto selected=std::find_if(g_visualIcons.begin(),g_visualIcons.end(),[](const auto& icon){return icon.selected;});
            if(selected!=g_visualIcons.end()) {PostMessageW(g_manager,WM_APP+14,0,reinterpret_cast<LPARAM>(new std::wstring(selected->path)));return 1;}
        }
        if(ctrl&&key->vkCode=='V'&&ForwardDesktopShortcut({},key->vkCode))return 1;
    }
    return CallNextHookEx(g_keyboardHook,code,wParam,lParam);
}
void UpdateMarquee(POINT point) {
    g_marqueeRect={min(g_marqueeStart.x,point.x),min(g_marqueeStart.y,point.y),max(g_marqueeStart.x,point.x),max(g_marqueeStart.y,point.y)};
    g_marqueeMoved=abs(point.x-g_marqueeStart.x)>=4||abs(point.y-g_marqueeStart.y)>=4;
    if(!g_marqueeMoved)return;
    if(!g_marqueeCtrl&&!g_marqueeShift)for(auto& item:g_visualIcons)item.selected=false;
    for(auto& item:g_visualIcons){RECT overlap{};bool inside=IntersectRect(&overlap,&g_marqueeRect,&item.hit)!=FALSE;bool initial=std::any_of(g_selectionStart.begin(),g_selectionStart.end(),[&](const auto& path){return _wcsicmp(path.c_str(),item.path.c_str())==0;});if(g_marqueeCtrl)item.selected=initial!=inside;else if(g_marqueeShift)item.selected=initial||inside;else item.selected=inside;}
}
void FinishMarquee() {g_marquee=false;g_marqueeMoved=false;g_selectionStart.clear();g_visualDirty=true;PaintAll();}
void CancelIconGesture(bool releaseCapture) {
    if(g_iconMoved)g_visualGeometryDirty=true;
    EndIconDragPreview();
    g_marquee=false;g_marqueeMoved=false;g_iconDrag=-1;g_iconMoved=false;
    g_iconDragStart.clear();g_selectionStart.clear();
    if(releaseCapture&&GetCapture())ReleaseCapture();
    PaintAll();
}
void ShowShellMenu(HWND owner,const std::wstring& path,POINT screen) {
    PIDLIST_ABSOLUTE pidl=nullptr;ComPtr<IShellFolder> parent;PCUITEMID_CHILD child=nullptr;
    if(FAILED(SHParseDisplayName(path.c_str(),nullptr,&pidl,0,nullptr))||!pidl)return;
    if(SUCCEEDED(SHBindToParent(pidl,IID_PPV_ARGS(&parent),&child))) {
        ComPtr<IContextMenu> menu; if(SUCCEEDED(parent->GetUIObjectOf(owner,1,&child,IID_IContextMenu,nullptr,reinterpret_cast<void**>(menu.GetAddressOf())))) {
            HMENU popup=CreatePopupMenu();if(SUCCEEDED(menu->QueryContextMenu(popup,0,1,0x7fff,CMF_NORMAL))) {
                int command=TrackPopupMenu(popup,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,owner,nullptr);
                if(command){
                    char verb[64]{};
                    const bool rename=SUCCEEDED(menu->GetCommandString(command-1,GCS_VERBA,nullptr,verb,sizeof(verb)))&&_stricmp(verb,"rename")==0;
                    if(rename)BeginItemRename(path);
                    else {CMINVOKECOMMANDINFOEX invoke{};invoke.cbSize=sizeof(invoke);invoke.fMask=CMIC_MASK_UNICODE;invoke.hwnd=owner;invoke.lpVerb=MAKEINTRESOURCEA(command-1);invoke.lpVerbW=MAKEINTRESOURCEW(command-1);invoke.nShow=SW_SHOWNORMAL;menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke));PostMessageW(g_manager,WM_APP+12,0,0);}
                }
            } DeleteMenu(popup,0,MF_BYPOSITION);DestroyMenu(popup);
        }
    } CoTaskMemFree(pidl);
}
struct ShellDropOperation {ComPtr<IDataObject> data;ComPtr<IDropTarget> target;POINTL point{};DWORD agreedEffect=0;std::vector<std::wstring> paths;};
// Explorer performs cross-window file drags through OLE.  The canvas owns the
// hit testing for desktop icons, so use a minimal source object when the
// pointer leaves the canvas and hand Explorer's IDataObject to DoDragDrop.
class DesktopDragSource final : public IDropSource {
    LONG references_=1;
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** value) override {
        if(!value)return E_POINTER;*value=nullptr;
        if(iid==IID_IUnknown||iid==IID_IDropSource){*value=static_cast<IDropSource*>(this);AddRef();return S_OK;}
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return static_cast<ULONG>(InterlockedIncrement(&references_));}
    ULONG STDMETHODCALLTYPE Release() override {const LONG count=InterlockedDecrement(&references_);if(!count)delete this;return static_cast<ULONG>(count);}
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape,DWORD keys) override {
        if(escape)return DRAGDROP_S_CANCEL;
        return (keys&MK_LBUTTON)?S_OK:DRAGDROP_S_DROP;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {return DRAGDROP_S_USEDEFAULTCURSORS;}
};
bool IsCanvasSurfaceWindow(HWND window) {
    return window&&(window==g_background||IsChild(g_background,window));
}
void RemoveMovedDesktopItems(const std::vector<std::wstring>& paths);
void BeginExternalShellDrag() {
    std::vector<std::wstring> paths;
    for(const auto& item:g_iconDragStart)paths.push_back(item.first);
    if(paths.empty())return;
    ComPtr<IDataObject> data;
    const HRESULT create=CreateDesktopDataObject(paths,data.GetAddressOf());
    if(FAILED(create)){db::LogF("[drag] desktop data object unavailable: 0x%08lx",static_cast<unsigned long>(create));return;}
    const bool hadPreview=g_iconDragPreview!=nullptr;
    EndIconDragPreview();
    g_iconDrag=-1;g_iconMoved=false;g_iconDragStart.clear();
    if(GetCapture())ReleaseCapture();
    if(hadPreview)PaintWindow(g_background);
    auto* source=new DesktopDragSource();DWORD effect=DROPEFFECT_COPY|DROPEFFECT_MOVE|DROPEFFECT_LINK;
    const HRESULT result=DoDragDrop(data.Get(),source,effect,&effect);
    source->Release();
    if(SUCCEEDED(result)&&(effect&DROPEFFECT_MOVE)) {
        // The target owns the filesystem operation.  Hide the source visuals
        // immediately and let the regular shell refresh confirm the move.
        MarkPendingShellDrops(paths);RemoveMovedDesktopItems(paths);
    }
    g_visualDirty=true;PaintAll();
    PostMessageW(g_manager,WM_APP+12,0,0);
}
void RemoveMovedDesktopItems(const std::vector<std::wstring>& paths) {
    for(auto& box:g_layout->boxes)box.items.erase(std::remove_if(box.items.begin(),box.items.end(),[&](const auto& item){return std::any_of(paths.begin(),paths.end(),[&](const auto& path){return _wcsicmp(path.c_str(),item.c_str())==0;});}),box.items.end());
    g_layout->desktop.erase(std::remove_if(g_layout->desktop.begin(),g_layout->desktop.end(),[&](const auto& item){return std::any_of(paths.begin(),paths.end(),[&](const auto& path){return _wcsicmp(path.c_str(),item.path.c_str())==0;});}),g_layout->desktop.end());
    SaveLayout(*g_layout);
}
std::unique_ptr<ShellDropOperation> BeginDesktopShellDrop(const std::vector<std::wstring>& paths,const std::wstring& destination,POINT screen) {
    ComPtr<IDataObject> data;const HRESULT dataResult=CreateDesktopDataObject(paths,data.GetAddressOf());
    auto target=ShellDropTargetFor(destination);if(FAILED(dataResult)||!target)return {};
    POINTL point{screen.x,screen.y};DWORD effect=DROPEFFECT_COPY|DROPEFFECT_MOVE|DROPEFFECT_LINK;
    const HRESULT enter=target->DragEnter(data.Get(),MK_LBUTTON,point,&effect);
    const HRESULT over=SUCCEEDED(enter)&&effect!=DROPEFFECT_NONE?target->DragOver(MK_LBUTTON,point,&effect):E_FAIL;
    if(FAILED(over)||effect==DROPEFFECT_NONE){target->DragLeave();db::LogF("[drop] target rejected source=0x%08lx enter=0x%08lx over=0x%08lx effect=%lu",static_cast<unsigned long>(dataResult),static_cast<unsigned long>(enter),static_cast<unsigned long>(over),static_cast<unsigned long>(effect));return {};}
    auto operation=std::make_unique<ShellDropOperation>();operation->data=std::move(data);operation->target=std::move(target);operation->point=point;operation->agreedEffect=effect;operation->paths=paths;return operation;
}
bool CompleteDesktopShellDrop(ShellDropOperation& operation) {
    // Some Shell targets (notably Recycle Bin) accept the transfer but write
    // DROPEFFECT_NONE on return. The negotiated DragOver effect is the stable
    // result; only a failing HRESULT means the optimistic visual must revert.
    DWORD reportedEffect=operation.agreedEffect;
    const HRESULT drop=operation.target->Drop(operation.data.Get(),0,operation.point,&reportedEffect);
    if(FAILED(drop)){db::LogF("[drop] completion failed drop=0x%08lx effect=%lu",static_cast<unsigned long>(drop),static_cast<unsigned long>(reportedEffect));ClearPendingShellDrops(operation.paths);g_visualDirty=true;PaintAll();return false;}
    if(operation.agreedEffect&DROPEFFECT_MOVE)RemoveMovedDesktopItems(operation.paths);
    PostMessageW(g_manager,WM_APP+12,0,0);return true;
}
void FinishVisualDrag() {
    if(g_iconDrag<0||g_iconDrag>=static_cast<int>(g_visualIcons.size()))return;
    const auto& anchor=g_visualIcons[g_iconDrag];
    POINT center{anchor.position.x+g_snapshot.iconSize/2,anchor.position.y+g_snapshot.iconSize/2};
    int target=-1;
    // Group tabs share one rectangle.  A drop belongs to the tab currently
    // visible in that rectangle, not to whichever grouped child is last in
    // the layout vector.
    for(auto& root:g_layout->boxes)if(IsGroupRoot(root)) {
        RECT r=root.rect;r.top+=BoxHeaderHeight(root);
        if(!root.collapsed&&PtInRect(&r,center)) {
            Box* active=ActiveBox(root);
            target=static_cast<int>(active-&g_layout->boxes.front());
            break;
        }
    }
    std::vector<int> affected;std::vector<std::pair<std::wstring,POINT>> dragged;
    for(const auto& start:g_iconDragStart) {
        auto found=std::find_if(g_visualIcons.begin(),g_visualIcons.end(),[&](const auto& item){return _wcsicmp(item.path.c_str(),start.first.c_str())==0;});
        if(found==g_visualIcons.end())continue;
        dragged.push_back({found->path,found->position});
        for(int i=0;i<static_cast<int>(g_layout->boxes.size());++i)if(HasPath(g_layout->boxes[i],start.first)){affected.push_back(i);break;}
    }
    std::vector<std::wstring> paths;for(const auto& item:dragged)paths.push_back(item.first);
    // A desktop icon drag starts inside our canvas, not in Explorer's hidden
    // list view.  Detect a non-dragged icon under the drop point and delegate
    // to its Shell target so folders, applications and Recycle Bin retain
    // their normal drop semantics.
    const auto nativeTarget=std::find_if(g_visualIcons.rbegin(),g_visualIcons.rend(),[&](const auto& icon){
        const bool moving=std::any_of(paths.begin(),paths.end(),[&](const auto& path){return _wcsicmp(path.c_str(),icon.path.c_str())==0;});
        return !moving&&PtInRect(&icon.hit,center);
    });
    if(nativeTarget!=g_visualIcons.rend()) {
        POINT screen{center.x+GetSystemMetrics(SM_XVIRTUALSCREEN),center.y+GetSystemMetrics(SM_YVIRTUALSCREEN)};
        if(auto operation=BeginDesktopShellDrop(paths,nativeTarget->path,screen)) {
            if(operation->agreedEffect&DROPEFFECT_MOVE)MarkPendingShellDrops(paths);
            g_visualDirty=true;g_iconDrag=-1;g_iconDragStart.clear();PaintAll();
            PostMessageW(g_manager,WM_APP+15,0,reinterpret_cast<LPARAM>(operation.release()));return;
        }
    }
    for(auto& box:g_layout->boxes) {
        box.items.erase(std::remove_if(box.items.begin(),box.items.end(),[&](const auto& item){
            return std::any_of(paths.begin(),paths.end(),[&](const auto& path){return _wcsicmp(path.c_str(),item.c_str())==0;});
        }),box.items.end());
    }
    if(target>=0) {
        auto& targetBox=g_layout->boxes[target];
        LONG slot=BoxSlot(targetBox,center);
        slot=min(slot,static_cast<LONG>(targetBox.items.size()));
        targetBox.items.insert(targetBox.items.begin()+slot,paths.begin(),paths.end());
        g_layout->desktop.erase(std::remove_if(g_layout->desktop.begin(),g_layout->desktop.end(),[&](const auto& placement){
            return std::any_of(paths.begin(),paths.end(),[&](const auto& path){return _wcsicmp(path.c_str(),placement.path.c_str())==0;});
        }),g_layout->desktop.end());
        affected.push_back(target);
    } else {
        dragged=SnapDesktopDrop(dragged);
        for(const auto& item:dragged)AssignDesktopItem(*g_layout,item.first,-1,item.second);
    }
    std::sort(affected.begin(),affected.end());affected.erase(std::unique(affected.begin(),affected.end()),affected.end());
    std::vector<DesktopMove> moves;
    for(int index:affected) {
        auto& box=g_layout->boxes[index];if(box.collapsed)continue;
        box.rect=FitGrid(box,g_snapshot);SyncGroupRect(box);auto arranged=GridMoves(box,g_snapshot);moves.insert(moves.end(),arranged.begin(),arranged.end());
    }
    if(target<0)for(const auto& item:dragged)moves.push_back({item.first,item.second});
    if(!moves.empty())QueueMoves(moves);
    g_visualDirty=true;g_iconDrag=-1;g_iconDragStart.clear();
    PaintAll();
    QueueLayoutSave();
}
bool PrimeSurface() {
    if(!g_background||!IsWindow(g_background))return false;
    g_visualDirty=true;
    RebuildVisualIcons();
    if(!PaintWindow(g_background))return false;
    PaintAll();
    return true;
}
void ShowPreparedSurface() {
    if(!g_visible)return;
    SyncWidgets();
    UpdateSurfaceVisibility();
}
bool HideNativeDesktopAfterSurfaceIsReady() {
    if(!g_visible)return true;
    if(!IsWindow(g_host.listview))return false;
    g_hiddenListview=g_host.listview;
    ClaimHiddenDesktopListView(g_hiddenListview);
    ShowWindow(g_hiddenListview,SW_HIDE);
    if(IsWindowVisible(g_hiddenListview)) {
        ReleaseHiddenDesktopListView(g_hiddenListview);
        g_hiddenListview=nullptr;
        return false;
    }
    g_nativeHidden=true;
    return true;
}
bool Attach() {
    // Foreground changes only require ordering our window. Do not enumerate
    // every Explorer child or synchronously query its ListView every 350 ms.
    if(IsWindow(g_background)&&IsWindow(g_host.listview)&&IsWindow(g_host.defviewParent)&&g_nativeHidden) {
        UpdateSurfaceVisibility();return true;
    }
    auto host=db::DiscoverDesktopHost();
    if(!host.listview || !host.defviewParent){db::LogF("[canvas] attach deferred: desktop host unavailable");return false;}
    if(g_background && IsWindow(g_background) && g_host.listview==host.listview) {
        if(!g_visible||g_nativeHidden){UpdateSurfaceVisibility();return true;}
        if(!PrimeSurface())return false;
        ShowPreparedSurface();
        // A hidden layered window can accept UpdateLayeredWindow without
        // presenting pixels.  Re-prime after ShowWindow before hiding Explorer.
        if(!PrimeSurface()||!HideNativeDesktopAfterSurfaceIsReady()) {
            DestroyDecorations();
            return false;
        }
        return true;
    }
    DestroyDecorations();g_host=host;
    RestoreLegacyMask(host.listview);RecoverBoxPositions();
    DesktopSnapshot initial;
    if(!ReadDesktop(initial)||!initial.readable||initial.listview!=host.listview){
        db::LogF("[canvas] attach deferred: desktop snapshot unavailable (%s)",db::ToUtf8(initial.error).c_str());
        return false;
    }
    g_snapshot=std::move(initial);g_visualDirty=true;
    POINT origin{GetSystemMetrics(SM_XVIRTUALSCREEN),GetSystemMetrics(SM_YVIRTUALSCREEN)};
    g_background=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
        L"nestlone-D.Decoration",L"",WS_POPUP,origin.x,origin.y,GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN),nullptr,nullptr,g_instance,nullptr);
    if(!g_background){DestroyDecorations();return false;}
    auto* dropTarget=new CanvasDropTarget();
    g_dropRegistered=SUCCEEDED(RegisterDragDrop(g_background,dropTarget));
    dropTarget->Release();
    if(!g_dropRegistered)db::LogF("[canvas] OLE drop target unavailable: %lu",GetLastError());
    // This is the owned desktop surface. Explorer's list view is hidden while
    // the surface is alive; file paths remain unchanged.
    SetWindowPos(g_background,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    for(const auto& box:g_layout->boxes)if(IsGroupRoot(box)) {
        auto d=std::make_unique<Decoration>();d->id=box.id;
        d->header=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"nestlone-D.Decoration",L"",SurfaceWindowStyle(),0,0,200,HeaderHeight(),SurfaceParent(),nullptr,g_instance,d.get());
        d->gripLeft=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"nestlone-D.Decoration",L"",SurfaceWindowStyle(),0,0,18,18,SurfaceParent(),nullptr,g_instance,d.get());
        d->grip=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"nestlone-D.Decoration",L"",SurfaceWindowStyle(),0,0,18,18,SurfaceParent(),nullptr,g_instance,d.get());
        if(!d->header||!d->gripLeft||!d->grip){if(d->header)DestroyWindow(d->header);if(d->gripLeft)DestroyWindow(d->gripLeft);if(d->grip)DestroyWindow(d->grip);DestroyDecorations();return false;}
        SetWindowPos(d->header,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(d->gripLeft,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(d->grip,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        g_decorations.push_back(std::move(d));
    }
    if(!PrimeSurface()){db::LogF("[canvas] attach failed: initial surface paint");DestroyDecorations();return false;}
    if(g_visible) {
        ShowPreparedSurface();
        if(!PrimeSurface()||!HideNativeDesktopAfterSurfaceIsReady()) {db::LogF("[canvas] attach failed: surface presentation");DestroyDecorations();return false;}
    }
    return true;
}
void Rebuild(){
    RecoverBoxPositions();
    if(!g_background||!IsWindow(g_background)){Attach();return;}
    // Keep the desktop canvas, Explorer binding and unaffected group windows.
    for(auto it=g_decorations.begin();it!=g_decorations.end();) {
        auto* box=FindBox((*it)->id);
        if(box&&IsGroupRoot(*box)){++it;continue;}
        if(IsWindow((*it)->header))DestroyWindow((*it)->header);
        if(IsWindow((*it)->gripLeft))DestroyWindow((*it)->gripLeft);
        if(IsWindow((*it)->grip))DestroyWindow((*it)->grip);
        it=g_decorations.erase(it);
    }
    for(const auto& box:g_layout->boxes)if(IsGroupRoot(box)) {
        if(std::any_of(g_decorations.begin(),g_decorations.end(),[&](const auto& d){return d->id==box.id;}))continue;
        auto d=std::make_unique<Decoration>();d->id=box.id;
        d->header=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"nestlone-D.Decoration",L"",SurfaceWindowStyle(),0,0,200,HeaderHeight(),SurfaceParent(),nullptr,g_instance,d.get());
        d->gripLeft=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"nestlone-D.Decoration",L"",SurfaceWindowStyle(),0,0,18,18,SurfaceParent(),nullptr,g_instance,d.get());
        d->grip=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"nestlone-D.Decoration",L"",SurfaceWindowStyle(),0,0,18,18,SurfaceParent(),nullptr,g_instance,d.get());
        if(!d->header||!d->gripLeft||!d->grip){if(d->header)DestroyWindow(d->header);if(d->gripLeft)DestroyWindow(d->gripLeft);if(d->grip)DestroyWindow(d->grip);continue;}
        SetWindowPos(d->header,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(d->gripLeft,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        SetWindowPos(d->grip,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        g_decorations.push_back(std::move(d));
    }
    g_visualDirty=true;PaintAll();SyncWidgets();SaveLayout(*g_layout);
}
void ToggleBoxView(Box& box) {box.iconView=!box.iconView;box.listScroll=0;SyncGroupRect(box);SaveLayout(*g_layout);g_visualDirty=true;PaintAll();}
void CreateBoxAt(POINT point) {
    Box box;box.id=std::to_wstring(GetTickCount64());box.title=L"新盒子";
    box.color=g_layout->defaultBoxColor;
    box.rect={point.x,point.y,point.x+g_layout->defaultBoxWidth,point.y+g_layout->defaultBoxHeight};
    g_layout->boxes.push_back(std::move(box));Rebuild();
}
void Menu(HWND hwnd,Box& box,POINT point) {
    Box* active=ActiveBox(box);
    auto tabs=GroupTabs(*GroupRoot(box));Box* orderTab=active;
    POINT local{point.x-GetSystemMetrics(SM_XVIRTUALSCREEN)-box.rect.left,point.y-GetSystemMetrics(SM_YVIRTUALSCREEN)-box.rect.top};
    if(tabs.size()>1&&local.y>=HeaderHeight()&&local.y<2*HeaderHeight()&&local.x>=0&&local.x<box.rect.right-box.rect.left)
        orderTab=tabs[min(tabs.size()-1,static_cast<size_t>(local.x*tabs.size()/max(1L,box.rect.right-box.rect.left)))];
    int orderIndex=static_cast<int>(std::find(tabs.begin(),tabs.end(),orderTab)-tabs.begin());
    HMENU menu=CreatePopupMenu();
    if(tabs.size()>1){
        AppendMenuW(menu,MF_STRING|(orderIndex==0?MF_GRAYED:0),8,L"标签向左移动");
        AppendMenuW(menu,MF_STRING|(orderIndex+1==static_cast<int>(tabs.size())?MF_GRAYED:0),9,L"标签向右移动");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    }
    AppendMenuW(menu,MF_STRING,1,L"重命名盒子");AppendMenuW(menu,MF_STRING,2,L"整理盒内图标位置");
    AppendMenuW(menu,MF_STRING|(active->iconView?MF_CHECKED:0),6,L"图标显示");AppendMenuW(menu,MF_STRING|(!active->iconView?MF_CHECKED:0),7,L"列表显示");
    AppendMenuW(menu,MF_STRING,3,L"收纳区域内图标");AppendMenuW(menu,MF_STRING,4,box.collapsed?L"展开盒子":L"折叠盒子（隐藏图标）");
    AppendMenuW(menu,MF_STRING,5,L"删除盒子（保留图标与文件）");
    if(!g_status.empty())AppendMenuW(menu,MF_STRING|MF_DISABLED,0,g_status.c_str());
    int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd,nullptr);DestroyMenu(menu);
    if(command==8||command==9){
        ReorderTab(*orderTab,command==8?orderIndex-1:orderIndex+2);SaveLayout(*g_layout);
        for(auto& decoration:g_decorations)if(decoration->id==GroupRoot(box)->id)PaintWindow(decoration->header,GroupRoot(box));
        return;
    }
    if(command==1)BeginRename(box,HasGroupTabs(box));
    if(command==2){Arrange(box);if(!g_status.empty())MessageBoxW(hwnd,g_status.c_str(),L"图标位置",MB_OK|MB_ICONINFORMATION);}
    if(command==3) {
        int index=static_cast<int>(&box-g_layout->boxes.data());
        for(const auto& entry:g_snapshot.entries)if(ContainingBox(entry)==index)AssignDesktopItem(*g_layout,entry.path,index,entry.position);
        Arrange(box);SaveLayout(*g_layout);
    }
    if(command==6){active->iconView=true;active->listScroll=0;SyncGroupRect(*active);SaveLayout(*g_layout);g_visualDirty=true;PaintAll();}
    if(command==7){active->iconView=false;active->listScroll=0;SyncGroupRect(*active);SaveLayout(*g_layout);g_visualDirty=true;PaintAll();}
    if(command==4){box.collapsed=!box.collapsed;g_visualDirty=true;SaveLayout(*g_layout);PaintAll();}
    if(command==5){auto id=box.id;g_layout->boxes.erase(std::remove_if(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const auto& b){return b.id==id;}),g_layout->boxes.end());PostMessageW(g_manager,WM_APP+11,0,0);}
}
LRESULT CALLBACK DecorationProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    LRESULT shellResult=0;
    if(ForwardShellContextMenuMessage(message,wp,lp,&shellResult))return shellResult;
    if(message==WM_NCDESTROY)g_paintBuffers.erase(hwnd);
    if(message==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(message==WM_NCCREATE){auto c=reinterpret_cast<CREATESTRUCTW*>(lp);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(c->lpCreateParams));}
    auto* d=reinterpret_cast<Decoration*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    Box* box=d?FindBox(d->id):nullptr;
    if(!box) {
        if(message==WM_NCHITTEST)return HTCLIENT;
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_PAINT){PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);if(g_layout)PaintWindow(hwnd);return 0;}
        if(hwnd==g_background && message==WM_LBUTTONDOWN) {
            int hit=HitVisualIcon(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
            bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0,shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
            if(hit<0){if(!ctrl&&!shift)ClearSelections();g_marquee=true;g_marqueeMoved=false;g_marqueeCtrl=ctrl;g_marqueeShift=shift;g_marqueeStart={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};g_selectionStart.clear();for(const auto& item:g_visualIcons)if(item.selected)g_selectionStart.push_back(item.path);SetCapture(hwnd);PaintWindow(hwnd);return 0;}
            bool alreadySelected=g_visualIcons[hit].selected;if(!ctrl&&!shift&&!alreadySelected)ClearSelections();for(auto& candidate:g_layout->boxes)candidate.selected=false;
            if(shift&&g_focusIcon>=0){int first=min(g_focusIcon,hit),last=max(g_focusIcon,hit);if(!ctrl)for(auto& item:g_visualIcons)item.selected=false;for(int i=first;i<=last;++i)g_visualIcons[i].selected=true;}
            else if(ctrl)g_visualIcons[hit].selected=!g_visualIcons[hit].selected;
            else if(!alreadySelected){for(auto& item:g_visualIcons)item.selected=false;g_visualIcons[hit].selected=true;}
            g_focusIcon=hit;g_iconDrag=g_visualIcons[hit].selected?hit:-1;g_iconMoved=false;g_iconDragStart.clear();for(const auto& item:g_visualIcons)if(item.selected)g_iconDragStart.push_back({item.path,item.position});GetCursorPos(&g_iconMouseStart);if(g_iconDrag>=0)SetCapture(hwnd);PaintWindow(hwnd);return 0;
        }
        if(hwnd==g_background && message==WM_MOUSEMOVE && GetCapture()==hwnd) {
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(g_marquee){UpdateMarquee(point);QueueInteractivePaint();return 0;}
            if(g_iconDrag>=0) {
                POINT cursor{};GetCursorPos(&cursor);
                const POINT offset{cursor.x-g_iconMouseStart.x,cursor.y-g_iconMouseStart.y};
                if(!g_iconMoved&&abs(offset.x)<4&&abs(offset.y)<4)return 0;
                if(!IsCanvasSurfaceWindow(WindowFromPoint(cursor))) {
                    BeginExternalShellDrag();
                    return 0;
                }
                if(!g_iconMoved)BeginIconDragPreview();
                g_iconMoved=true;
                if(g_iconDragPreview) {
                    SetWindowPos(g_iconDragPreview,nullptr,g_iconPreviewOrigin.x+offset.x,
                        g_iconPreviewOrigin.y+offset.y,0,0,
                        SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
                } else {
                    ApplyIconDragOffset(offset);
                    QueueInteractivePaint();
                }
                return 0;
            }
        }
        if(hwnd==g_background && message==WM_LBUTTONUP) {
            if(g_marquee){FinishMarquee();ReleaseCapture();return 0;}
            if(g_iconDrag>=0) {
                POINT cursor{};GetCursorPos(&cursor);
                if(g_iconMoved&&!IsCanvasSurfaceWindow(WindowFromPoint(cursor))) {
                    CancelIconGesture();
                    return 0;
                }
                const int anchor=g_iconDrag;
                g_iconDrag=-1;
                ReleaseCapture();
                if(g_iconMoved) {
                    g_iconDrag=anchor;
                    ApplyIconDragOffset({cursor.x-g_iconMouseStart.x,cursor.y-g_iconMouseStart.y});
                    EndIconDragPreview();
                    FinishVisualDrag();
                    g_iconMoved=false;
                } else {
                    g_iconDragStart.clear();
                }
                return 0;
            }
        }
        if(hwnd==g_background&&(message==WM_CAPTURECHANGED||message==WM_CANCELMODE)&&
            (g_iconDrag>=0||g_marquee)) {
            CancelIconGesture(message!=WM_CAPTURECHANGED);
            return 0;
        }
        if(hwnd==g_background && message==WM_LBUTTONDBLCLK) {
            const POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            const int hit=HitVisualIcon(point.x,point.y);
            if(hit>=0) {
                const auto& icon=g_visualIcons[hit];
                if(PtInRect(&icon.label,point)&&(!icon.list||PtInRect(&icon.clip,point)))
                    BeginItemRename(icon.path);
                else
                    ShellExecuteW(nullptr,L"open",icon.path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            }
            return 0;
        }
        if(hwnd==g_background && message==WM_RBUTTONDOWN){int hit=HitVisualIcon(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));if(hit>=0&&!g_visualIcons[hit].selected){for(auto& item:g_visualIcons)item.selected=false;g_visualIcons[hit].selected=true;g_focusIcon=hit;PaintWindow(hwnd);}return 0;}
        if(hwnd==g_background && message==WM_RBUTTONUP){
            int hit=HitVisualIcon(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));POINT screen{};GetCursorPos(&screen);
            if(hit>=0)ShowShellMenu(hwnd,g_visualIcons[hit].path,screen);
            else {
                POINT virtualPoint{screen.x-GetSystemMetrics(SM_XVIRTUALSCREEN),screen.y-GetSystemMetrics(SM_YVIRTUALSCREEN)};bool boxHit=false;
                for(auto& candidate:g_layout->boxes){RECT bounds=DisplayRect(candidate);if(PtInRect(&bounds,virtualPoint)){Menu(hwnd,candidate,screen);boxHit=true;break;}}
                if(!boxHit) {
                    bool createBox=false;
                    if(ShowShellBackgroundMenu(hwnd,screen,&createBox)) {
                        if(createBox)CreateBoxAt(virtualPoint);
                        else PostMessageW(g_manager,WM_APP+12,TRUE,0);
                    }
                }
            }
            return 0;
        }
        if(hwnd==g_background && message==WM_MOUSEWHEEL) {
            if(g_iconDrag>=0)return 0;
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(hwnd,&point);
            for(auto& root:g_layout->boxes)if(IsGroupRoot(root)&&!root.collapsed) {
                Box* active=ActiveBox(root);if(!active||active->iconView)continue;
                const RECT content=BoxContentRect(*active);
                if(!PtInRect(&content,point))continue;
                const int lines=max(1,abs(GET_WHEEL_DELTA_WPARAM(wp))/WHEEL_DELTA)*3;
                const int delta=GET_WHEEL_DELTA_WPARAM(wp)>0?-lines*BoxRowHeight(*active):lines*BoxRowHeight(*active);
                const int next=std::clamp(active->listScroll+delta,0,ListScrollMaximum(*active));
                if(next!=active->listScroll){active->listScroll=next;RefreshVisualGeometry();PaintWindow(hwnd);}
                return 0;
            }
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    switch(message) {
    case WM_NCHITTEST:return HTCLIENT;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);PaintWindow(hwnd,box,hwnd==d->grip||hwnd==d->gripLeft,hwnd==d->gripLeft);return 0;}
    case WM_LBUTTONDBLCLK:if(hwnd==d->header && GET_Y_LPARAM(lp)<HeaderHeight() && GET_X_LPARAM(lp)<box->rect.right-box->rect.left-3*HeaderHeight())BeginRename(*box,HasGroupTabs(*box));return 0;
    case WM_CONTEXTMENU:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(p.x==-1)GetCursorPos(&p);Menu(hwnd,*box,p);return 0;}
    case WM_LBUTTONDOWN: {
        FinishRename(true);int h=HeaderHeight(),x=GET_X_LPARAM(lp),y=GET_Y_LPARAM(lp),width=box->rect.right-box->rect.left;
        if(hwnd==d->header && y<h && x>=width-3*h) {
            if(x>=width-h)g_boxButton=3;
            else if(x>=width-2*h)g_boxButton=2;
            else g_boxButton=1;
            SetCapture(hwnd);return 0;
        }
        if(hwnd==d->header && y>=h && HasGroupTabs(*box)) {
            auto tabs=GroupTabs(*box);
            if(tabs.size()>1) {
                const size_t index=min(tabs.size()-1,static_cast<size_t>(max(0,x)*static_cast<int>(tabs.size())/max(1,width)));
                g_tabPending=true;g_tabInsert=-1;g_dragId=tabs[index]->id;g_boxStart=tabs[index]->rect;GetCursorPos(&g_mouseStart);SetCapture(hwnd);return 0;
            }
        }
        bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        if(!ctrl)for(auto& item:g_visualIcons)item.selected=false;
        if(ctrl){box->selected=!box->selected;if(!box->selected){g_visualDirty=true;PaintAll();}return 0;}
        if(!box->selected)for(auto& candidate:g_layout->boxes)candidate.selected=false;
        box->selected=true;g_drag=true;g_resizing=hwnd==d->grip||hwnd==d->gripLeft;g_resizeFromLeft=hwnd==d->gripLeft;g_dragId=box->id;g_boxStart=box->rect;GetCursorPos(&g_mouseStart);g_dragStart.clear();g_boxDragStart.clear();
        if(HasGroupTabs(*box)) {
            for(const auto& candidate:g_layout->boxes)if(GroupRoot(candidate)==box)g_boxDragStart.push_back({candidate.id,candidate.rect});
        } else {
            for(const auto& candidate:g_layout->boxes)if(candidate.selected)g_boxDragStart.push_back({candidate.id,candidate.rect});
        }
        for(const auto& e:g_snapshot.entries)if(HasPath(*box,e.path))g_dragStart.push_back({e.path,e.position});
        SetCapture(hwnd);return 0;
    }
    case WM_MOUSEMOVE:
        if(g_tabPending&&GetCapture()==hwnd) {
            POINT p{};GetCursorPos(&p);LONG dx=p.x-g_mouseStart.x,dy=p.y-g_mouseStart.y;
            if(g_tabInsert<0&&abs(dx)<GetSystemMetrics(SM_CXDRAG)&&abs(dy)<GetSystemMetrics(SM_CYDRAG))return 0;
            POINT local=p;ScreenToClient(hwnd,&local);
            const int width=box->rect.right-box->rect.left,h=HeaderHeight();
            const int margin=MulDiv(20,GetDpiForWindow(hwnd),96);
            if(local.x>=-margin&&local.x<=width+margin&&local.y>=h-margin&&local.y<=2*h+margin) {
                const int count=static_cast<int>(GroupTabs(*box).size());
                const int insertion=std::clamp((max(0L,local.x)*count+width/2)/max(1,width),0L,static_cast<LONG>(count));
                if(g_tabInsert!=insertion){g_tabInsert=insertion;PaintWindow(hwnd,box);}
                return 0;
            }
            g_tabInsert=-1;
            if(auto* tab=FindBox(g_dragId)) {
                DetachTab(*tab);
                tab->rect=g_boxStart;tab->selected=true;g_boxDragStart={{tab->id,g_boxStart}};g_drag=true;g_tabPending=false;g_tabDetached=true;g_resizing=false;g_visualDirty=true;box=tab;
                Rebuild();
            }
        }
        if(g_drag && GetCapture()==hwnd) {
            if(auto* dragged=FindBox(g_dragId))box=dragged;
            POINT p{};GetCursorPos(&p);LONG dx=p.x-g_mouseStart.x,dy=p.y-g_mouseStart.y;
            if(g_resizing){
                box->rect=g_boxStart;
                if(g_resizeFromLeft)box->rect.left=min(box->rect.right-240,box->rect.left+dx);
                else box->rect.right=max(box->rect.left+240,box->rect.right+dx);
                box->rect.bottom=max(box->rect.top+BoxHeaderHeight(*box)+100,box->rect.bottom+dy);box->rect=FitGrid(*box,g_snapshot);
                if(HasGroupTabs(*box))for(auto& candidate:g_layout->boxes)if(GroupRoot(candidate)==box)candidate.rect=box->rect;g_visualGeometryDirty=true;
            }
            else {
                RECT desired=g_boxStart;OffsetRect(&desired,dx,dy);
                const RECT constrained=KeepBoxOnScreen(desired,BoxHeaderHeight(*box));
                if(EqualRect(&constrained,&box->rect)&&g_boxDragStart.size()==1)return 0;
                if(!g_dragPreview)BeginDragPreview(*box);
                dx=constrained.left-g_boxStart.left;dy=constrained.top-g_boxStart.top;
                for(const auto& start:g_boxDragStart)if(auto* selected=FindBox(start.first)) {
                    if(selected==box)selected->rect=constrained;
                    else {
                        RECT position=start.second;OffsetRect(&position,dx,dy);
                        selected->rect=KeepBoxOnScreen(position,BoxHeaderHeight(*selected));
                    }
                    SyncGroupRect(*selected);
                }
                if(g_nativeHidden){g_visualGeometryDirty=true;}else {std::vector<DesktopMove> moves=g_dragStart;for(auto& m:moves){m.position.x+=dx;m.position.y+=dy;}if(!moves.empty())QueueMoves(moves);}
            }
            if(g_dragPreview)MoveDragPreview(*box);else QueueInteractivePaint();
        }
        return 0;
    case WM_CANCELMODE:
        EndDragPreview();
        g_boxButton=0;g_drag=false;g_tabPending=false;g_tabInsert=-1;g_tabDetached=false;g_resizing=false;g_resizeFromLeft=false;g_boxDragStart.clear();CancelIconGesture();return 0;
    case WM_LBUTTONUP:
        if(g_tabPending){
            const int insertion=g_tabInsert;g_tabPending=false;g_tabInsert=-1;ReleaseCapture();
            if(auto* tab=FindBox(g_dragId)) {
                if(insertion>=0){ReorderTab(*tab,insertion);SaveLayout(*g_layout);PaintWindow(hwnd,box);}
                else if(auto* root=GroupRoot(*tab)){root->activeTabId=tab->id;g_visualDirty=true;SaveLayout(*g_layout);PaintAll();}
            }
            return 0;
        }
        if(g_boxButton){int button=g_boxButton;g_boxButton=0;ReleaseCapture();if(button==1)ToggleBoxView(*ActiveBox(*box));else if(button==2){box->collapsed=!box->collapsed;g_visualDirty=true;SaveLayout(*g_layout);PaintAll();}else {auto id=box->id;g_layout->boxes.erase(std::remove_if(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const auto& b){return b.id==id;}),g_layout->boxes.end());PostMessageW(g_manager,WM_APP+11,0,0);}return 0;}
        if(g_drag){
            if(auto* dragged=FindBox(g_dragId))box=dragged;
            g_drag=false;ReleaseCapture();EndDragPreview();
            if(g_resizing){FlushInteractivePaint();Arrange(*box);}
            else {
                POINT cursor{};GetCursorPos(&cursor);cursor.x-=GetSystemMetrics(SM_XVIRTUALSCREEN);cursor.y-=GetSystemMetrics(SM_YVIRTUALSCREEN);
                Box* source=GroupRoot(*box);Box* destination=nullptr;
                for(auto& candidate:g_layout->boxes)if(IsGroupRoot(candidate)&&candidate.id!=source->id) {
                    RECT header=DisplayRect(candidate);header.bottom=header.top+BoxHeaderHeight(candidate);
                    if(PtInRect(&header,cursor)){destination=&candidate;break;}
                }
                if(destination) {
                    if(destination->groupTitle.empty())destination->groupTitle=destination->title;
                    const auto members=GroupTabs(*source);
                    auto existing=GroupTabs(*destination);int order=0;
                    for(auto* member:existing)member->tabOrder=order++;
                    for(auto* member:members)member->tabOrder=order++;
                    for(auto* candidate:members) {candidate->groupId=destination->id;candidate->rect=destination->rect;candidate->color=destination->color;}
                    destination->activeTabId=box->id;
                    g_visualDirty=true;
                    g_tabDetached=false;
                    g_boxDragStart.clear();
                    // Rebuild presents and saves the merged state in one pass.
                    if(!PostMessageW(g_manager,WM_APP+11,0,0))Rebuild();
                    return 0;
                }
                FlushInteractivePaint();
            }
            const bool rebuildDecorations=g_tabDetached;g_tabDetached=false;g_resizeFromLeft=false;
            g_boxDragStart.clear();SaveLayout(*g_layout);
            if(rebuildDecorations)PostMessageW(g_manager,WM_APP+11,0,0);
        }return 0;
    case WM_CAPTURECHANGED:
        if(g_tabPending){g_tabPending=false;g_tabInsert=-1;PaintWindow(hwnd,box);}
        if(g_drag){g_drag=false;EndDragPreview();if(g_resizing)Arrange(*box);g_resizing=false;g_resizeFromLeft=false;g_boxDragStart.clear();SaveLayout(*g_layout);FlushInteractivePaint();}return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
LRESULT CALLBACK ManagerProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==kFinishRenameMessage){if(g_edit==reinterpret_cast<HWND>(lp))FinishRename(true);return 0;}
    if(message==WM_APP+11){Rebuild();return 0;}
    if(message==WM_APP+13){DeleteSelection();return 0;}
    if(message==WM_APP+14){std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(lp));if(path)BeginItemRename(*path);return 0;}
    if(message==WM_APP+15){std::unique_ptr<ShellDropOperation> operation(reinterpret_cast<ShellDropOperation*>(lp));if(operation)CompleteDesktopShellDrop(*operation);return 0;}
    if(message==WM_APP+16){if(PasteDesktopClipboard(g_background))PostMessageW(hwnd,WM_APP+12,0,0);return 0;}
    if(message==WM_APP+21){UpdateSurfaceVisibility();return 0;}
    if(message==WM_APP+12){
        // Explorer may commit a background-menu command (New, View size,
        // Refresh) after the menu closes. Poll briefly so the owned canvas
        // receives the same change notification it would have received from
        // Explorer's visible ListView.
        // New and pasted items should appear promptly. Explorer may still be
        // committing the operation, so keep a brief high-frequency window.
        g_forceShellRepaint|=wp!=0;
        g_shellRefreshAttempts=g_pendingShellDrops.empty()?24:40;SetTimer(hwnd,kShellRefreshTimer,g_pendingShellDrops.empty()?40:50,nullptr);return 0;
    }
    if(message==WM_TIMER&&wp==kInteractivePaintTimer) {
        KillTimer(hwnd,kInteractivePaintTimer);g_interactivePaintQueued=false;
        if(g_visible){
            if(g_drag&&!g_dragPreview)PaintAll();
            else if((g_marquee||(g_iconDrag>=0&&!g_iconDragPreview))&&g_background)PaintWindow(g_background);
        }
        return 0;
    }
    const auto gestureActive=[] {
        return g_drag||g_tabPending||g_marquee||g_iconDrag>=0||
            std::any_of(g_widgets.begin(),g_widgets.end(),[](const auto& w){return w->dragging||w->resizing;});
    };
    if(message==WM_TIMER&&wp==kLayoutSaveTimer) {
        if(!gestureActive())FinishQueuedLayoutSave(true);
        return 0;
    }
    if(message==WM_TIMER&&wp==kShellRefreshTimer) {
        if(!g_visible||g_shellRefreshAttempts<=0) {
            KillTimer(hwnd,kShellRefreshTimer);
            g_forceShellRepaint=false;
            return 0;
        }
        // A refresh can rebuild the icons while their drag is still active.
        // Preserve the remaining refresh attempts until the gesture ends.
        if(gestureActive())return 0;
        --g_shellRefreshAttempts;
        DesktopSnapshot next;
        if(PollDesktop(next)&&next.readable) {
            const bool forceRepaint=g_forceShellRepaint;
            g_forceShellRepaint=false;
            ApplyDesktopSnapshot(std::move(next),forceRepaint);
        }
        return 0;
    }
    if(message==WM_TIMER) {
        // Host discovery enumerates Explorer windows and sends synchronous
        // messages. Defer maintenance while the pointer gesture is active.
        if(gestureActive())return 0;
        Attach();
        if(!g_visible)return 0;
        DesktopSnapshot next;
        if(PollDesktop(next)) {
            if(!next.error.empty())g_status=next.error;
            if(next.readable) {
                bool settling=g_pending!=0;
                if(settling && next.applied>=g_pending){g_pending=0;g_moves.clear();g_status=next.error;}
                if(!g_nativeHidden && !settling && !g_drag && !(GetAsyncKeyState(VK_LBUTTON)&0x8000) && !(GetAsyncKeyState(VK_RBUTTON)&0x8000))Observe(g_snapshot,next);
                // Keep the pre-drag baseline until Explorer has finished native drag/drop.
                if(!(GetAsyncKeyState(VK_LBUTTON)&0x8000) && !(GetAsyncKeyState(VK_RBUTTON)&0x8000)){
                    ApplyDesktopSnapshot(std::move(next));
                }
            }
        }
        return 0;
    }
    if(message==WM_DISPLAYCHANGE||message==WM_DPICHANGED){CanvasNotifyDesktopHostChanged();return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
bool CreateCanvas(HINSTANCE instance,HWND,Layout* layout) {
    g_instance=instance;g_layout=layout;g_visible=true;
    Gdiplus::GdiplusStartupInput input;if(Gdiplus::GdiplusStartup(&g_token,&input,nullptr)!=Gdiplus::Ok)return false;
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DecorationProc;wc.lpszClassName=L"nestlone-D.Decoration";wc.style=CS_DBLCLKS;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    wc.lpfnWndProc=ManagerProc;wc.lpszClassName=L"nestlone-D.PositionRules";RegisterClassW(&wc);
    wc.lpfnWndProc=WidgetProc;wc.lpszClassName=L"nestlone-D.Widget";wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));RegisterClassW(&wc);
    g_manager=CreateWindowExW(WS_EX_TOOLWINDOW,L"nestlone-D.PositionRules",L"",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    if(!g_manager)return false;
    g_keyboardHook=SetWindowsHookExW(WH_KEYBOARD_LL,KeyboardProc,instance,0);
    g_foregroundHook=SetWinEventHook(EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND,nullptr,ForegroundProc,0,0,WINEVENT_OUTOFCONTEXT);
    SetTimer(g_manager,1,350,nullptr);Attach();PollDesktop(g_snapshot);return true;
}
void DestroyCanvas() {
    FinishQueuedLayoutSave(true);
    g_forceShellRepaint=false;
    CancelDesktopMoves();DestroyDecorations();if(g_manager)DestroyWindow(g_manager);g_manager=nullptr;
    if(g_keyboardHook){UnhookWindowsHookEx(g_keyboardHook);g_keyboardHook=nullptr;}
    if(g_foregroundHook){UnhookWinEvent(g_foregroundHook);g_foregroundHook=nullptr;}
    g_interactivePaintQueued=false;g_paintBuffers.clear();g_iconCache.clear();
    if(g_editFont){DeleteObject(g_editFont);g_editFont=nullptr;}
    if(g_token){Gdiplus::GdiplusShutdown(g_token);g_token=0;}
}
bool CanvasVisible(){return g_visible;}
RECT NewNoteRect(const RECT& work,const RECT& settings,LONG offset) {
    constexpr LONG gap=16;
    const LONG width=min(280L,work.right-work.left),height=min(220L,work.bottom-work.top);
    const LONG y=std::clamp(settings.top+offset,work.top,work.bottom-height);
    const LONG x=std::clamp(settings.left+offset,work.left,work.right-width);
    if(settings.right+gap+width<=work.right)
        return {settings.right+gap,y,settings.right+gap+width,y+height};
    if(settings.left-gap-width>=work.left)
        return {settings.left-gap-width,y,settings.left-gap,y+height};
    if(settings.bottom+gap+height<=work.bottom)
        return {x,settings.bottom+gap,x+width,settings.bottom+gap+height};
    if(settings.top-gap-height>=work.top)
        return {x,settings.top-gap-height,x+width,settings.top-gap};
    return {x,y,x+width,y+height};
}
static bool NoteSurfaceReady() {
    return g_layout&&g_visible&&(IsWindowVisible(g_background)||(Attach()&&IsWindowVisible(g_background)));
}
static bool PresentNote(const std::wstring& id) {
    SyncWidgets();
    const auto created=std::find_if(g_widgets.begin(),g_widgets.end(),[&](const auto& window){
        return window->id==id&&IsWindowVisible(window->window)&&IsWindow(window->edit);
    });
    if(created==g_widgets.end())return false;
    const auto& window=**created;
    SetForegroundWindow(window.window);
    if(GetForegroundWindow()==window.window)SetFocus(window.edit);
    RedrawWindow(window.window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    return true;
}
bool CanvasCreateNote(HWND settings) {
    if(!NoteSurfaceReady())return false;

    RECT settingsRect{};
    MONITORINFO monitor{sizeof(MONITORINFO)};
    if(!GetWindowRect(settings,&settingsRect)||
        !GetMonitorInfoW(MonitorFromWindow(settings,MONITOR_DEFAULTTONEAREST),&monitor))return false;

    Layout::Widget widget;
    const auto id=std::to_wstring(GetTickCount64());
    widget.id=id;
    for(unsigned suffix=1;FindWidget(widget.id);++suffix)widget.id=id+L"-"+std::to_wstring(suffix);
    const LONG offset=static_cast<LONG>(g_layout->widgets.size()%6)*20;
    widget.rect=NewNoteRect(monitor.rcWork,settingsRect,offset);
    OffsetRect(&widget.rect,-GetSystemMetrics(SM_XVIRTUALSCREEN),-GetSystemMetrics(SM_YVIRTUALSCREEN));
    g_layout->widgets.push_back(std::move(widget));
    if(!PresentNote(g_layout->widgets.back().id)) {
        g_layout->widgets.pop_back();
        SyncWidgets();
        db::LogF("[note] creation did not produce a visible editor");
        return false;
    }
    QueueLayoutSave();
    return true;
}
bool CanvasShowNote(const std::wstring& id) {
    if(!NoteSurfaceReady())return false;
    auto* widget=FindWidget(id);
    if(!widget)return false;
    const bool wasClosed=widget->closed;
    widget->closed=false;
    if(!PresentNote(id)) {
        widget->closed=wasClosed;
        SyncWidgets();
        return false;
    }
    if(wasClosed)QueueLayoutSave();
    return true;
}
void CanvasSetOpacity(int opacity){if(g_layout){g_layout->opacity=std::clamp(opacity,0,100);PaintAll();}}
void CanvasNotifyDesktopHostChanged() {
    CancelDesktopMoves();
    // Explorer may have replaced every desktop HWND.  DestroyDecorations first
    // restores any surviving old ListView, then attach to the new host.
    DestroyDecorations();
    g_host={};g_snapshot={};g_visualDirty=true;
    Attach();
}
void HandleCanvasCommand(CanvasCommand command) {
    if(!g_layout)return;
    if(command==CanvasCommand::Toggle) {
        if(g_iconDrag>=0||g_marquee)CancelIconGesture();
        g_visible=!g_visible;CancelDesktopMoves();g_pending=0;g_moves.clear();g_snapshot={};FinishRename(true);
        if(!g_visible) {
            if(g_background)ShowWindow(g_background,SW_HIDE);
            for(auto& d:g_decorations){if(IsWindow(d->header))ShowWindow(d->header,SW_HIDE);if(IsWindow(d->gripLeft))ShowWindow(d->gripLeft,SW_HIDE);if(IsWindow(d->grip))ShowWindow(d->grip,SW_HIDE);}
            for(auto& widget:g_widgets)if(IsWindow(widget->window))ShowWindow(widget->window,SW_HIDE);
            if(IsWindow(g_hiddenListview))ReleaseHiddenDesktopListView(g_hiddenListview);
            g_hiddenListview=nullptr;g_nativeHidden=false;
        } else {
            // Attach prepares and proves the surface before it hides Explorer.
            // If discovery or painting fails, the native desktop stays visible.
            if(!Attach()) {g_visible=false;return;}
            DesktopSnapshot current;if(ReadDesktop(current)){g_snapshot=std::move(current);g_visualDirty=true;}
        }
        if(g_visible)PaintAll();
        UpdateSurfaceVisibility();
    }
    if(command==CanvasCommand::NewBox)CreateBoxAt({160,160});
    if(command==CanvasCommand::ClearWidgets) {g_layout->widgets.clear();SyncWidgets();QueueLayoutSave();}
    if(command==CanvasCommand::RefreshLayout)Rebuild();
    if(command==CanvasCommand::Reload) {
        // A backup restore replaces the file before requesting Reload.
        FinishQueuedLayoutSave(false);
        g_iconCache.clear();*g_layout=LoadLayout();Rebuild();
    }
    if(command==CanvasCommand::Exit)DestroyCanvas();
}
}

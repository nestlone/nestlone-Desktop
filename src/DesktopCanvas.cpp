#include "DesktopCanvas.h"
#include "DesktopSession.h"
#include "DesktopHost.h"
#include "log.h"
#include <windowsx.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>
#include <algorithm>
#include <memory>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace nestlone {
namespace {
using Microsoft::WRL::ComPtr;
Layout* g_layout=nullptr;
HWND g_manager=nullptr,g_background=nullptr,g_edit=nullptr;
HINSTANCE g_instance=nullptr;
ULONG_PTR g_token=0;
bool g_visible=true,g_drag=false,g_resizing=false,g_tabPending=false,g_tabDetached=false,g_nativeHidden=false;
HWND g_hiddenListview=nullptr;
db::HostInfo g_host;
DesktopSnapshot g_snapshot;
uint64_t g_pending=0;
std::vector<DesktopMove> g_moves,g_dragStart;
POINT g_mouseStart{};
RECT g_boxStart{};
std::wstring g_dragId,g_editId,g_status;
std::vector<std::pair<std::wstring,RECT>> g_boxDragStart;
HFONT g_editFont=nullptr;
struct Decoration {std::wstring id;HWND header=nullptr,grip=nullptr;};
std::vector<std::unique_ptr<Decoration>> g_decorations;
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
bool g_visualDirty=true;
void CancelIconGesture();
void RebuildVisualIcons();
void RefreshVisualGeometry();
void DrawVisualIcons(Gdiplus::Graphics&);
constexpr int kHeader=32;
int HeaderHeight(){return MulDiv(kHeader,static_cast<int>(g_host.listview?GetDpiForWindow(g_host.listview):96),96);}
Box* FindBox(const std::wstring& id) {
    if(g_layout)for(auto& box:g_layout->boxes)if(box.id==id)return &box;
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
std::vector<Box*> GroupTabs(Box& root) {
    std::vector<Box*> tabs;tabs.push_back(&root);
    for(auto& box:g_layout->boxes)if(box.groupId==root.id)tabs.push_back(&box);
    return tabs;
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
RECT DisplayRect(const Box& box) {RECT r=box.rect;if(box.collapsed)r.bottom=r.top+HeaderHeight();return r;}
int ContainingBox(const DesktopEntry& entry) {
    POINT center{entry.position.x+g_snapshot.iconSize/2,entry.position.y+g_snapshot.iconSize/2};
    for(int i=static_cast<int>(g_layout->boxes.size())-1;i>=0;--i) {
        auto& b=g_layout->boxes[i];RECT r=b.rect;r.top+=HeaderHeight();
        if(!b.collapsed && PtInRect(&r,center))return i;
    }
    return -1;
}
void Rounded(Gdiplus::Graphics& g,const RECT& r,Gdiplus::Color color) {
    if(r.right<=r.left||r.bottom<=r.top)return;
    Gdiplus::GraphicsPath path;
    const float d=12,x=static_cast<float>(r.left),y=static_cast<float>(r.top);
    const float w=static_cast<float>(r.right-r.left),h=static_cast<float>(r.bottom-r.top);
    path.AddArc(x,y,d,d,180,90);path.AddArc(x+w-d,y,d,d,270,90);
    path.AddArc(x+w-d,y+h-d,d,d,0,90);path.AddArc(x,y+h-d,d,d,90,90);path.CloseFigure();
    Gdiplus::SolidBrush brush(color);g.FillPath(&brush,&path);
}
Gdiplus::Color Background(COLORREF color,int opacity) {
    return Gdiplus::Color(static_cast<BYTE>(std::clamp(opacity,20,100)*255/100),GetRValue(color),GetGValue(color),GetBValue(color));
}
// Background rendering intentionally knows nothing about desktop items.
bool RenderPixels(void* pixels,int width,int height,const Layout& layout) {
    Gdiplus::Bitmap bitmap(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
    Gdiplus::Graphics g(&bitmap);g.Clear(Gdiplus::Color(0,0,0,0));g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    for(const auto& box:layout.boxes)if(IsGroupRoot(box))Rounded(g,DisplayRect(box),Background(box.color,layout.opacity));
    g.Flush();return g.GetLastStatus()==Gdiplus::Ok;
}
void Label(Gdiplus::Graphics& g,const std::wstring& value,RECT r) {
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");Gdiplus::StringFormat format;
    format.SetAlignment(Gdiplus::StringAlignmentCenter);format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    Gdiplus::GraphicsPath text;
    text.AddString(value.c_str(),-1,&family,Gdiplus::FontStyleBold,static_cast<float>(HeaderHeight()*0.4),
        Gdiplus::RectF(static_cast<float>(r.left),static_cast<float>(r.top),static_cast<float>(r.right-r.left),static_cast<float>(r.bottom-r.top)),&format);
    Gdiplus::Pen outline(Gdiplus::Color(230,22,38,49),1.4f);Gdiplus::SolidBrush ink(Gdiplus::Color(255,255,255,255));
    g.DrawPath(&outline,&text);g.FillPath(&ink,&text);
}
bool PaintWindow(HWND hwnd,Box* box=nullptr,bool grip=false) {
    RECT r{};if(!GetClientRect(hwnd,&r)||r.right<=0||r.bottom<=0)return false;
    HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!bitmap){DeleteDC(dc);ReleaseDC(nullptr,screen);return false;}
    auto old=SelectObject(dc,bitmap);
    if(!box) {
        RenderPixels(pixels,r.right,r.bottom,*g_layout);
        Gdiplus::Bitmap surface(r.right,r.bottom,r.right*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
        Gdiplus::Graphics g(&surface);g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);DrawVisualIcons(g);g.Flush();
        auto* hitPixels=static_cast<DWORD*>(pixels);for(int i=0;i<r.right*r.bottom;++i)if((hitPixels[i]>>24)==0)hitPixels[i]=0x01000000u;
    }
    else {
        Gdiplus::Bitmap surface(r.right,r.bottom,r.right*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
        Gdiplus::Graphics g(&surface);g.Clear(Gdiplus::Color(0,0,0,0));g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Rounded(g,r,Background(box->color,g_layout->opacity));
        if(box->selected) { Gdiplus::Pen outline(Gdiplus::Color(230,255,255,255),2.0f);g.DrawRectangle(&outline,1,1,r.right-3,r.bottom-3); }
        if(grip)Label(g,L"◢",r);
        else {
            const int h=HeaderHeight();
            auto tabs=GroupTabs(*box);const int contentRight=r.right-3*h;
            if(tabs.size()==1) Label(g,box->title,{h,0,contentRight,h});
            else for(size_t i=0;i<tabs.size();++i) {
                const int left=static_cast<int>(i*contentRight/tabs.size());
                const int right=static_cast<int>((i+1)*contentRight/tabs.size());
                Label(g,tabs[i]->title,{left,0,right,h});
                if(IsActiveTab(*tabs[i])) {Gdiplus::Pen line(Gdiplus::Color(220,255,255,255),1.0f);g.DrawLine(&line,left+8,h-3,right-8,h-3);}
            }
            Label(g,L"▦",{r.right-3*h,0,r.right-2*h,h});
            Label(g,box->collapsed?L"+":L"−",{r.right-2*h,0,r.right-h,h});
            Label(g,L"×",{r.right-h,0,r.right,h});
        }
        g.Flush();
    }
    POINT source{};SIZE size{r.right,r.bottom};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    bool ok=UpdateLayeredWindow(hwnd,screen,nullptr,&size,dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(nullptr,screen);return ok;
}
POINT ParentPoint(POINT point) {
    point.x+=GetSystemMetrics(SM_XVIRTUALSCREEN);point.y+=GetSystemMetrics(SM_YVIRTUALSCREEN);
    ScreenToClient(g_host.defviewParent,&point);return point;
}
void PaintAll() {
    if(!g_layout)return;
    if(g_visualDirty){RebuildVisualIcons();g_visualDirty=false;}
    if(g_background)PaintWindow(g_background);
    for(auto& d:g_decorations)if(auto* box=FindBox(d->id)) {
        POINT top=ParentPoint({box->rect.left,box->rect.top});int h=HeaderHeight();
        SetWindowPos(d->header,HWND_TOP,top.x,top.y,box->rect.right-box->rect.left,h,SWP_NOACTIVATE);
        PaintWindow(d->header,box);
        POINT corner=ParentPoint({box->rect.right-18,box->rect.bottom-18});
        SetWindowPos(d->grip,HWND_TOP,corner.x,corner.y,18,18,SWP_NOACTIVATE);
        PaintWindow(d->grip,box,true);
        ShowWindow(d->header,g_visible?SW_SHOWNOACTIVATE:SW_HIDE);
        ShowWindow(d->grip,g_visible&&!box->collapsed?SW_SHOWNOACTIVATE:SW_HIDE);
    }
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
        r.bottom=max(r.bottom,r.top+HeaderHeight()+16+static_cast<LONG>(box.items.size())*rowHeight);
        return r;
    }
    const LONG sy=BoxGridCellHeight(snapshot);
    r.right=max(r.right,r.left+BoxGridMinimumWidth(snapshot)+16);
    Box fitted=box;fitted.rect=r;
    const LONG columns=BoxGridColumns(fitted,snapshot);
    LONG count=0;for(const auto& path:box.items)if(FindEntry(snapshot,path))++count;
    const LONG rows=max(1L,(count+columns-1)/columns);
    r.bottom=max(r.bottom,r.top+HeaderHeight()+12+rows*sy+24);
    return r;
}
std::vector<DesktopMove> GridMoves(const Box& box,const DesktopSnapshot& snapshot) {
    if(!box.iconView)return {};
    const LONG sx=BoxGridCellWidth(box,snapshot),sy=BoxGridCellHeight(snapshot);
    const LONG columns=BoxGridColumns(box,snapshot),left=BoxGridLeft(box);
    std::vector<DesktopMove> moves;int slot=0;
    for(const auto& path:box.items)if(FindEntry(snapshot,path)) {
        LONG x=left+(slot%columns)*sx;
        LONG y=box.rect.top+HeaderHeight()+12+(slot/columns)*sy;
        moves.push_back({path,{x+max(0L,(sx-snapshot.iconSize)/2),y}});++slot;
    }
    return moves;
}
std::shared_ptr<Gdiplus::Bitmap> LoadIconImage(const std::wstring& path,int size) {
    SHFILEINFOW info{};
    if(!SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON)||!info.hIcon)return {};
    HICON icon=info.hIcon;
    auto source=std::make_unique<Gdiplus::Bitmap>(icon);
    DestroyIcon(icon);
    if(source->GetLastStatus()!=Gdiplus::Ok)return {};
    auto* copy=source->Clone(0,0,source->GetWidth(),source->GetHeight(),PixelFormat32bppPARGB);
    if(!copy||copy->GetLastStatus()!=Gdiplus::Ok){delete copy;return {};}
    if(static_cast<int>(source->GetWidth())==size&&static_cast<int>(source->GetHeight())==size)
        return std::shared_ptr<Gdiplus::Bitmap>(copy);
    auto result=std::make_shared<Gdiplus::Bitmap>(size,size,PixelFormat32bppPARGB);
    if(result->GetLastStatus()!=Gdiplus::Ok){delete copy;return {};}
    Gdiplus::Graphics graphics(result.get());
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(copy,0,0,size,size);
    delete copy;
    return graphics.GetLastStatus()==Gdiplus::Ok?result:std::shared_ptr<Gdiplus::Bitmap>();
}
int BoxRowHeight(const Box& box) { return max(30,HeaderHeight())+(box.iconView?0:0); }
RECT BoxItemRect(const Box& box,int slot) {
    const int h=HeaderHeight();
    if(!box.iconView){int rowHeight=BoxRowHeight(box);return {box.rect.left+8,box.rect.top+h+8+slot*rowHeight,box.rect.right-8,box.rect.top+h+8+(slot+1)*rowHeight};}
    const LONG sx=BoxGridCellWidth(box,g_snapshot),sy=BoxGridCellHeight(g_snapshot),columns=BoxGridColumns(box,g_snapshot),left=BoxGridLeft(box);
    return {left+(slot%columns)*sx,box.rect.top+h+12+(slot/columns)*sy,left+(slot%columns)*sx+sx,box.rect.top+h+12+(slot/columns)*sy+sy};
}
int BoxSlot(const Box& box,POINT point) {
    const int h=HeaderHeight();
    if(!box.iconView)return max(0,(point.y-box.rect.top-h-8)/BoxRowHeight(box));
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
    visual.list=!box->iconView;visual.size=visual.list?min(24,HeaderHeight()):max(16,g_snapshot.iconSize);RECT row=BoxItemRect(*box,slot);visual.hit=row;visual.position={visual.list?row.left+8:row.left+max(0L,((row.right-row.left)-visual.size)/2),visual.list?row.top+(row.bottom-row.top-visual.size)/2:row.top};
    // Explorer's icon view reserves the lower part of each grid cell for the
    // (potentially two-line) title.  Do not vertically centre it over the icon.
    const LONG titleHeight=MulDiv(32,static_cast<int>(g_host.listview?GetDpiForWindow(g_host.listview):96),96);
    visual.label=visual.list
        ? RECT{visual.position.x+visual.size+8,row.top,row.right-8,row.bottom}
        : RECT{row.left,visual.position.y+visual.size+3,row.right,min(row.bottom,visual.position.y+visual.size+3+titleHeight)};
}
void ReleaseVisualIcons() { g_visualIcons.clear(); }
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
        const Box* owner=nullptr;int slot=0;
        for(const auto& box:g_layout->boxes)if(HasPath(box,entry.path)){owner=&box;for(const auto& item:box.items){if(_wcsicmp(item.c_str(),entry.path.c_str())==0)break;if(FindEntry(g_snapshot,item))++slot;}break;}
        if(owner&&(!IsActiveTab(*owner)||GroupRoot(*owner)->collapsed))continue;
        VisualIcon visual;visual.path=entry.path;visual.name=entry.path;auto slash=visual.name.find_last_of(L"\\/");if(slash!=std::wstring::npos)visual.name=visual.name.substr(slash+1);
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
void DrawVisualIcons(Gdiplus::Graphics& g) {
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    for(auto& item:g_visualIcons) {
        if(item.selected){Gdiplus::SolidBrush selected(Gdiplus::Color(72,55,133,190));Gdiplus::RectF r(static_cast<float>(item.hit.left),static_cast<float>(item.hit.top),static_cast<float>(item.hit.right-item.hit.left),static_cast<float>(item.hit.bottom-item.hit.top));g.FillRectangle(&selected,r);}
        if(item.image)g.DrawImage(item.image.get(),item.position.x,item.position.y,item.size,item.size);
        Gdiplus::StringFormat format;format.SetAlignment(item.list?Gdiplus::StringAlignmentNear:Gdiplus::StringAlignmentCenter);format.SetLineAlignment(item.list?Gdiplus::StringAlignmentCenter:Gdiplus::StringAlignmentNear);format.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);if(item.list)format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);else format.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);
        Gdiplus::SolidBrush text(Gdiplus::Color(255,255,255,255)),shadow(Gdiplus::Color(190,0,0,0));Gdiplus::Font font(&family,item.list?Gdiplus::REAL(12):Gdiplus::REAL(12),Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::RectF label(static_cast<float>(item.label.left),static_cast<float>(item.label.top),static_cast<float>(max(1L,item.label.right-item.label.left)),static_cast<float>(max(1L,item.label.bottom-item.label.top))),offset=label;offset.X+=1.0f;offset.Y+=1.0f;g.DrawString(item.name.c_str(),-1,&font,offset,&format,&shadow);g.DrawString(item.name.c_str(),-1,&font,label,&format,&text);
    }
    // A selected icon reveals its complete file name without permanently
    // widening the grid. This mirrors the desktop's focus-only title affordance.
    if(g_focusIcon>=0&&g_focusIcon<static_cast<int>(g_visualIcons.size())) {
        const auto& item=g_visualIcons[g_focusIcon];
        if(item.selected&&!item.list) {
            const int dpi=static_cast<int>(g_host.listview?GetDpiForWindow(g_host.listview):96);
            Gdiplus::Font font(&family,static_cast<Gdiplus::REAL>(MulDiv(12,dpi,96)),Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
            Gdiplus::StringFormat format;format.SetAlignment(Gdiplus::StringAlignmentCenter);format.SetLineAlignment(Gdiplus::StringAlignmentNear);
            const float width=static_cast<float>(min(440L,max(160L,g_snapshot.spacing.x*3L)));
            Gdiplus::RectF measure(0,0,width-16.0f,2000.0f),used{};g.MeasureString(item.name.c_str(),-1,&font,measure,&format,&used);
            const float height=max(static_cast<float>(MulDiv(28,dpi,96)),used.Height+10.0f);
            const float x=max(4.0f,min(static_cast<float>(GetSystemMetrics(SM_CXVIRTUALSCREEN))-width-4.0f,static_cast<float>(item.position.x+item.size/2)-width/2.0f));
            const float y=static_cast<float>(item.label.top);
            Gdiplus::SolidBrush background(Gdiplus::Color(238,55,133,190));g.FillRectangle(&background,x,y,width,height);
            Gdiplus::SolidBrush text(Gdiplus::Color(255,255,255,255));Gdiplus::RectF title(x+8.0f,y+5.0f,width-16.0f,height-10.0f);g.DrawString(item.name.c_str(),-1,&font,title,&format,&text);
        }
    }
    if(g_marquee&&g_marqueeMoved){Gdiplus::SolidBrush fill(Gdiplus::Color(45,80,160,230));Gdiplus::Pen edge(Gdiplus::Color(190,130,190,255),1.0f);Gdiplus::RectF r(static_cast<float>(g_marqueeRect.left),static_cast<float>(g_marqueeRect.top),static_cast<float>(g_marqueeRect.right-g_marqueeRect.left),static_cast<float>(g_marqueeRect.bottom-g_marqueeRect.top));g.FillRectangle(&fill,r);g.DrawRectangle(&edge,r);}
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
void FinishRename(bool save) {
    HWND edit=g_edit;if(!edit)return;g_edit=nullptr;
    wchar_t value[81]{};GetWindowTextW(edit,value,81);
    std::wstring text=value;auto first=text.find_first_not_of(L" \t\r\n");
    if(save&&first!=std::wstring::npos)if(auto* box=FindBox(g_editId)) {
        box->title=text.substr(first,text.find_last_not_of(L" \t\r\n")-first+1);SaveLayout(*g_layout);
    }
    DestroyWindow(edit);g_editId.clear();PaintAll();
}
LRESULT CALLBACK EditProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_KEYDOWN&&(wp==VK_RETURN||wp==VK_ESCAPE)){FinishRename(wp==VK_RETURN);return 0;}
    if(msg==WM_KILLFOCUS){PostMessageW(g_manager,WM_APP+10,0,0);return 0;}
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(hwnd,EditProc,1);
    return DefSubclassProc(hwnd,msg,wp,lp);
}
void BeginRename(Box& box) {
    FinishRename(true);g_editId=box.id;
    POINT p{box.rect.left+HeaderHeight(),box.rect.top+HeaderHeight()/4};
    p.x+=GetSystemMetrics(SM_XVIRTUALSCREEN);p.y+=GetSystemMetrics(SM_YVIRTUALSCREEN);
    g_edit=CreateWindowExW(WS_EX_TOOLWINDOW,L"EDIT",box.title.c_str(),WS_POPUP|ES_CENTER|ES_AUTOHSCROLL,p.x,p.y,
        max(40L,box.rect.right-box.rect.left-4*HeaderHeight()),HeaderHeight()*3/4,nullptr,nullptr,g_instance,nullptr);
    if(!g_edit)return;
    if(g_editFont)DeleteObject(g_editFont);
    g_editFont=CreateFontW(-HeaderHeight()*2/5,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    SendMessageW(g_edit,WM_SETFONT,reinterpret_cast<WPARAM>(g_editFont),TRUE);SendMessageW(g_edit,EM_SETLIMITTEXT,80,0);
    SetWindowSubclass(g_edit,EditProc,1,0);ShowWindow(g_edit,SW_SHOW);SetForegroundWindow(g_edit);SetFocus(g_edit);SendMessageW(g_edit,EM_SETSEL,0,-1);
}
void DestroyDecorations() {
    FinishRename(false);
    for(auto& d:g_decorations){if(IsWindow(d->header))DestroyWindow(d->header);if(IsWindow(d->grip))DestroyWindow(d->grip);}
    g_decorations.clear();if(IsWindow(g_background))DestroyWindow(g_background);g_background=nullptr;
    ReleaseVisualIcons();if(IsWindow(g_hiddenListview))ReleaseHiddenDesktopListView(g_hiddenListview);g_hiddenListview=nullptr;g_nativeHidden=false;
}
LRESULT CALLBACK DecorationProc(HWND,UINT,WPARAM,LPARAM);
int HitVisualIcon(int x,int y) { for(int i=static_cast<int>(g_visualIcons.size())-1;i>=0;--i)if(PtInRect(&g_visualIcons[i].hit,POINT{x,y}))return i;return -1; }
void RefreshIconHit(VisualIcon& item) {
    const LONG padding=max(0L,(g_snapshot.spacing.x-item.size)/2);
    item.hit={item.position.x-padding,item.position.y,item.position.x-padding+g_snapshot.spacing.x,item.position.y+g_snapshot.spacing.y};
    item.label={item.hit.left,item.position.y+item.size+3,item.hit.right,item.hit.bottom};
}
void ClearSelections() {
    for(auto& item:g_visualIcons)item.selected=false;
    for(auto& box:g_layout->boxes)box.selected=false;
    g_focusIcon=-1;
}
void UpdateMarquee(POINT point) {
    g_marqueeRect={min(g_marqueeStart.x,point.x),min(g_marqueeStart.y,point.y),max(g_marqueeStart.x,point.x),max(g_marqueeStart.y,point.y)};
    g_marqueeMoved=abs(point.x-g_marqueeStart.x)>=4||abs(point.y-g_marqueeStart.y)>=4;
    if(!g_marqueeMoved)return;
    if(!g_marqueeCtrl&&!g_marqueeShift)for(auto& item:g_visualIcons)item.selected=false;
    for(auto& item:g_visualIcons){RECT overlap{};bool inside=IntersectRect(&overlap,&g_marqueeRect,&item.hit)!=FALSE;bool initial=std::any_of(g_selectionStart.begin(),g_selectionStart.end(),[&](const auto& path){return _wcsicmp(path.c_str(),item.path.c_str())==0;});if(g_marqueeCtrl)item.selected=initial!=inside;else if(g_marqueeShift)item.selected=initial||inside;else item.selected=inside;}
}
void FinishMarquee() {g_marquee=false;g_marqueeMoved=false;g_selectionStart.clear();g_visualDirty=true;PaintAll();}
void CancelIconGesture() {g_marquee=false;g_marqueeMoved=false;g_iconDrag=-1;g_iconMoved=false;g_iconDragStart.clear();g_selectionStart.clear();if(GetCapture())ReleaseCapture();PaintAll();}
void ShowShellMenu(HWND owner,const std::wstring& path,POINT screen) {
    PIDLIST_ABSOLUTE pidl=nullptr;ComPtr<IShellFolder> parent;PCUITEMID_CHILD child=nullptr;
    if(FAILED(SHParseDisplayName(path.c_str(),nullptr,&pidl,0,nullptr))||!pidl)return;
    if(SUCCEEDED(SHBindToParent(pidl,IID_PPV_ARGS(&parent),&child))) {
        ComPtr<IContextMenu> menu; if(SUCCEEDED(parent->GetUIObjectOf(owner,1,&child,IID_IContextMenu,nullptr,reinterpret_cast<void**>(menu.GetAddressOf())))) {
            HMENU popup=CreatePopupMenu();if(SUCCEEDED(menu->QueryContextMenu(popup,0,1,0x7fff,CMF_NORMAL))) {
                int command=TrackPopupMenu(popup,TPM_RETURNCMD|TPM_RIGHTBUTTON,screen.x,screen.y,0,owner,nullptr);
                if(command){CMINVOKECOMMANDINFOEX invoke{};invoke.cbSize=sizeof(invoke);invoke.fMask=CMIC_MASK_UNICODE;invoke.hwnd=owner;invoke.lpVerb=MAKEINTRESOURCEA(command-1);invoke.lpVerbW=MAKEINTRESOURCEW(command-1);invoke.nShow=SW_SHOWNORMAL;menu->InvokeCommand(reinterpret_cast<LPCMINVOKECOMMANDINFO>(&invoke));}
            } DeleteMenu(popup,0,MF_BYPOSITION);DestroyMenu(popup);
        }
    } CoTaskMemFree(pidl);
}
void FinishVisualDrag() {
    if(g_iconDrag<0||g_iconDrag>=static_cast<int>(g_visualIcons.size()))return;
    const auto& anchor=g_visualIcons[g_iconDrag];
    POINT center{anchor.position.x+g_snapshot.iconSize/2,anchor.position.y+g_snapshot.iconSize/2};
    int target=-1;
    for(int i=0;i<static_cast<int>(g_layout->boxes.size());++i){RECT r=g_layout->boxes[i].rect;r.top+=HeaderHeight();if(!g_layout->boxes[i].collapsed&&PtInRect(&r,center))target=i;}
    std::vector<int> affected;std::vector<std::pair<std::wstring,POINT>> dragged;
    for(const auto& start:g_iconDragStart) {
        auto found=std::find_if(g_visualIcons.begin(),g_visualIcons.end(),[&](const auto& item){return _wcsicmp(item.path.c_str(),start.first.c_str())==0;});
        if(found==g_visualIcons.end())continue;
        dragged.push_back({found->path,found->position});
        for(int i=0;i<static_cast<int>(g_layout->boxes.size());++i)if(HasPath(g_layout->boxes[i],start.first)){affected.push_back(i);break;}
    }
    std::vector<std::wstring> paths;for(const auto& item:dragged)paths.push_back(item.first);
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
        box.rect=FitGrid(box,g_snapshot);auto arranged=GridMoves(box,g_snapshot);moves.insert(moves.end(),arranged.begin(),arranged.end());
    }
    if(target<0)for(const auto& item:dragged)moves.push_back({item.first,item.second});
    if(!moves.empty())QueueMoves(moves);
    SaveLayout(*g_layout);g_visualDirty=true;g_iconDrag=-1;g_iconDragStart.clear();PaintAll();
}
bool Attach() {
    auto host=db::DiscoverDesktopHost();
    if(!host.listview || !host.defviewParent)return false;
    if(g_background && IsWindow(g_background) && g_host.listview==host.listview)return true;
    DestroyDecorations();g_host=host;RestoreLegacyMask(host.listview);
    DesktopSnapshot initial;if(ReadDesktop(initial)){g_snapshot=std::move(initial);g_visualDirty=true;}
    g_hiddenListview=host.listview;ClaimHiddenDesktopListView(g_hiddenListview);ShowWindow(g_hiddenListview,SW_HIDE);g_nativeHidden=true;
    POINT origin=ParentPoint({0,0});
    g_background=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
        L"nestlone-D.Decoration",L"",WS_CHILD,origin.x,origin.y,GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN),host.defviewParent,nullptr,g_instance,nullptr);
    if(!g_background){DestroyDecorations();return false;}
    // This is the owned desktop surface. Explorer's list view is hidden while
    // the surface is alive; file paths remain unchanged.
    SetWindowPos(g_background,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    for(const auto& box:g_layout->boxes)if(IsGroupRoot(box)) {
        auto d=std::make_unique<Decoration>();d->id=box.id;
        d->header=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"nestlone-D.Decoration",L"",WS_CHILD,0,0,200,HeaderHeight(),host.defviewParent,nullptr,g_instance,d.get());
        d->grip=CreateWindowExW(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"nestlone-D.Decoration",L"",WS_CHILD,0,0,18,18,host.defviewParent,nullptr,g_instance,d.get());
        if(!d->header||!d->grip){if(d->header)DestroyWindow(d->header);if(d->grip)DestroyWindow(d->grip);DestroyDecorations();return false;}
        g_decorations.push_back(std::move(d));
    }
    if(!PaintWindow(g_background)){DestroyDecorations();return false;}
    ShowWindow(g_background,g_visible?SW_SHOWNOACTIVATE:SW_HIDE);PaintAll();return true;
}
void Rebuild(){DestroyDecorations();Attach();SaveLayout(*g_layout);}
void ToggleBoxView(Box& box) {box.iconView=!box.iconView;box.rect=FitGrid(box,g_snapshot);SaveLayout(*g_layout);g_visualDirty=true;PaintAll();}
void Menu(HWND hwnd,Box& box,POINT point) {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"重命名盒子");AppendMenuW(menu,MF_STRING,2,L"整理盒内图标位置");
    AppendMenuW(menu,MF_STRING|(box.iconView?MF_CHECKED:0),6,L"图标显示");AppendMenuW(menu,MF_STRING|(!box.iconView?MF_CHECKED:0),7,L"列表显示");
    AppendMenuW(menu,MF_STRING,3,L"收纳区域内图标");AppendMenuW(menu,MF_STRING,4,box.collapsed?L"展开盒子":L"折叠盒子（隐藏图标）");
    AppendMenuW(menu,MF_STRING,5,L"删除盒子（保留图标与文件）");
    if(!g_status.empty())AppendMenuW(menu,MF_STRING|MF_DISABLED,0,g_status.c_str());
    int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd,nullptr);DestroyMenu(menu);
    if(command==1)BeginRename(box);
    if(command==2){Arrange(box);if(!g_status.empty())MessageBoxW(hwnd,g_status.c_str(),L"图标位置",MB_OK|MB_ICONINFORMATION);}
    if(command==3) {
        int index=static_cast<int>(&box-g_layout->boxes.data());
        for(const auto& entry:g_snapshot.entries)if(ContainingBox(entry)==index)AssignDesktopItem(*g_layout,entry.path,index,entry.position);
        Arrange(box);SaveLayout(*g_layout);
    }
    if(command==6){box.iconView=true;box.rect=FitGrid(box,g_snapshot);SaveLayout(*g_layout);g_visualDirty=true;PaintAll();}
    if(command==7){box.iconView=false;box.rect=FitGrid(box,g_snapshot);SaveLayout(*g_layout);g_visualDirty=true;PaintAll();}
    if(command==4){box.collapsed=!box.collapsed;g_visualDirty=true;SaveLayout(*g_layout);PaintAll();}
    if(command==5){auto id=box.id;g_layout->boxes.erase(std::remove_if(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const auto& b){return b.id==id;}),g_layout->boxes.end());PostMessageW(g_manager,WM_APP+11,0,0);}
}
LRESULT CALLBACK DecorationProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
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
            g_focusIcon=hit;g_iconDrag=hit;g_iconMoved=false;g_iconDragStart.clear();for(const auto& item:g_visualIcons)if(item.selected)g_iconDragStart.push_back({item.path,item.position});GetCursorPos(&g_iconMouseStart);SetCapture(hwnd);PaintWindow(hwnd);return 0;
        }
        if(hwnd==g_background && message==WM_MOUSEMOVE && GetCapture()==hwnd) {
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(g_marquee){UpdateMarquee(point);PaintWindow(hwnd);return 0;}
            if(g_iconDrag>=0){POINT cursor{};GetCursorPos(&cursor);LONG dx=cursor.x-g_iconMouseStart.x,dy=cursor.y-g_iconMouseStart.y;if(!g_iconMoved&&abs(dx)<4&&abs(dy)<4)return 0;g_iconMoved=true;for(auto& item:g_visualIcons)if(item.selected){auto start=std::find_if(g_iconDragStart.begin(),g_iconDragStart.end(),[&](const auto& value){return _wcsicmp(value.first.c_str(),item.path.c_str())==0;});if(start!=g_iconDragStart.end()){item.position=ClampDesktopPosition({start->second.x+dx,start->second.y+dy});RefreshIconHit(item);}}PaintWindow(hwnd);return 0;}
        }
        if(hwnd==g_background && message==WM_LBUTTONUP){if(g_marquee){ReleaseCapture();FinishMarquee();return 0;}if(g_iconDrag>=0){ReleaseCapture();if(g_iconMoved)FinishVisualDrag();else{g_iconDrag=-1;g_iconDragStart.clear();}return 0;}}
        if(hwnd==g_background && message==WM_LBUTTONDBLCLK){int hit=HitVisualIcon(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));if(hit>=0)ShellExecuteW(nullptr,L"open",g_visualIcons[hit].path.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}
        if(hwnd==g_background && message==WM_RBUTTONDOWN){int hit=HitVisualIcon(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));if(hit>=0&&!g_visualIcons[hit].selected){for(auto& item:g_visualIcons)item.selected=false;g_visualIcons[hit].selected=true;g_focusIcon=hit;PaintWindow(hwnd);}return 0;}
        if(hwnd==g_background && message==WM_RBUTTONUP){
            int hit=HitVisualIcon(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));POINT screen{};GetCursorPos(&screen);
            if(hit>=0)ShowShellMenu(hwnd,g_visualIcons[hit].path,screen);
            else {
                POINT virtualPoint{screen.x-GetSystemMetrics(SM_XVIRTUALSCREEN),screen.y-GetSystemMetrics(SM_YVIRTUALSCREEN)};bool boxHit=false;
                for(auto& candidate:g_layout->boxes){RECT bounds=DisplayRect(candidate);if(PtInRect(&bounds,virtualPoint)){Menu(hwnd,candidate,screen);boxHit=true;break;}}
                if(!boxHit)ShowShellBackgroundMenu(hwnd,screen);
            }
            return 0;
        }
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    switch(message) {
    case WM_NCHITTEST:return HTCLIENT;
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);PaintWindow(hwnd,box,hwnd==d->grip);return 0;}
    case WM_LBUTTONDBLCLK:if(hwnd==d->header && GET_X_LPARAM(lp)<box->rect.right-box->rect.left-3*HeaderHeight())BeginRename(*box);return 0;
    case WM_CONTEXTMENU:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(p.x==-1)GetCursorPos(&p);Menu(hwnd,*box,p);return 0;}
    case WM_LBUTTONDOWN: {
        FinishRename(true);int h=HeaderHeight(),x=GET_X_LPARAM(lp),width=box->rect.right-box->rect.left;
        if(hwnd==d->header && x>=width-3*h) {
            if(x>=width-h)g_boxButton=3;
            else if(x>=width-2*h)g_boxButton=2;
            else g_boxButton=1;
            SetCapture(hwnd);return 0;
        }
        if(hwnd==d->header) {
            auto tabs=GroupTabs(*box);
            if(tabs.size()>1&&x<width-3*h) {
                const size_t index=min(tabs.size()-1,static_cast<size_t>(max(0,x)*static_cast<int>(tabs.size())/max(1,width-3*h)));
                g_tabPending=true;g_dragId=tabs[index]->id;g_boxStart=tabs[index]->rect;GetCursorPos(&g_mouseStart);SetCapture(hwnd);return 0;
            }
        }
        bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
        if(!ctrl)for(auto& item:g_visualIcons)item.selected=false;
        if(ctrl){box->selected=!box->selected;if(!box->selected){g_visualDirty=true;PaintAll();}return 0;}
        if(!box->selected)for(auto& candidate:g_layout->boxes)candidate.selected=false;
        box->selected=true;g_drag=true;g_resizing=hwnd==d->grip;g_dragId=box->id;g_boxStart=box->rect;GetCursorPos(&g_mouseStart);g_dragStart.clear();g_boxDragStart.clear();
        for(const auto& candidate:g_layout->boxes)if(candidate.selected)g_boxDragStart.push_back({candidate.id,candidate.rect});
        for(const auto& e:g_snapshot.entries)if(HasPath(*box,e.path))g_dragStart.push_back({e.path,e.position});
        SetCapture(hwnd);return 0;
    }
    case WM_MOUSEMOVE:
        if(g_tabPending&&GetCapture()==hwnd) {
            POINT p{};GetCursorPos(&p);LONG dx=p.x-g_mouseStart.x,dy=p.y-g_mouseStart.y;
            if(abs(dx)<4&&abs(dy)<4)return 0;
            if(auto* tab=FindBox(g_dragId)) {
                Box* root=GroupRoot(*tab);tab->groupId.clear();tab->activeTabId.clear();
                if(root!=tab&&root->activeTabId==tab->id)root->activeTabId.clear();
                tab->rect=g_boxStart;tab->selected=true;g_boxDragStart={{tab->id,g_boxStart}};g_drag=true;g_tabPending=false;g_tabDetached=true;g_resizing=false;g_visualDirty=true;box=tab;
            }
        }
        if(g_drag && GetCapture()==hwnd) {
            if(auto* dragged=FindBox(g_dragId))box=dragged;
            POINT p{};GetCursorPos(&p);LONG dx=p.x-g_mouseStart.x,dy=p.y-g_mouseStart.y;box->rect=g_boxStart;
            if(g_resizing){box->rect.right=max(box->rect.left+240,box->rect.right+dx);box->rect.bottom=max(box->rect.top+HeaderHeight()+100,box->rect.bottom+dy);box->rect=FitGrid(*box,g_snapshot);}
            else {for(auto& start:g_boxDragStart)if(auto* selected=FindBox(start.first)){selected->rect=start.second;OffsetRect(&selected->rect,dx,dy);}if(g_nativeHidden){RefreshVisualGeometry();}else {std::vector<DesktopMove> moves=g_dragStart;for(auto& m:moves){m.position.x+=dx;m.position.y+=dy;}if(!moves.empty())QueueMoves(moves);}}
            PaintAll();
        }
        return 0;
    case WM_CANCELMODE:
        g_boxButton=0;g_drag=false;g_tabPending=false;g_tabDetached=false;g_resizing=false;g_boxDragStart.clear();CancelIconGesture();return 0;
    case WM_LBUTTONUP:
        if(g_tabPending){g_tabPending=false;ReleaseCapture();if(auto* tab=FindBox(g_dragId)){if(auto* root=GroupRoot(*tab)){root->activeTabId=tab->id;g_visualDirty=true;SaveLayout(*g_layout);PaintAll();}}return 0;}
        if(g_boxButton){int button=g_boxButton;g_boxButton=0;ReleaseCapture();if(button==1)ToggleBoxView(*box);else if(button==2){box->collapsed=!box->collapsed;g_visualDirty=true;SaveLayout(*g_layout);PaintAll();}else {auto id=box->id;g_layout->boxes.erase(std::remove_if(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const auto& b){return b.id==id;}),g_layout->boxes.end());PostMessageW(g_manager,WM_APP+11,0,0);}return 0;}
        if(g_drag){
            if(auto* dragged=FindBox(g_dragId))box=dragged;
            g_drag=false;ReleaseCapture();
            if(g_resizing)Arrange(*box);
            else {
                POINT cursor{};GetCursorPos(&cursor);cursor.x-=GetSystemMetrics(SM_XVIRTUALSCREEN);cursor.y-=GetSystemMetrics(SM_YVIRTUALSCREEN);
                Box* source=GroupRoot(*box);Box* destination=nullptr;
                for(auto& candidate:g_layout->boxes)if(IsGroupRoot(candidate)&&candidate.id!=source->id) {
                    RECT header=DisplayRect(candidate);header.bottom=header.top+HeaderHeight();
                    if(PtInRect(&header,cursor)){destination=&candidate;break;}
                }
                if(destination) {
                    for(auto& candidate:g_layout->boxes)if(GroupRoot(candidate)->id==source->id) {candidate.groupId=destination->id;candidate.rect=destination->rect;candidate.color=destination->color;}
                    destination->activeTabId=box->id;g_visualDirty=true;g_boxDragStart.clear();SaveLayout(*g_layout);PostMessageW(g_manager,WM_APP+11,0,0);return 0;
                }
            }
            const bool rebuildDecorations=g_tabDetached;g_tabDetached=false;
            g_boxDragStart.clear();SaveLayout(*g_layout);
            if(rebuildDecorations)PostMessageW(g_manager,WM_APP+11,0,0);
        }return 0;
    case WM_CAPTURECHANGED:if(g_drag){g_drag=false;if(g_resizing)Arrange(*box);g_boxDragStart.clear();SaveLayout(*g_layout);}return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
LRESULT CALLBACK ManagerProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_APP+10){FinishRename(true);return 0;}
    if(message==WM_APP+11){Rebuild();return 0;}
    if(message==WM_TIMER) {
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
                    bool resourcesChanged=g_snapshot.entries.size()!=next.entries.size() || g_snapshot.iconSize!=next.iconSize || g_snapshot.spacing.x!=next.spacing.x || g_snapshot.spacing.y!=next.spacing.y || g_snapshot.iconMode!=next.iconMode;
                    if(!resourcesChanged)for(const auto& entry:next.entries)if(!FindEntry(g_snapshot,entry.path)){resourcesChanged=true;break;}
                    g_snapshot=std::move(next);if(g_nativeHidden&&resourcesChanged)g_visualDirty=true;
                }
            }
        }
        return 0;
    }
    if(message==WM_DISPLAYCHANGE||message==WM_DPICHANGED){DestroyDecorations();Attach();return 0;}
    return DefWindowProcW(hwnd,message,wp,lp);
}
}
bool CreateCanvas(HINSTANCE instance,HWND,Layout* layout) {
    g_instance=instance;g_layout=layout;g_visible=true;
    Gdiplus::GdiplusStartupInput input;if(Gdiplus::GdiplusStartup(&g_token,&input,nullptr)!=Gdiplus::Ok)return false;
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DecorationProc;wc.lpszClassName=L"nestlone-D.Decoration";wc.style=CS_DBLCLKS;wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&wc);
    wc.lpfnWndProc=ManagerProc;wc.lpszClassName=L"nestlone-D.PositionRules";RegisterClassW(&wc);
    g_manager=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,instance,nullptr);
    if(!g_manager)return false;
    SetTimer(g_manager,1,350,nullptr);Attach();PollDesktop(g_snapshot);return true;
}
void DestroyCanvas() {
    CancelDesktopMoves();DestroyDecorations();if(g_manager)DestroyWindow(g_manager);g_manager=nullptr;
    if(g_editFont){DeleteObject(g_editFont);g_editFont=nullptr;}
    if(g_token){Gdiplus::GdiplusShutdown(g_token);g_token=0;}
}
bool CanvasVisible(){return g_visible;}
void CanvasSetOpacity(int opacity){if(g_layout){g_layout->opacity=std::clamp(opacity,20,100);PaintAll();}}
void HandleCanvasCommand(CanvasCommand command) {
    if(!g_layout)return;
    if(command==CanvasCommand::Toggle) {
        g_visible=!g_visible;CancelDesktopMoves();g_pending=0;g_moves.clear();g_snapshot={};FinishRename(true);
        if(g_background)ShowWindow(g_background,g_visible?SW_SHOWNOACTIVATE:SW_HIDE);
        if(IsWindow(g_hiddenListview))ShowWindow(g_hiddenListview,g_visible?SW_HIDE:SW_SHOWNOACTIVATE);
        if(g_visible){DesktopSnapshot current;if(ReadDesktop(current)){g_snapshot=std::move(current);g_visualDirty=true;}}
        PaintAll();
    }
    if(command==CanvasCommand::NewBox) {Box box;box.id=std::to_wstring(GetTickCount64());box.title=L"新盒子";box.rect={160,160,520,480};g_layout->boxes.push_back(box);Rebuild();}
    if(command==CanvasCommand::Reload){*g_layout=LoadLayout();Rebuild();}
    if(command==CanvasCommand::Exit)DestroyCanvas();
}
}

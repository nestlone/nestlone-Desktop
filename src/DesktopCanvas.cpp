#include "DesktopCanvas.h"
#include "DesktopSession.h"
#include "DesktopHost.h"
#include "log.h"
#include <windowsx.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <algorithm>
#include <memory>

namespace nestlone {
namespace {
Layout* g_layout=nullptr;
HWND g_manager=nullptr,g_background=nullptr,g_edit=nullptr;
HINSTANCE g_instance=nullptr;
ULONG_PTR g_token=0;
bool g_visible=true,g_drag=false,g_resizing=false;
db::HostInfo g_host;
DesktopSnapshot g_snapshot;
uint64_t g_pending=0;
std::vector<DesktopMove> g_moves,g_dragStart;
POINT g_mouseStart{};
RECT g_boxStart{};
std::wstring g_dragId,g_editId,g_status;
HFONT g_editFont=nullptr;
struct Decoration {std::wstring id;HWND header=nullptr,grip=nullptr;};
std::vector<std::unique_ptr<Decoration>> g_decorations;
constexpr int kHeader=32;
int HeaderHeight(){return MulDiv(kHeader,static_cast<int>(g_host.listview?GetDpiForWindow(g_host.listview):96),96);}
Box* FindBox(const std::wstring& id) {
    if(g_layout)for(auto& box:g_layout->boxes)if(box.id==id)return &box;
    return nullptr;
}
bool HasPath(const Box& box,const std::wstring& path) {
    return std::any_of(box.items.begin(),box.items.end(),[&](const auto& p){return _wcsicmp(p.c_str(),path.c_str())==0;});
}
const DesktopEntry* FindEntry(const DesktopSnapshot& snapshot,const std::wstring& path) {
    for(const auto& e:snapshot.entries)if(_wcsicmp(e.path.c_str(),path.c_str())==0)return &e;
    return nullptr;
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
    for(const auto& box:layout.boxes)Rounded(g,DisplayRect(box),Background(box.color,layout.opacity));
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
    if(!box)RenderPixels(pixels,r.right,r.bottom,*g_layout);
    else {
        Gdiplus::Bitmap surface(r.right,r.bottom,r.right*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
        Gdiplus::Graphics g(&surface);g.Clear(Gdiplus::Color(0,0,0,0));g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Rounded(g,r,Background(box->color,g_layout->opacity));
        if(grip)Label(g,L"◢",r);
        else {
            const int h=HeaderHeight();
            Label(g,box->title,{h,0,r.right-3*h,h});
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
        auto found=std::find_if(g_moves.begin(),g_moves.end(),[&](const auto& m){return _wcsicmp(m.path.c_str(),move.path.c_str())==0;});
        if(found==g_moves.end())g_moves.push_back(move);else *found=move;
    }
    g_pending=QueueDesktopMoves(g_moves);
}
void Arrange(Box& box) {
    if(!g_snapshot.readable || box.collapsed)return;
    if(g_snapshot.autoArrange){g_status=L"请先关闭桌面“自动排列图标”";return;}
    if(!g_snapshot.iconMode){g_status=L"当前桌面视图不支持自由定位";return;}
    const int sx=g_snapshot.spacing.x,sy=g_snapshot.spacing.y;
    const int columns=max(1,(box.rect.right-box.rect.left-16)/sx);
    std::vector<DesktopMove> moves;int slot=0;
    for(const auto& path:box.items)if(FindEntry(g_snapshot,path)) {
        LONG x=box.rect.left+8+(slot%columns)*sx;
        LONG y=box.rect.top+HeaderHeight()+8+(slot/columns)*sy;
        if(x+sx>box.rect.right-8 || y+sy>box.rect.bottom-8){g_status=L"盒子空间不足，部分图标保持原位；请扩大盒子";break;}
        RECT target{x,y,x+sx,y+sy};bool occupied=false;
        for(const auto& e:g_snapshot.entries)if(!HasPath(box,e.path)) {RECT overlap{};if(IntersectRect(&overlap,&target,&e.bounds)){occupied=true;break;}}
        if(occupied){g_status=L"目标位置有未收纳图标，请先移开或使用“收纳区域内图标”";return;}
        moves.push_back({path,{x+(sx-g_snapshot.iconSize)/2,y}});++slot;
    }
    if(!moves.empty())QueueMoves(moves);
}
void Observe(const DesktopSnapshot& before,const DesktopSnapshot& after) {
    if(!before.readable||before.listview!=after.listview||before.process!=after.process)return;
    bool changed=false;
    for(const auto& entry:after.entries) {
        auto old=FindEntry(before,entry.path);
        if(old && old->position.x==entry.position.x && old->position.y==entry.position.y)continue;
        int owner=-1;for(int i=0;i<static_cast<int>(g_layout->boxes.size());++i)if(HasPath(g_layout->boxes[i],entry.path)){owner=i;break;}
        int target=ContainingBox(entry);
        if(target==owner)continue;
        for(auto& box:g_layout->boxes)box.items.erase(std::remove_if(box.items.begin(),box.items.end(),[&](const auto& p){return _wcsicmp(p.c_str(),entry.path.c_str())==0;}),box.items.end());
        if(target>=0)AddItem(g_layout->boxes[target],entry.path);
        changed=true;
    }
    if(changed)SaveLayout(*g_layout);
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
}
LRESULT CALLBACK DecorationProc(HWND,UINT,WPARAM,LPARAM);
bool Attach() {
    auto host=db::DiscoverDesktopHost();
    if(!host.listview || !host.defviewParent)return false;
    if(g_background && IsWindow(g_background) && g_host.listview==host.listview)return true;
    DestroyDecorations();g_host=host;RestoreLegacyMask(host.listview);
    POINT origin=ParentPoint({0,0});
    g_background=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,
        L"nestlone-D.Decoration",L"",WS_CHILD,origin.x,origin.y,GetSystemMetrics(SM_CXVIRTUALSCREEN),GetSystemMetrics(SM_CYVIRTUALSCREEN),host.defviewParent,nullptr,g_instance,nullptr);
    if(!g_background)return false;
    // Same parent as SHELLDLL_DefView, immediately below the complete native view.
    SetWindowPos(g_background,host.defview,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
    for(const auto& box:g_layout->boxes) {
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
void Menu(HWND hwnd,Box& box,POINT point) {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"重命名盒子");AppendMenuW(menu,MF_STRING,2,L"整理盒内图标位置");
    AppendMenuW(menu,MF_STRING,3,L"收纳区域内图标");AppendMenuW(menu,MF_STRING,4,box.collapsed?L"展开背景":L"收起背景（图标保留）");
    AppendMenuW(menu,MF_STRING,5,L"删除盒子（保留图标与文件）");
    if(!g_status.empty())AppendMenuW(menu,MF_STRING|MF_DISABLED,0,g_status.c_str());
    int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd,nullptr);DestroyMenu(menu);
    if(command==1)BeginRename(box);
    if(command==2){Arrange(box);if(!g_status.empty())MessageBoxW(hwnd,g_status.c_str(),L"图标位置",MB_OK|MB_ICONINFORMATION);}
    if(command==3) {
        int index=static_cast<int>(&box-g_layout->boxes.data());
        for(const auto& entry:g_snapshot.entries)if(ContainingBox(entry)==index)AssignDesktopItem(*g_layout,entry.path,index,entry.position);
        SaveLayout(*g_layout);
    }
    if(command==4){box.collapsed=!box.collapsed;SaveLayout(*g_layout);PaintAll();}
    if(command==5){auto id=box.id;g_layout->boxes.erase(std::remove_if(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const auto& b){return b.id==id;}),g_layout->boxes.end());PostMessageW(g_manager,WM_APP+11,0,0);}
}
LRESULT CALLBACK DecorationProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_NCCREATE){auto c=reinterpret_cast<CREATESTRUCTW*>(lp);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(c->lpCreateParams));}
    auto* d=reinterpret_cast<Decoration*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    Box* box=d?FindBox(d->id):nullptr;
    if(!box) {
        if(message==WM_NCHITTEST)return HTTRANSPARENT;
        if(message==WM_ERASEBKGND)return 1;
        if(message==WM_PAINT){PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);if(g_layout)PaintWindow(hwnd);return 0;}
        return DefWindowProcW(hwnd,message,wp,lp);
    }
    switch(message) {
    case WM_MOUSEACTIVATE:return MA_NOACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};BeginPaint(hwnd,&ps);EndPaint(hwnd,&ps);PaintWindow(hwnd,box,hwnd==d->grip);return 0;}
    case WM_LBUTTONDBLCLK:if(hwnd==d->header && GET_X_LPARAM(lp)<box->rect.right-box->rect.left-3*HeaderHeight())BeginRename(*box);return 0;
    case WM_CONTEXTMENU:{POINT p{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};if(p.x==-1)GetCursorPos(&p);Menu(hwnd,*box,p);return 0;}
    case WM_LBUTTONDOWN: {
        FinishRename(true);int h=HeaderHeight(),x=GET_X_LPARAM(lp),width=box->rect.right-box->rect.left;
        if(hwnd==d->header && x>=width-3*h) {
            if(x>=width-h){auto id=box->id;g_layout->boxes.erase(std::remove_if(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const auto& b){return b.id==id;}),g_layout->boxes.end());PostMessageW(g_manager,WM_APP+11,0,0);}
            else if(x>=width-2*h){box->collapsed=!box->collapsed;SaveLayout(*g_layout);PaintAll();}
            else {g_status.clear();Arrange(*box);if(!g_status.empty())MessageBoxW(hwnd,g_status.c_str(),L"图标位置",MB_OK|MB_ICONINFORMATION);}
            return 0;
        }
        g_drag=true;g_resizing=hwnd==d->grip;g_dragId=box->id;g_boxStart=box->rect;GetCursorPos(&g_mouseStart);g_dragStart.clear();
        for(const auto& e:g_snapshot.entries)if(HasPath(*box,e.path))g_dragStart.push_back({e.path,e.position});
        SetCapture(hwnd);return 0;
    }
    case WM_MOUSEMOVE:
        if(g_drag && GetCapture()==hwnd) {
            POINT p{};GetCursorPos(&p);LONG dx=p.x-g_mouseStart.x,dy=p.y-g_mouseStart.y;box->rect=g_boxStart;
            if(g_resizing){box->rect.right=max(box->rect.left+240,box->rect.right+dx);box->rect.bottom=max(box->rect.top+HeaderHeight()+100,box->rect.bottom+dy);}
            else {OffsetRect(&box->rect,dx,dy);std::vector<DesktopMove> moves=g_dragStart;for(auto& m:moves){m.position.x+=dx;m.position.y+=dy;}if(!moves.empty())QueueMoves(moves);}
            PaintAll();
        }
        return 0;
    case WM_LBUTTONUP:if(g_drag){g_drag=false;ReleaseCapture();SaveLayout(*g_layout);}return 0;
    case WM_CAPTURECHANGED:if(g_drag){g_drag=false;SaveLayout(*g_layout);}return 0;
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
                if(!settling && !g_drag && !(GetAsyncKeyState(VK_LBUTTON)&0x8000) && !(GetAsyncKeyState(VK_RBUTTON)&0x8000))Observe(g_snapshot,next);
                // Keep the pre-drag baseline until Explorer has finished native drag/drop.
                if(!(GetAsyncKeyState(VK_LBUTTON)&0x8000) && !(GetAsyncKeyState(VK_RBUTTON)&0x8000))g_snapshot=std::move(next);
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
        if(g_background)ShowWindow(g_background,g_visible?SW_SHOWNOACTIVATE:SW_HIDE);PaintAll();
    }
    if(command==CanvasCommand::NewBox) {Box box;box.id=std::to_wstring(GetTickCount64());box.title=L"新盒子";box.rect={160,160,520,480};g_layout->boxes.push_back(box);Rebuild();}
    if(command==CanvasCommand::Reload){*g_layout=LoadLayout();Rebuild();}
    if(command==CanvasCommand::Exit)DestroyCanvas();
}
}

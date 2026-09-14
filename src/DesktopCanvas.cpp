#include "DesktopCanvas.h"
#include "Theme.h"
#include "DesktopItems.h"
#include <windowsx.h>
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <gdiplus.h>
#include <commctrl.h>
#include "log.h"

namespace nestlone {
namespace {
constexpr int kHeader = 32;
constexpr int kItemHeight = 26;
constexpr int kMinWidth = 180;
constexpr int kMinHeight = 100;
constexpr int kCorner = 14;
HWND g_canvas = nullptr;
Layout* g_layout = nullptr;
int g_activeBox = -1;
int g_activeItem = -1;
bool g_resize = false;
int g_resizeEdges = 0;
POINT g_last{};

enum ResizeEdge { EdgeLeft = 1, EdgeTop = 2, EdgeRight = 4, EdgeBottom = 8 };

int ResizeEdgesAt(const Box& box, POINT point) {
    if (box.collapsed) return 0;
    constexpr int grip = 8;
    int edges = 0;
    if (point.x >= box.rect.left && point.x < box.rect.left + grip) edges |= EdgeLeft;
    if (point.x <= box.rect.right && point.x > box.rect.right - grip) edges |= EdgeRight;
    if (point.y >= box.rect.top && point.y < box.rect.top + grip) edges |= EdgeTop;
    if (point.y <= box.rect.bottom && point.y > box.rect.bottom - grip) edges |= EdgeBottom;
    return edges;
}

RECT ItemRect(const Box& box, int index) {
    if (!box.iconView) {
        return {box.rect.left + 10, box.rect.top + kHeader + 6 + index * 26, box.rect.right - 10, box.rect.top + kHeader + 6 + (index + 1) * 26};
    }
    const int columns = max(1, (box.rect.right - box.rect.left - 20) / 72);
    const int tileWidth = (box.rect.right - box.rect.left - 20) / columns;
    const int column = index % columns;
    const int row = index / columns;
    return {box.rect.left + 10 + column * tileWidth, box.rect.top + kHeader + 6 + row * 72, box.rect.left + 10 + (column + 1) * tileWidth, box.rect.top + kHeader + 6 + row * 72 + 68};
}

RECT DisplayRect(const Box& box) {
    RECT rect = box.rect;
    if (box.collapsed) rect.bottom = rect.top + kHeader;
    return rect;
}

int HitBox(POINT point) {
    for (int i = static_cast<int>(g_layout->boxes.size()) - 1; i >= 0; --i) { const RECT visible = DisplayRect(g_layout->boxes[i]); if (PtInRect(&visible, point)) return i; }
    return -1;
}

int HitItem(const Box& box, POINT point) {
    if (box.collapsed) return -1;
    for (int i = 0; i < static_cast<int>(box.items.size()); ++i) {
        const RECT item = ItemRect(box, i);
        if (PtInRect(&item, point)) return i;
    }
    return -1;
}

Gdiplus::Color Tint(COLORREF c, BYTE a = 255) {
    return Gdiplus::Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

void Rounded(Gdiplus::Graphics& g, const RECT& r, Gdiplus::Color color) {
    Gdiplus::GraphicsPath p;
    const float x = static_cast<float>(r.left), y = static_cast<float>(r.top);
    const float w = static_cast<float>(r.right-r.left-1), h = static_cast<float>(r.bottom-r.top-1);
    constexpr float d = 14;
    p.AddArc(x,y,d,d,180,90); p.AddArc(x+w-d,y,d,d,270,90);
    p.AddArc(x+w-d,y+h-d,d,d,0,90); p.AddArc(x,y+h-d,d,d,90,90);
    p.CloseFigure();
    Gdiplus::SolidBrush brush(color);
    g.FillPath(&brush,&p);
}

void Label(Gdiplus::Graphics& g, const std::wstring& text, RECT r, COLORREF color,
           bool center=false, bool wrap=false, bool bold=false) {
    Gdiplus::Font font(L"Microsoft YaHei UI", 12.0f, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::StringFormat format;
    format.SetAlignment(center ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
    format.SetLineAlignment(wrap ? Gdiplus::StringAlignmentNear : Gdiplus::StringAlignmentCenter);
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    if (!wrap) format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    (void)color;
    Gdiplus::FontFamily family(L"Microsoft YaHei UI");
    Gdiplus::GraphicsPath glyphs;
    glyphs.AddString(text.c_str(),-1,&family,bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
        12.0f,Gdiplus::RectF(static_cast<float>(r.left),static_cast<float>(r.top),
        static_cast<float>(r.right-r.left),static_cast<float>(r.bottom-r.top)),&format);
    Gdiplus::Pen outline(Gdiplus::Color(255,28,48,60),1.5f);
    outline.SetLineJoin(Gdiplus::LineJoinRound);
    Gdiplus::SolidBrush ink(Gdiplus::Color(255,255,255,255));
    g.DrawPath(&outline,&glyphs);
    g.FillPath(&ink,&glyphs);
}

// Render against two mattes with DrawIconEx: preserves both modern alpha icons
// and legacy AND masks. GDI+ Bitmap(HICON) discards alpha for some Shell icons.
std::vector<DWORD> IconPixels(HICON icon,int size) {
    std::vector<DWORD> result;
    HDC dc=CreateCompatibleDC(nullptr);
    BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth=size; bi.bmiHeader.biHeight=-size;
    bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
    void* raw=nullptr;
    HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,&raw,nullptr,0);
    if(!bitmap || !raw) { if(bitmap)DeleteObject(bitmap); DeleteDC(dc); return result; }
    auto old=SelectObject(dc,bitmap);
    auto* pixels=static_cast<DWORD*>(raw);
    std::fill(pixels,pixels+size*size,0);
    DrawIconEx(dc,0,0,icon,size,size,0,nullptr,DI_NORMAL);
    GdiFlush();
    result.assign(pixels,pixels+size*size);
    std::fill(pixels,pixels+size*size,0x00ffffff);
    DrawIconEx(dc,0,0,icon,size,size,0,nullptr,DI_NORMAL);
    GdiFlush();
    for(int i=0;i<size*size;++i) {
        DWORD black=result[i],white=pixels[i];
        int delta=0;
        for(int shift=0;shift<24;shift+=8)
            delta=max(delta,static_cast<int>((white>>shift)&255)-static_cast<int>((black>>shift)&255));
        DWORD a=static_cast<DWORD>(255-delta);
        result[i]=(a<<24)|(min(a,(black>>16)&255)<<16)|(min(a,(black>>8)&255)<<8)|min(a,black&255);
    }
    SelectObject(dc,old);DeleteObject(bitmap);DeleteDC(dc);
    return result;
}

void PaintBox(Gdiplus::Graphics& g, const Box& box, int opacity) {
    const RECT visible = DisplayRect(box);
    const BYTE alpha = static_cast<BYTE>(max(20,min(100,opacity))*255/100);
    // The background is a distinct drawing operation. Never infer alpha from RGB.
    Rounded(g,visible,Tint(box.color,alpha));
    Gdiplus::Pen divider(Tint(theme::panelEdge,alpha),1);
    if (!box.collapsed) g.DrawLine(&divider,visible.left+12,visible.top+kHeader,visible.right-12,visible.top+kHeader);
    RECT title{visible.left+62,visible.top,visible.right-62,visible.top+kHeader};
    Label(g,box.title,title,theme::title,true,false,true);
    RECT toggle{visible.right-58,visible.top,visible.right-30,visible.top+kHeader};
    Label(g,box.iconView ? L"▦" : L"≡",toggle,theme::title,true);
    RECT collapse{visible.right-30,visible.top,visible.right-6,visible.top+kHeader};
    Label(g,box.collapsed ? L"+" : L"−",collapse,theme::title,true);
    if (box.collapsed) return;
    for (int i=0;i<static_cast<int>(box.items.size());++i) {
        RECT item=ItemRect(box,i);
        if (item.bottom>box.rect.bottom-8) break;
        const auto& path=box.items[i];
        SHFILEINFOW fi{};
        if (SHGetFileInfoW(path.c_str(),0,&fi,sizeof(fi),SHGFI_ICON | SHGFI_DISPLAYNAME |
            (box.iconView ? SHGFI_LARGEICON : SHGFI_SMALLICON))) {
            int size=box.iconView ? 32 : 16;
            int x=box.iconView ? (item.left+item.right-size)/2 : item.left+4;
            auto pixels=IconPixels(fi.hIcon,size);
            if(!pixels.empty()) {
                Gdiplus::Bitmap icon(size,size,size*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(pixels.data()));
                g.DrawImage(&icon,x,item.top+(box.iconView ? 2 : 5),size,size);
            }
            DestroyIcon(fi.hIcon);
        }
        std::wstring name=fi.szDisplayName[0] ? fi.szDisplayName : std::filesystem::path(path).filename().wstring();
        if (box.iconView) { item.left+=2; item.right-=2; item.top+=36; }
        else item.left+=26;
        Label(g,name,item,theme::text,box.iconView,box.iconView);
    }
}

bool RenderPixels(void* pixels, int width, int height, const Layout& layout) {
    Gdiplus::Bitmap surface(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
    Gdiplus::Graphics g(&surface);
    g.Clear(Gdiplus::Color(0,0,0,0));
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    for (const Box& box:layout.boxes) PaintBox(g,box,layout.opacity);
    g.Flush(Gdiplus::FlushIntentionSync);
    return g.GetLastStatus()==Gdiplus::Ok;
}

void Paint(HWND hwnd, HDC target) {
    RECT client{}; GetClientRect(hwnd,&client);
    if (client.right<=0 || client.bottom<=0) return;
    HDC memory=CreateCompatibleDC(target);
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=client.right; info.bmiHeader.biHeight=-client.bottom;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* pixels=nullptr;
    HBITMAP bitmap=CreateDIBSection(memory,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if (!bitmap || !pixels) { DeleteDC(memory); return; }
    HGDIOBJ old=SelectObject(memory,bitmap);
    if (RenderPixels(pixels,client.right,client.bottom,*g_layout)) {
        RECT wr{}; GetWindowRect(hwnd,&wr);
        POINT position{wr.left,wr.top}, source{};
        SIZE size{client.right,client.bottom};
        BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        if (!UpdateLayeredWindow(hwnd,nullptr,&position,&size,memory,&source,0,&blend,ULW_ALPHA))
            db::LogF("UpdateLayeredWindow failed: %lu",GetLastError());
    }
    SelectObject(memory,old); DeleteObject(bitmap); DeleteDC(memory);
}

void SaveAndRedraw() { SaveLayout(*g_layout); InvalidateRect(g_canvas, nullptr, TRUE); }

HWND g_rename=nullptr;
HWND g_nameEdit=nullptr;
HFONT g_renameFont=nullptr;
std::wstring g_renameId;

bool RenameBox(const std::wstring& id,std::wstring value) {
    auto first=value.find_first_not_of(L" \t\r\n");
    if(first==std::wstring::npos) return false;
    value=value.substr(first,value.find_last_not_of(L" \t\r\n")-first+1);
    for(auto& box:g_layout->boxes) if(box.id==id) {
        box.title=value.substr(0,80);
        SaveAndRedraw(); return true;
    }
    return false;
}

LRESULT CALLBACK RenameEditProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_KEYDOWN && (wp==VK_RETURN || wp==VK_ESCAPE)) {
        SendMessageW(GetParent(hwnd),WM_COMMAND,wp==VK_RETURN ? IDOK : IDCANCEL,0); return 0;
    }
    if(msg==WM_NCDESTROY) RemoveWindowSubclass(hwnd,RenameEditProc,1);
    return DefSubclassProc(hwnd,msg,wp,lp);
}

LRESULT CALLBACK RenameProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_COMMAND:
        if(LOWORD(wp)==IDOK) {
            wchar_t text[81]{};
            GetWindowTextW(g_nameEdit,text,81);
            if(!RenameBox(g_renameId,text)) { MessageBeep(MB_ICONWARNING);SetFocus(g_nameEdit);return 0; }
            DestroyWindow(hwnd);return 0;
        }
        if(LOWORD(wp)==IDCANCEL) {DestroyWindow(hwnd);return 0;}
        break;
    case WM_CLOSE: DestroyWindow(hwnd);return 0;
    case WM_DESTROY:
        g_rename=nullptr;g_nameEdit=nullptr;g_renameId.clear();
        if(g_renameFont) {DeleteObject(g_renameFont);g_renameFont=nullptr;}
        return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

void BeginRename(int index) {
    if(g_rename) {SetForegroundWindow(g_rename);return;}
    if(index<0 || index>=static_cast<int>(g_layout->boxes.size()))return;
    const Box& box=g_layout->boxes[index];
    g_renameId=box.id;
    HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=RenameProc;
    wc.lpszClassName=L"nestlone-D.Rename";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClassW(&wc);
    g_rename=CreateWindowExW(WS_EX_TOOLWINDOW,wc.lpszClassName,L"重命名盒子",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,CW_USEDEFAULT,CW_USEDEFAULT,360,155,
        nullptr,nullptr,instance,nullptr);
    if(!g_rename)return;
    g_renameFont=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    g_nameEdit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",box.title.c_str(),
        WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,20,20,302,28,g_rename,nullptr,instance,nullptr);
    SendMessageW(g_nameEdit,WM_SETFONT,reinterpret_cast<WPARAM>(g_renameFont),TRUE);
    SendMessageW(g_nameEdit,EM_SETLIMITTEXT,80,0);
    SetWindowSubclass(g_nameEdit,RenameEditProc,1,0);
    HWND ok=CreateWindowW(L"BUTTON",L"保存",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
        150,65,80,28,g_rename,reinterpret_cast<HMENU>(IDOK),instance,nullptr);
    HWND cancel=CreateWindowW(L"BUTTON",L"取消",WS_CHILD|WS_VISIBLE|WS_TABSTOP,
        242,65,80,28,g_rename,reinterpret_cast<HMENU>(IDCANCEL),instance,nullptr);
    SendMessageW(ok,WM_SETFONT,reinterpret_cast<WPARAM>(g_renameFont),TRUE);
    SendMessageW(cancel,WM_SETFONT,reinterpret_cast<WPARAM>(g_renameFont),TRUE);
    ShowWindow(g_rename,SW_SHOWNORMAL);SetForegroundWindow(g_rename);
    SetFocus(g_nameEdit);SendMessageW(g_nameEdit,EM_SETSEL,0,-1);
}

void AddBox() {
    Box box;
    box.id = std::to_wstring(GetTickCount64());
    box.title = L"新盒子";
    const int offset = static_cast<int>(g_layout->boxes.size()) * 24;
    box.rect = {80 + offset, 80 + offset, 360 + offset, 280 + offset};
    g_layout->boxes.push_back(std::move(box));
    SaveAndRedraw();
}

void ShowContextMenu(HWND hwnd, POINT point) {
    const int boxIndex = HitBox(point);
    const int itemIndex = boxIndex >= 0 ? HitItem(g_layout->boxes[boxIndex], point) : -1;
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"新建盒子");
    if (boxIndex >= 0) {
        if (itemIndex >= 0) {
            AppendMenuW(menu, MF_STRING, 2, L"打开项目");
            AppendMenuW(menu, MF_STRING, 3, L"从盒子移除");
        } else {
            AppendMenuW(menu, MF_STRING, 4, g_layout->boxes[boxIndex].collapsed ? L"展开盒子" : L"折叠盒子");
            AppendMenuW(menu, MF_STRING, 5, L"删除盒子");
        }
    }
    ClientToScreen(hwnd, &point);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (command == 1) AddBox();
    if (command == 2 && itemIndex >= 0) ShellExecuteW(hwnd, L"open", g_layout->boxes[boxIndex].items[itemIndex].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (command == 3 && itemIndex >= 0) { SetDesktopItemHidden(g_layout->boxes[boxIndex].items[itemIndex], false); g_layout->boxes[boxIndex].items.erase(g_layout->boxes[boxIndex].items.begin() + itemIndex); SaveAndRedraw(); }
    if (command == 4 && boxIndex >= 0) { g_layout->boxes[boxIndex].collapsed = !g_layout->boxes[boxIndex].collapsed; SaveAndRedraw(); }
    if (command == 5 && boxIndex >= 0) { for (const auto& item : g_layout->boxes[boxIndex].items) SetDesktopItemHidden(item, false); g_layout->boxes.erase(g_layout->boxes.begin() + boxIndex); SaveAndRedraw(); }
}

LRESULT CALLBACK CanvasProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: DragAcceptFiles(hwnd, TRUE); return 0;
    case WM_SIZE: InvalidateRect(hwnd, nullptr, TRUE); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); Paint(hwnd, dc); EndPaint(hwnd, &ps); return 0; }
    case WM_LBUTTONDBLCLK: {
        POINT p{GET_X_LPARAM(lParam),GET_Y_LPARAM(lParam)};
        int index=HitBox(p);
        if(index>=0) {
            const RECT& r=g_layout->boxes[index].rect;
            if(p.y<r.top+kHeader && p.x<r.right-62 && p.x>=r.left+12) BeginRename(index);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        g_activeBox = HitBox(point);
        if (g_activeBox < 0) return 0;
        const Box& box = g_layout->boxes[g_activeBox];
        const int item = HitItem(box, point);
        if (item >= 0) { g_activeItem = item; return 0; }
        if (point.x >= box.rect.right - 58 && point.x < box.rect.right - 30 && point.y < box.rect.top + kHeader) {
            Box& mutableBox = g_layout->boxes[g_activeBox];
            mutableBox.iconView = !mutableBox.iconView;
            SaveAndRedraw();
            g_activeBox = -1;
            return 0;
        }
        if (point.x >= box.rect.right - 30 && point.y < box.rect.top + kHeader) {
            Box& mutableBox = g_layout->boxes[g_activeBox];
            mutableBox.collapsed = !mutableBox.collapsed;
            SaveAndRedraw();
            g_activeBox = -1;
            return 0;
        }
        const int edges = ResizeEdgesAt(box, point);
        if (edges) {
            g_resize = true;
            g_resizeEdges = edges;
            g_last = point;
            SetCapture(hwnd);
            return 0;
        }
        if (point.x > box.rect.right - 18 && point.y > box.rect.bottom - 18) g_resize = true;
        else if (point.y < box.rect.top + kHeader) g_resize = false;
        else { g_activeBox = -1; return 0; }
        g_last = point;
        SetCapture(hwnd);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (GetCapture() != hwnd) {
            const int hoveredBox = HitBox(POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
            if (hoveredBox >= 0) {
                const int edges = ResizeEdgesAt(g_layout->boxes[hoveredBox], POINT{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});
                if ((edges & (EdgeLeft | EdgeRight)) && (edges & (EdgeTop | EdgeBottom))) SetCursor(LoadCursorW(nullptr, IDC_SIZENWSE));
                else if (edges & (EdgeLeft | EdgeRight)) SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                else if (edges & (EdgeTop | EdgeBottom)) SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                else SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
        }
        if (g_activeBox >= 0 && GetCapture() == hwnd) {
            Box& box = g_layout->boxes[g_activeBox];
            const int dx = GET_X_LPARAM(lParam) - g_last.x;
            const int dy = GET_Y_LPARAM(lParam) - g_last.y;
            if (g_resize) {
                if (g_resizeEdges & EdgeLeft) box.rect.left = min(box.rect.right - kMinWidth, box.rect.left + dx);
                if (g_resizeEdges & EdgeRight) box.rect.right = max(box.rect.left + kMinWidth, box.rect.right + dx);
                if (g_resizeEdges & EdgeTop) box.rect.top = min(box.rect.bottom - kMinHeight, box.rect.top + dy);
                if (g_resizeEdges & EdgeBottom) box.rect.bottom = max(box.rect.top + kMinHeight, box.rect.bottom + dy);
            }
            else OffsetRect(&box.rect, dx, dy);
            g_last = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (g_activeItem >= 0 && g_activeBox >= 0) {
            Box& box = g_layout->boxes[g_activeBox];
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (HitItem(box, point) == g_activeItem) ShellExecuteW(hwnd, L"open", box.items[g_activeItem].c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (GetCapture() == hwnd) { ReleaseCapture(); SaveAndRedraw(); }
        g_activeBox = -1; g_activeItem = -1; g_resize = false; g_resizeEdges = 0;
        return 0;
    case WM_DROPFILES: {
        HDROP drop = reinterpret_cast<HDROP>(wParam);
        POINT point{}; DragQueryPoint(drop, &point);
        const int boxIndex = HitBox(point);
        if (boxIndex >= 0) {
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            bool changed = false;
            for (UINT i = 0; i < count; ++i) { const UINT size = DragQueryFileW(drop, i, nullptr, 0); std::wstring path(size, L'\0'); DragQueryFileW(drop, i, path.data(), size + 1); if (AddItem(g_layout->boxes[boxIndex], path)) { SetDesktopItemHidden(path, true); changed = true; } }
            if (changed) SaveAndRedraw();
        }
        DragFinish(drop);
        return 0;
    }
    case WM_CONTEXTMENU: { POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}; if (point.x == -1) GetCursorPos(&point); ScreenToClient(hwnd, &point); ShowContextMenu(hwnd, point); return 0; }
    default: return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

bool CreateCanvas(HINSTANCE instance, HWND parent, Layout* layout) {
    g_layout = layout;
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = CanvasProc;
    wc.lpszClassName = L"nestlone-D.Canvas";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    RECT parentRect{};
    GetClientRect(parent, &parentRect);
    // Layered child windows are rejected on some Explorer/Windows configurations.
    // Use a borderless popup owned by the desktop host instead; it keeps the same
    // z-order intent while allowing the color-key transparency to work reliably.
    g_canvas = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        wc.lpszClassName, L"", WS_POPUP | WS_VISIBLE, 0, 0,
        parentRect.right, parentRect.bottom, parent, nullptr, instance, nullptr);
    if (!g_canvas) return false;
    InvalidateRect(g_canvas, nullptr, TRUE);
    UpdateWindow(g_canvas);
    return true;
}

void DestroyCanvas() { if(g_rename)DestroyWindow(g_rename); if (g_canvas) DestroyWindow(g_canvas); g_canvas = nullptr; }

bool CanvasVisible() { return g_canvas && IsWindowVisible(g_canvas); }

void CanvasSetOpacity(int opacity) {
    if (!g_canvas) return;
    if (g_layout) g_layout->opacity = max(20, min(100, opacity));
    // Layered desktop windows may not receive a normal WM_PAINT after an
    // invalidation. Render synchronously so the slider always has immediate
    // visual feedback.
    HDC dc = GetDC(g_canvas);
    if (dc) {
        Paint(g_canvas, dc);
        ReleaseDC(g_canvas, dc);
    }
}

void HandleCanvasCommand(CanvasCommand command) {
    if (!g_canvas) return;
    if (command == CanvasCommand::Toggle) ShowWindow(g_canvas, CanvasVisible() ? SW_HIDE : SW_SHOW);
    if (command == CanvasCommand::NewBox) AddBox();
    if (command == CanvasCommand::Reload) { *g_layout = LoadLayout(); if (g_layout->boxes.empty()) AddBox(); else InvalidateRect(g_canvas, nullptr, TRUE); }
    if (command == CanvasCommand::Exit) DestroyCanvas();
}
}

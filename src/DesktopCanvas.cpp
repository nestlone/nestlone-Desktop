#include "DesktopCanvas.h"
#include "DesktopSession.h"
#include "DesktopHost.h"
#include "resource.h"
#include "log.h"
#include <windowsx.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>
#pragma warning(push)
#pragma warning(disable:4996)
#include <locationapi.h>
#pragma warning(pop)
#include <wrl/client.h>
#include <algorithm>
#include <memory>
#include <vector>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <string>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <iomanip>
#include <sstream>

namespace nestlone {
namespace {
using Microsoft::WRL::ComPtr;
Layout* g_layout=nullptr;
HWND g_manager=nullptr,g_background=nullptr,g_edit=nullptr;
HINSTANCE g_instance=nullptr;
ULONG_PTR g_token=0;
HHOOK g_keyboardHook=nullptr;
HWINEVENTHOOK g_foregroundHook=nullptr;
bool g_visible=true,g_drag=false,g_resizing=false,g_resizeFromLeft=false,g_tabPending=false,g_tabDetached=false,g_nativeHidden=false;
bool g_interactivePaintQueued=false;
std::atomic_bool g_weatherRequestInFlight=false;
std::unique_ptr<Gdiplus::PrivateFontCollection> g_weatherIconFonts;
std::unique_ptr<Gdiplus::FontFamily> g_weatherIconFamily;
std::string g_weatherIconCss;
bool g_visualGeometryDirty=false;
int g_shellRefreshAttempts=0;
HWND g_dragPreview=nullptr;
std::wstring g_previewGroup,g_renderOnlyGroup;
void EndDragPreview();
HWND g_hiddenListview=nullptr;
db::HostInfo g_host;
DesktopSnapshot g_snapshot;
uint64_t g_pending=0;
std::vector<DesktopMove> g_moves,g_dragStart;
POINT g_mouseStart{};
RECT g_boxStart{};
std::wstring g_dragId,g_editId,g_status;
bool g_editGroupTitle=false;
std::vector<std::pair<std::wstring,RECT>> g_boxDragStart;
HFONT g_editFont=nullptr;
struct Decoration {std::wstring id;HWND header=nullptr,gripLeft=nullptr,grip=nullptr;};
struct WidgetWindow {std::wstring id;HWND window=nullptr,edit=nullptr;bool dragging=false,pendingSave=false;POINT mouse{};RECT start{};};
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
bool g_visualDirty=true;
void CancelIconGesture();
void RebuildVisualIcons();
void RefreshVisualGeometry();
void DrawVisualIcons(Gdiplus::Graphics&);
LRESULT CALLBACK KeyboardProc(int code,WPARAM wParam,LPARAM lParam);
LRESULT CALLBACK WidgetProc(HWND,UINT,WPARAM,LPARAM);
void UpdateSurfaceVisibility();
constexpr int kHeader=32;
constexpr UINT_PTR kWeatherTimer=4;
constexpr UINT kWeatherRefreshMs=5*60*1000;
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
    return tabs;
}
bool HasGroupTabs(const Box& root) {
    return g_layout&&std::any_of(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const Box& box){return box.groupId==root.id;});
}
void DetachTab(Box& tab) {
    Box* root=GroupRoot(tab);
    auto members=GroupTabs(*root);
    if(root==&tab&&members.size()>1) {
        Box* successor=members[1];
        const auto active=root->activeTabId;
        successor->groupId.clear();successor->groupTitle=root->groupTitle.empty()?root->title:root->groupTitle;
        successor->collapsed=root->collapsed;
        successor->activeTabId=(active.empty()||active==tab.id)?successor->id:active;
        for(size_t i=2;i<members.size();++i)members[i]->groupId=successor->id;
    } else if(root!=&tab&&root->activeTabId==tab.id)root->activeTabId.clear();
    tab.groupId.clear();tab.activeTabId.clear();tab.collapsed=false;
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
    if(diameter<=0){Gdiplus::SolidBrush brush(color);g.FillRectangle(&brush,r.left,r.top,r.right-r.left,r.bottom-r.top);return;}
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
struct WeatherReply {std::wstring summary,icon;int temperature=0;};
bool ReadResourceBytes(int id,const BYTE*& data,DWORD& size) {
    HINSTANCE module=GetModuleHandleW(nullptr);HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
    if(!resource)return false;size=SizeofResource(module,resource);data=static_cast<const BYTE*>(LockResource(LoadResource(module,resource)));return data&&size;
}
void EnsureWeatherIconFont() {
    if(g_weatherIconFamily)return;
    const BYTE* fontData=nullptr;DWORD fontSize=0;if(!ReadResourceBytes(IDR_QWEATHER_FONT,fontData,fontSize))return;
    if(!g_weatherIconFonts)g_weatherIconFonts=std::make_unique<Gdiplus::PrivateFontCollection>();
    if(g_weatherIconFonts->AddMemoryFont(fontData,fontSize)!=Gdiplus::Ok)return;
    const INT count=g_weatherIconFonts->GetFamilyCount();if(count<1)return;
    Gdiplus::FontFamily family;INT found=0;if(g_weatherIconFonts->GetFamilies(1,&family,&found)!=Gdiplus::Ok||found<1)return;
    WCHAR name[LF_FACESIZE]{};if(family.GetFamilyName(name)!=Gdiplus::Ok)return;
    auto loaded=std::make_unique<Gdiplus::FontFamily>(name,g_weatherIconFonts.get());if(loaded->GetLastStatus()==Gdiplus::Ok)g_weatherIconFamily=std::move(loaded);
    const BYTE* cssData=nullptr;DWORD cssSize=0;if(ReadResourceBytes(IDR_QWEATHER_CSS,cssData,cssSize))g_weatherIconCss.assign(reinterpret_cast<const char*>(cssData),cssSize);
}
wchar_t WeatherGlyph(const std::wstring& code) {
    EnsureWeatherIconFont();if(g_weatherIconCss.empty())return 0;
    std::string id;id.reserve(code.size());for(const wchar_t value:code){if(value>0x7f)return 0;id.push_back(static_cast<char>(value));}const size_t rule=g_weatherIconCss.find(".qi-"+id+"::before");if(rule==std::string::npos)return 0;
    const size_t marker=g_weatherIconCss.find("\\f",rule);if(marker==std::string::npos)return 0;
    size_t end=marker+1;while(end<g_weatherIconCss.size()&&std::isxdigit(static_cast<unsigned char>(g_weatherIconCss[end])))++end;
    return static_cast<wchar_t>(std::strtoul(g_weatherIconCss.substr(marker+1,end-marker-1).c_str(),nullptr,16));
}
int JsonNumber(const std::string& text,const char* key) {
    const auto at=text.find(key);if(at==std::string::npos)return 0;
    return static_cast<int>(std::lround(std::strtod(text.c_str()+at+strlen(key),nullptr)));
}
std::wstring JsonText(const std::string& text,const char* key) {
    const auto at=text.find(key);if(at==std::string::npos)return {};
    const size_t begin=at+strlen(key),end=text.find('"',begin);if(end==std::string::npos)return {};
    const std::string value=text.substr(begin,end-begin);if(value.empty())return {};
    const int count=MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    std::wstring result(max(0,count),L'\0');if(count)MultiByteToWideChar(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),count);return result;
}
std::wstring WeatherApiKey() {
    const auto path=std::filesystem::path(LayoutPath()).parent_path()/L"weather-api-key.txt";
    std::ifstream file(path,std::ios::binary);std::string key;if(!file||!std::getline(file,key))return {};
    while(!key.empty()&&(key.back()=='\r'||key.back()=='\n'||key.back()==' '||key.back()=='\t'))key.pop_back();
    return std::wstring(key.begin(),key.end());
}
#pragma warning(push)
#pragma warning(disable:4995)
bool CurrentCoordinates(double& latitude,double& longitude) {
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return false;
    ILocation* location=nullptr;ILocationReport* base=nullptr;ILatLongReport* report=nullptr;bool found=false;
    if(SUCCEEDED(CoCreateInstance(CLSID_Location,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&location)))&&
       SUCCEEDED(location->GetReport(IID_ILatLongReport,&base))&&
       SUCCEEDED(base->QueryInterface(IID_PPV_ARGS(&report)))&&
       SUCCEEDED(report->GetLatitude(&latitude))&&SUCCEEDED(report->GetLongitude(&longitude)))found=true;
    if(report)report->Release();if(base)base->Release();if(location)location->Release();if(SUCCEEDED(initialized))CoUninitialize();return found;
}
#pragma warning(pop)
bool HasActiveWeatherWidget() {return g_visible&&g_layout&&std::any_of(g_layout->widgets.begin(),g_layout->widgets.end(),[](const auto& widget){return widget.type==L"weather";});}
void FetchWeather() {
    if(g_weatherRequestInFlight.exchange(true))return;
    std::thread([] {
        auto reply=std::make_unique<WeatherReply>();reply->summary=L"天气暂不可用";reply->icon=L"999";
        const std::wstring key=WeatherApiKey();
        double latitude=0,longitude=0;
        if(key.empty())reply->summary=L"未配置和风天气密钥";
        else if(!CurrentCoordinates(latitude,longitude))reply->summary=L"请开启 Windows 定位服务";
        else {
        HINTERNET session=WinHttpOpen(L"nestlone-D/0.2",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0);
        if(session) {
            WinHttpSetTimeouts(session,3000,3000,6000,6000);
            HINTERNET connection=WinHttpConnect(session,L"devapi.qweather.com",INTERNET_DEFAULT_HTTPS_PORT,0);
            if(connection) {
                std::wostringstream coordinate;coordinate<<std::fixed<<std::setprecision(2)<<longitude<<L","<<latitude;
                const std::wstring path=L"/v7/weather/now?location="+coordinate.str()+L"&key="+key+L"&lang=zh";
                HINTERNET request=WinHttpOpenRequest(connection,L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE);
                if(request&&WinHttpSendRequest(request,WINHTTP_NO_ADDITIONAL_HEADERS,0,WINHTTP_NO_REQUEST_DATA,0,0,0)&&WinHttpReceiveResponse(request,nullptr)) {
                    std::string body;DWORD available=0;
                    while(WinHttpQueryDataAvailable(request,&available)&&available) {std::string chunk(available,'\0');DWORD read=0;if(!WinHttpReadData(request,chunk.data(),available,&read))break;chunk.resize(read);body+=chunk;}
                    if(body.find("\"code\":\"200\"")!=std::string::npos) {reply->temperature=JsonNumber(body,"\"temp\":\"");reply->summary=JsonText(body,"\"text\":\"");reply->icon=JsonText(body,"\"icon\":\"");if(reply->summary.empty())reply->summary=L"天气暂不可用";if(reply->icon.empty())reply->icon=L"999";}
                }
                if(request)WinHttpCloseHandle(request);
                WinHttpCloseHandle(connection);
            }
            WinHttpCloseHandle(session);
        }
        }
        g_weatherRequestInFlight=false;HWND manager=g_manager;WeatherReply* raw=reply.release();
        if(!manager||!PostMessageW(manager,WM_APP+20,0,reinterpret_cast<LPARAM>(raw)))delete raw;
    }).detach();
}
void UpdateWeatherSchedule() {
    if(!g_manager)return;
    if(!HasActiveWeatherWidget()){KillTimer(g_manager,kWeatherTimer);return;}
    SetTimer(g_manager,kWeatherTimer,kWeatherRefreshMs,nullptr);
}
void RefreshWeatherNow() {if(HasActiveWeatherWidget())FetchWeather();}
// The application owns one top-level surface and all its decorations are
// children. Keep the surface immediately above the desktop, below normal apps.
// Never infer desktop visibility from a foreground process ID (Explorer also
// owns ordinary folder windows), or hide the entire desktop on focus changes.
bool SurfacePresentationAllowed() {return g_visible;}
void UpdateSurfaceVisibility() {
    if(!IsWindow(g_background))return;
    if(!g_visible){ShowWindow(g_background,SW_HIDE);return;}
    HWND desktop=GetAncestor(g_host.defview,GA_ROOT);
    if(!IsWindow(desktop))return;
    HWND preceding=GetWindow(desktop,GW_HWNDPREV);
    while(preceding==g_background)preceding=GetWindow(preceding,GW_HWNDPREV);
    // Inserting after a topmost window can itself promote our window.  Keep
    // the canvas in the normal window band even when the desktop is foremost.
    if(preceding&&(GetWindowLongPtrW(preceding,GWL_EXSTYLE)&WS_EX_TOPMOST))preceding=nullptr;
    SetWindowPos(g_background,preceding?preceding:HWND_TOP,0,0,0,0,
        SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW);
}
void CALLBACK ForegroundProc(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD) {
    if(g_manager)PostMessageW(g_manager,WM_APP+21,0,0);
}
void SyncWidgets() {
    if(!g_layout||!g_host.defviewParent)return;
    for(auto it=g_widgets.begin();it!=g_widgets.end();) {
        if(!FindWidget((*it)->id)){if(IsWindow((*it)->window))DestroyWindow((*it)->window);g_paintBuffers.erase((*it)->window);it=g_widgets.erase(it);}else ++it;
    }
    for(auto& model:g_layout->widgets) {
        auto found=std::find_if(g_widgets.begin(),g_widgets.end(),[&](const auto& item){return item->id==model.id;});
        if(found==g_widgets.end()) {
            auto widget=std::make_unique<WidgetWindow>();widget->id=model.id;POINT p=ParentPoint({model.rect.left,model.rect.top});
            const DWORD extended=model.type==L"weather"?(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW):WS_EX_TOOLWINDOW;
            widget->window=CreateWindowExW(extended,L"nestlone-D.Widget",L"",SurfaceWindowStyle()|WS_CLIPCHILDREN,p.x,p.y,model.rect.right-model.rect.left,model.rect.bottom-model.rect.top,SurfaceParent(),nullptr,g_instance,widget.get());
            if(!widget->window)continue;
            if(model.type==L"note")widget->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",model.text.c_str(),WS_CHILD|WS_VISIBLE|ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL,10,34,100,100,widget->window,reinterpret_cast<HMENU>(1),g_instance,nullptr);
            if(widget->edit)SendMessageW(widget->edit,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE);
            if(widget->edit)MoveWindow(widget->edit,8,34,max(1L,model.rect.right-model.rect.left-16),max(1L,model.rect.bottom-model.rect.top-42),TRUE);
            SetWindowPos(widget->window,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            ShowWindow(widget->window,SurfacePresentationAllowed()?SW_SHOWNOACTIVATE:SW_HIDE);
            g_widgets.push_back(std::move(widget));
        } else {POINT p=ParentPoint({model.rect.left,model.rect.top});SetWindowPos((*found)->window,nullptr,p.x,p.y,model.rect.right-model.rect.left,model.rect.bottom-model.rect.top,SWP_NOACTIVATE|SWP_NOZORDER);ShowWindow((*found)->window,SurfacePresentationAllowed()?SW_SHOWNOACTIVATE:SW_HIDE);}
    }
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
void QueueInteractivePaint() {
    if(!g_manager||g_interactivePaintQueued)return;
    g_interactivePaintQueued=true;SetTimer(g_manager,kInteractivePaintTimer,16,nullptr);
}
bool SnapshotVisualsChanged(const DesktopSnapshot& before,const DesktopSnapshot& after) {
    if(before.entries.size()!=after.entries.size() || before.iconSize!=after.iconSize ||
       before.spacing.x!=after.spacing.x || before.spacing.y!=after.spacing.y ||
       before.iconMode!=after.iconMode)return true;
    for(const auto& entry:after.entries)if(!FindEntry(before,entry.path))return true;
    return false;
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
    if(!SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_ICON|SHGFI_LARGEICON)||!info.hIcon)return {};
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
bool RenderIconInCurrentPass(const VisualIcon& icon) {
    if(g_previewGroup.empty()&&g_renderOnlyGroup.empty())return true;
    std::wstring owner;
    for(const auto& box:g_layout->boxes)if(HasPath(box,icon.path)){owner=GroupRoot(box)->id;break;}
    return g_renderOnlyGroup.empty()?owner!=g_previewGroup:owner==g_renderOnlyGroup;
}
void DrawVisualIcons(Gdiplus::Graphics& g) {
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::FontFamily family(CanvasFontFamily());
    for(auto& item:g_visualIcons) {
        if(!RenderIconInCurrentPass(item))continue;
        const auto state=g.Save();
        if(item.list)g.SetClip(Gdiplus::Rect(item.clip.left,item.clip.top,item.clip.right-item.clip.left,item.clip.bottom-item.clip.top));
        if(item.selected){Gdiplus::SolidBrush selected(Gdiplus::Color(72,55,133,190));Gdiplus::RectF r(static_cast<float>(item.hit.left),static_cast<float>(item.hit.top),static_cast<float>(item.hit.right-item.hit.left),static_cast<float>(item.hit.bottom-item.hit.top));g.FillRectangle(&selected,r);}
        if(item.image)g.DrawImage(item.image.get(),item.position.x,item.position.y,item.size,item.size);
        Gdiplus::StringFormat format;format.SetAlignment(item.list?Gdiplus::StringAlignmentNear:Gdiplus::StringAlignmentCenter);format.SetLineAlignment(item.list?Gdiplus::StringAlignmentCenter:Gdiplus::StringAlignmentNear);format.SetTrimming(Gdiplus::StringTrimmingEllipsisWord);if(item.list)format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);else format.SetFormatFlags(Gdiplus::StringFormatFlagsLineLimit);
        Gdiplus::SolidBrush text(Gdiplus::Color(255,255,255,255)),shadow(Gdiplus::Color(190,0,0,0));Gdiplus::Font font(&family,item.list?Gdiplus::REAL(12):Gdiplus::REAL(12),Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::RectF label(static_cast<float>(item.label.left),static_cast<float>(item.label.top),static_cast<float>(max(1L,item.label.right-item.label.left)),static_cast<float>(max(1L,item.label.bottom-item.label.top))),offset=label;offset.X+=1.0f;offset.Y+=1.0f;g.DrawString(item.name.c_str(),-1,&font,offset,&format,&shadow);g.DrawString(item.name.c_str(),-1,&font,label,&format,&text);g.Restore(state);
    }
    // A selected icon reveals its complete file name without permanently
    // widening the grid. This mirrors the desktop's focus-only title affordance.
    if(g_focusIcon>=0&&g_focusIcon<static_cast<int>(g_visualIcons.size())) {
        const auto& item=g_visualIcons[g_focusIcon];
        if(item.selected&&!item.list&&RenderIconInCurrentPass(item)) {
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
void EndDragPreview() {
    HWND preview=g_dragPreview;g_dragPreview=nullptr;g_previewGroup.clear();g_renderOnlyGroup.clear();
    if(preview)DestroyWindow(preview);
}
bool BeginDragPreview(Box& box) {
    if(g_dragPreview)return true;
    if(!g_nativeHidden||g_resizing)return false;
    // Multiple separately selected groups continue to use the normal path.
    for(const auto& start:g_boxDragStart)if(auto* member=FindBox(start.first))if(GroupRoot(*member)!=&box)return false;
    PaintAll();
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
    WNDCLASSW wc{};wc.hInstance=g_instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"nestlone-D.DragPreview";RegisterClassW(&wc);
    POINT top=ParentPoint({bounds.left,bounds.top});
    HWND preview=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"",SurfaceWindowStyle(),top.x,top.y,width,height,SurfaceParent(),nullptr,g_instance,nullptr);
    if(!preview)return false;
    POINT source{};SIZE size{width,height};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    if(!UpdateLayeredWindow(preview,nullptr,nullptr,&size,buffer.dc,&source,0,&blend,ULW_ALPHA)){DestroyWindow(preview);return false;}
    g_dragPreview=preview;g_previewGroup=box.id;
    // Remove only the moving group's pixels from the static desktop once.
    PaintWindow(g_background);
    SetWindowPos(preview,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    return true;
}
void MoveDragPreview(Box& box) {
    POINT p=ParentPoint({box.rect.left,box.rect.top});
    SetWindowPos(g_dragPreview,SurfaceControlInsertAfter(),p.x,p.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    for(auto& d:g_decorations)if(d->id==box.id){
        SetWindowPos(d->header,SurfaceControlInsertAfter(),p.x,p.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
        p=ParentPoint({box.rect.right-18,box.rect.bottom-18});
        SetWindowPos(d->grip,SurfaceControlInsertAfter(),p.x,p.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
        p=ParentPoint({box.rect.left,box.rect.bottom-18});
        SetWindowPos(d->gripLeft,SurfaceControlInsertAfter(),p.x,p.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    }
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
void FinishRename(bool save) {
    HWND edit=g_edit;if(!edit)return;g_edit=nullptr;
    wchar_t value[81]{};GetWindowTextW(edit,value,81);
    std::wstring text=value;auto first=text.find_first_not_of(L" \t\r\n");
    if(save&&first!=std::wstring::npos)if(auto* box=FindBox(g_editId)) {
        const std::wstring trimmed=text.substr(first,text.find_last_not_of(L" \t\r\n")-first+1);
        if(g_editGroupTitle)box->groupTitle=trimmed;else box->title=trimmed;
        SaveLayout(*g_layout);
    }
    DestroyWindow(edit);g_editId.clear();g_editGroupTitle=false;PaintAll();
}
LRESULT CALLBACK EditProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR) {
    if(msg==WM_KEYDOWN&&(wp==VK_RETURN||wp==VK_ESCAPE)){FinishRename(wp==VK_RETURN);return 0;}
    if(msg==WM_KILLFOCUS){PostMessageW(g_manager,WM_APP+10,0,0);return 0;}
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(hwnd,EditProc,1);
    return DefSubclassProc(hwnd,msg,wp,lp);
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
    SetWindowSubclass(g_edit,EditProc,1,0);ShowWindow(g_edit,SW_SHOW);SetForegroundWindow(g_edit);SetFocus(g_edit);SendMessageW(g_edit,EM_SETSEL,0,-1);
}
void DestroyDecorations() {
    EndDragPreview();
    FinishRename(false);
    for(auto& widget:g_widgets){if(IsWindow(widget->window))DestroyWindow(widget->window);g_paintBuffers.erase(widget->window);}
    g_widgets.clear();
    for(auto& d:g_decorations){if(IsWindow(d->header))DestroyWindow(d->header);if(IsWindow(d->gripLeft))DestroyWindow(d->gripLeft);if(IsWindow(d->grip))DestroyWindow(d->grip);}
    g_decorations.clear();if(IsWindow(g_background))DestroyWindow(g_background);g_background=nullptr;
    ReleaseVisualIcons();if(IsWindow(g_hiddenListview))ReleaseHiddenDesktopListView(g_hiddenListview);g_hiddenListview=nullptr;g_nativeHidden=false;
}
LRESULT CALLBACK DecorationProc(HWND,UINT,WPARAM,LPARAM);
void DrawWidgetLockGlyph(HDC dc,const RECT& bounds,bool locked,COLORREF background) {
    const int x=bounds.left+(bounds.right-bounds.left-22)/2,y=bounds.top+3;
    HPEN pen=CreatePen(PS_SOLID,2,RGB(25,49,60));HGDIOBJ oldPen=SelectObject(dc,pen);HGDIOBJ oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    RoundRect(dc,x+4,y,x+18,y+16,8,8);
    HBRUSH clear=CreateSolidBrush(background);RECT lower{x+3,y+11,x+19,y+17};FillRect(dc,&lower,clear);DeleteObject(clear);
    if(!locked){HBRUSH gap=CreateSolidBrush(background);RECT open{x+13,y-1,x+21,y+11};FillRect(dc,&open,gap);DeleteObject(gap);}
    HBRUSH body=CreateSolidBrush(RGB(25,49,60));SelectObject(dc,body);RoundRect(dc,x+1,y+12,x+21,y+27,3,3);SelectObject(dc,oldBrush);DeleteObject(body);SelectObject(dc,oldPen);DeleteObject(pen);
}
void DrawWeatherIcon(HDC dc,const RECT& bounds,const std::wstring& code) {
    const wchar_t glyph=WeatherGlyph(code);
    if(glyph&&g_weatherIconFamily){Gdiplus::Graphics graphics(dc);graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);Gdiplus::Font font(g_weatherIconFamily.get(),48,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::SolidBrush ink(Gdiplus::Color(255,28,105,137));Gdiplus::RectF target(static_cast<Gdiplus::REAL>(bounds.left),static_cast<Gdiplus::REAL>(bounds.top),static_cast<Gdiplus::REAL>(bounds.right-bounds.left),static_cast<Gdiplus::REAL>(bounds.bottom-bounds.top));graphics.DrawString(&glyph,1,&font,target,nullptr,&ink);return;}
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(28,105,137));DrawTextW(dc,L"☁",-1,const_cast<RECT*>(&bounds),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
bool PaintWeatherWidget(HWND hwnd,const Layout::Widget& widget) {
    RECT r{};if(!GetClientRect(hwnd,&r)||r.right<=0||r.bottom<=0)return false;
    auto& stored=g_paintBuffers[hwnd];if(!stored)stored=std::make_unique<PaintBuffer>();auto& buffer=*stored;if(!buffer.Ensure(r.right,r.bottom))return false;
    Gdiplus::Bitmap surface(r.right,r.bottom,r.right*4,PixelFormat32bppPARGB,static_cast<BYTE*>(buffer.pixels));Gdiplus::Graphics graphics(&surface);graphics.Clear(Gdiplus::Color(0,0,0,0));graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);Rounded(graphics,r,Gdiplus::Color(255,215,241,248),16);Rounded(graphics,{0,0,r.right,31},Gdiplus::Color(255,157,216,231),16);
    Gdiplus::FontFamily family(CanvasFontFamily());Gdiplus::Font title(&family,13,Gdiplus::FontStyleBold,Gdiplus::UnitPixel),body(&family,14,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::SolidBrush ink(Gdiplus::Color(255,25,49,60));Gdiplus::StringFormat centered;centered.SetAlignment(Gdiplus::StringAlignmentCenter);centered.SetLineAlignment(Gdiplus::StringAlignmentCenter);
    const std::wstring heading=L"天气 · 当前定位";graphics.DrawString(heading.c_str(),-1,&title,Gdiplus::RectF(8,0,static_cast<Gdiplus::REAL>(r.right-44),31),&centered,&ink);const std::wstring value=std::to_wstring(widget.temperature)+L"°C  "+widget.weather;graphics.DrawString(value.c_str(),-1,&body,Gdiplus::RectF(72,47,static_cast<Gdiplus::REAL>(max(1L,r.right-84)),40),&centered,&ink);graphics.Flush();
    // Keep all pixels in the same PARGB graphics surface. GDI writes into this
    // DIB can zero alpha and make lock/icon pixels invisible to hit testing.
    const wchar_t glyph=WeatherGlyph(widget.weatherIcon);
    if(glyph&&g_weatherIconFamily){Gdiplus::Font iconFont(g_weatherIconFamily.get(),48,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);graphics.DrawString(&glyph,1,&iconFont,Gdiplus::RectF(12,42,56,58),&centered,&ink);}
    const float x=static_cast<float>(r.right-26);
    Gdiplus::Pen lockPen(Gdiplus::Color(255,25,49,60),2);
    graphics.DrawArc(&lockPen,x+3,5.0f,10.0f,12.0f,180.0f,widget.locked?180.0f:130.0f);
    graphics.DrawLine(&lockPen,x+3,11.0f,x+3,15.0f);
    if(widget.locked)graphics.DrawLine(&lockPen,x+13,11.0f,x+13,15.0f);
    Rounded(graphics,{r.right-26,14,r.right-10,26},Gdiplus::Color(255,25,49,60),4);
    graphics.Flush(Gdiplus::FlushIntentionSync);
    POINT source{};SIZE size{r.right,r.bottom};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};return UpdateLayeredWindow(hwnd,nullptr,nullptr,&size,buffer.dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
}
LRESULT CALLBACK WidgetProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    auto* state=reinterpret_cast<WidgetWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(message==WM_NCCREATE){auto* create=reinterpret_cast<CREATESTRUCTW*>(lp);state=static_cast<WidgetWindow*>(create->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));}
    Layout::Widget* widget=state?FindWidget(state->id):nullptr;
    if(!widget)return DefWindowProcW(hwnd,message,wp,lp);
    switch(message) {
    case WM_NCHITTEST:return HTCLIENT;
    case WM_MOUSEACTIVATE:return widget->type==L"weather"?MA_NOACTIVATE:MA_ACTIVATE;
    case WM_ERASEBKGND:return 1;
    case WM_PAINT:{PAINTSTRUCT ps{};HDC dc=BeginPaint(hwnd,&ps);if(widget->type==L"weather"){EndPaint(hwnd,&ps);PaintWeatherWidget(hwnd,*widget);}else {RECT r{};GetClientRect(hwnd,&r);const COLORREF headerColor=RGB(246,220,122);HBRUSH body=CreateSolidBrush(RGB(255,247,190));FillRect(dc,&r,body);DeleteObject(body);HBRUSH header=CreateSolidBrush(headerColor);RECT top=r;top.bottom=30;FillRect(dc,&top,header);DeleteObject(header);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(25,49,60));HFONT old=static_cast<HFONT>(SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT)));RECT title=top;title.right-=36;DrawTextW(dc,L"便签",-1,&title,DT_SINGLELINE|DT_VCENTER|DT_CENTER);RECT lock=top;lock.left=lock.right-34;DrawWidgetLockGlyph(dc,lock,widget->locked,headerColor);SelectObject(dc,old);EndPaint(hwnd,&ps);}return 0;}
    case WM_SIZE:{const int width=max(1,LOWORD(lp)),height=max(1,HIWORD(lp));if(widget->type==L"weather")PaintWeatherWidget(hwnd,*widget);else {HRGN region=CreateRoundRectRgn(0,0,width+1,height+1,16,16);SetWindowRgn(hwnd,region,TRUE);if(state->edit)MoveWindow(state->edit,8,34,max(1,width-16),max(1,height-42),TRUE);}return 0;}
    case WM_COMMAND:if(state->edit&&reinterpret_cast<HWND>(lp)==state->edit){if(HIWORD(wp)==EN_CHANGE){wchar_t text[4096]{};GetWindowTextW(state->edit,text,4096);widget->text=text;state->pendingSave=true;SetTimer(hwnd,1,450,nullptr);}else if(HIWORD(wp)==EN_KILLFOCUS&&state->pendingSave){KillTimer(hwnd,1);state->pendingSave=false;SaveLayout(*g_layout);}}return 0;
    case WM_LBUTTONDOWN:if(GET_Y_LPARAM(lp)<30&&GET_X_LPARAM(lp)>=widget->rect.right-widget->rect.left-36){widget->locked=!widget->locked;SaveLayout(*g_layout);InvalidateRect(hwnd,nullptr,TRUE);return 0;}else if(GET_Y_LPARAM(lp)<30&&!widget->locked){SetWindowPos(hwnd,SurfaceControlInsertAfter(),0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);state->dragging=true;state->start=widget->rect;GetCursorPos(&state->mouse);SetCapture(hwnd);}return 0;
    case WM_MOUSEMOVE:if(state->dragging&&GetCapture()==hwnd){POINT p{};GetCursorPos(&p);const LONG dx=p.x-state->mouse.x,dy=p.y-state->mouse.y;widget->rect=state->start;OffsetRect(&widget->rect,dx,dy);widget->rect=KeepBoxOnScreen(widget->rect,30);POINT top=ParentPoint({widget->rect.left,widget->rect.top});SetWindowPos(hwnd,nullptr,top.x,top.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOZORDER);return 0;}break;
    case WM_LBUTTONUP:if(state->dragging){state->dragging=false;ReleaseCapture();SaveLayout(*g_layout);}return 0;
    case WM_CAPTURECHANGED:if(state->dragging){state->dragging=false;SaveLayout(*g_layout);}return 0;
    case WM_CANCELMODE:if(GetCapture()==hwnd)ReleaseCapture();return 0;
    case WM_DESTROY:KillTimer(hwnd,1);if(state->pendingSave&&g_layout)SaveLayout(*g_layout);g_paintBuffers.erase(hwnd);return 0;
    case WM_TIMER:if(wp==1&&state->pendingSave){KillTimer(hwnd,1);state->pendingSave=false;SaveLayout(*g_layout);}return 0;
    case WM_LBUTTONDBLCLK:if(widget->type==L"weather")RefreshWeatherNow();return 0;
    case WM_RBUTTONUP:{POINT point{};GetCursorPos(&point);HMENU menu=CreatePopupMenu();if(widget->type==L"weather")AppendMenuW(menu,MF_STRING,1,L"刷新天气");AppendMenuW(menu,MF_STRING|(widget->locked?MF_GRAYED:0),2,L"删除组件");const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd,nullptr);DestroyMenu(menu);if(command==1)RefreshWeatherNow();if(command==2&&!widget->locked){auto id=widget->id;g_layout->widgets.erase(std::remove_if(g_layout->widgets.begin(),g_layout->widgets.end(),[&](const auto& item){return item.id==id;}),g_layout->widgets.end());SaveLayout(*g_layout);UpdateWeatherSchedule();PostMessageW(g_manager,WM_APP+11,0,0);}return 0;}
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
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
bool DesktopPointerActive() {
    if(!g_background||!IsWindowVisible(g_background))return false;
    POINT cursor{};GetCursorPos(&cursor);
    HWND underCursor=WindowFromPoint(cursor);
    return underCursor==g_background||IsChild(g_background,underCursor);
}
void RequestDeleteSelection() {
    if(!g_pendingDelete.empty())return;
    for(const auto& item:g_visualIcons)if(item.selected&&GetFileAttributesW(item.path.c_str())!=INVALID_FILE_ATTRIBUTES)g_pendingDelete.push_back(item.path);
    if(!g_pendingDelete.empty())PostMessageW(g_manager,WM_APP+13,0,0);
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
        const bool deleteKey=key->vkCode==VK_DELETE;
        const bool ctrlD=key->vkCode=='D'&&(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;
        if(deleteKey||ctrlD) {
            RequestDeleteSelection();
            if(!g_pendingDelete.empty())return 1;
        }
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
    SaveLayout(*g_layout);g_visualDirty=true;g_iconDrag=-1;g_iconDragStart.clear();PaintAll();
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
    UpdateWeatherSchedule();
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
    g_visualDirty=true;PaintAll();SyncWidgets();UpdateWeatherSchedule();SaveLayout(*g_layout);
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
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,1,L"重命名盒子");AppendMenuW(menu,MF_STRING,2,L"整理盒内图标位置");
    AppendMenuW(menu,MF_STRING|(active->iconView?MF_CHECKED:0),6,L"图标显示");AppendMenuW(menu,MF_STRING|(!active->iconView?MF_CHECKED:0),7,L"列表显示");
    AppendMenuW(menu,MF_STRING,3,L"收纳区域内图标");AppendMenuW(menu,MF_STRING,4,box.collapsed?L"展开盒子":L"折叠盒子（隐藏图标）");
    AppendMenuW(menu,MF_STRING,5,L"删除盒子（保留图标与文件）");
    if(!g_status.empty())AppendMenuW(menu,MF_STRING|MF_DISABLED,0,g_status.c_str());
    int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,hwnd,nullptr);DestroyMenu(menu);
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
            g_focusIcon=hit;g_iconDrag=hit;g_iconMoved=false;g_iconDragStart.clear();for(const auto& item:g_visualIcons)if(item.selected)g_iconDragStart.push_back({item.path,item.position});GetCursorPos(&g_iconMouseStart);SetCapture(hwnd);PaintWindow(hwnd);return 0;
        }
        if(hwnd==g_background && message==WM_MOUSEMOVE && GetCapture()==hwnd) {
            POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if(g_marquee){UpdateMarquee(point);QueueInteractivePaint();return 0;}
            if(g_iconDrag>=0){POINT cursor{};GetCursorPos(&cursor);LONG dx=cursor.x-g_iconMouseStart.x,dy=cursor.y-g_iconMouseStart.y;if(!g_iconMoved&&abs(dx)<4&&abs(dy)<4)return 0;g_iconMoved=true;for(auto& item:g_visualIcons)if(item.selected){auto start=std::find_if(g_iconDragStart.begin(),g_iconDragStart.end(),[&](const auto& value){return _wcsicmp(value.first.c_str(),item.path.c_str())==0;});if(start!=g_iconDragStart.end()){item.position=ClampDesktopPosition({start->second.x+dx,start->second.y+dy});RefreshIconHit(item);}}QueueInteractivePaint();return 0;}
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
                if(!boxHit) {
                    bool createBox=false;
                    if(ShowShellBackgroundMenu(hwnd,screen,&createBox)) {
                        if(createBox)CreateBoxAt(virtualPoint);
                        else PostMessageW(g_manager,WM_APP+12,0,0);
                    }
                }
            }
            return 0;
        }
        if(hwnd==g_background && message==WM_MOUSEWHEEL) {
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
                g_tabPending=true;g_dragId=tabs[index]->id;g_boxStart=tabs[index]->rect;GetCursorPos(&g_mouseStart);SetCapture(hwnd);return 0;
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
            if(abs(dx)<4&&abs(dy)<4)return 0;
            if(auto* tab=FindBox(g_dragId)) {
                DetachTab(*tab);
                tab->rect=g_boxStart;tab->selected=true;g_boxDragStart={{tab->id,g_boxStart}};g_drag=true;g_tabPending=false;g_tabDetached=true;g_resizing=false;g_visualDirty=true;box=tab;
                Rebuild();
            }
        }
        if(g_drag && GetCapture()==hwnd) {
            if(auto* dragged=FindBox(g_dragId))box=dragged;
            if(!g_resizing&&!g_dragPreview)BeginDragPreview(*box);
            POINT p{};GetCursorPos(&p);LONG dx=p.x-g_mouseStart.x,dy=p.y-g_mouseStart.y;box->rect=g_boxStart;
            if(g_resizing){
                if(g_resizeFromLeft)box->rect.left=min(box->rect.right-240,box->rect.left+dx);
                else box->rect.right=max(box->rect.left+240,box->rect.right+dx);
                box->rect.bottom=max(box->rect.top+BoxHeaderHeight(*box)+100,box->rect.bottom+dy);box->rect=FitGrid(*box,g_snapshot);
                if(HasGroupTabs(*box))for(auto& candidate:g_layout->boxes)if(GroupRoot(candidate)==box)candidate.rect=box->rect;g_visualGeometryDirty=true;
            }
            else {
                RECT desired=g_boxStart;OffsetRect(&desired,dx,dy);
                const RECT constrained=KeepBoxOnScreen(desired,BoxHeaderHeight(*box));
                dx=constrained.left-g_boxStart.left;dy=constrained.top-g_boxStart.top;
                for(auto& start:g_boxDragStart)if(auto* selected=FindBox(start.first)){selected->rect=start.second;OffsetRect(&selected->rect,dx,dy);}
                RecoverBoxPositions();
                if(g_nativeHidden){g_visualGeometryDirty=true;}else {std::vector<DesktopMove> moves=g_dragStart;for(auto& m:moves){m.position.x+=dx;m.position.y+=dy;}if(!moves.empty())QueueMoves(moves);}
            }
            if(g_dragPreview)MoveDragPreview(*box);else QueueInteractivePaint();
        }
        return 0;
    case WM_CANCELMODE:
        EndDragPreview();
        g_boxButton=0;g_drag=false;g_tabPending=false;g_tabDetached=false;g_resizing=false;g_resizeFromLeft=false;g_boxDragStart.clear();CancelIconGesture();return 0;
    case WM_LBUTTONUP:
        if(g_tabPending){g_tabPending=false;ReleaseCapture();if(auto* tab=FindBox(g_dragId)){if(auto* root=GroupRoot(*tab)){root->activeTabId=tab->id;g_visualDirty=true;SaveLayout(*g_layout);PaintAll();}}return 0;}
        if(g_boxButton){int button=g_boxButton;g_boxButton=0;ReleaseCapture();if(button==1)ToggleBoxView(*ActiveBox(*box));else if(button==2){box->collapsed=!box->collapsed;g_visualDirty=true;SaveLayout(*g_layout);PaintAll();}else {auto id=box->id;g_layout->boxes.erase(std::remove_if(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const auto& b){return b.id==id;}),g_layout->boxes.end());PostMessageW(g_manager,WM_APP+11,0,0);}return 0;}
        if(g_drag){
            if(auto* dragged=FindBox(g_dragId))box=dragged;
            g_drag=false;ReleaseCapture();EndDragPreview();FlushInteractivePaint();
            if(g_resizing)Arrange(*box);
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
                    for(auto* candidate:members) {candidate->groupId=destination->id;candidate->rect=destination->rect;candidate->color=destination->color;}
                    destination->activeTabId=box->id;g_visualDirty=true;g_boxDragStart.clear();SaveLayout(*g_layout);PostMessageW(g_manager,WM_APP+11,0,0);return 0;
                }
            }
            const bool rebuildDecorations=g_tabDetached;g_tabDetached=false;g_resizeFromLeft=false;
            g_boxDragStart.clear();SaveLayout(*g_layout);
            if(rebuildDecorations)PostMessageW(g_manager,WM_APP+11,0,0);
        }return 0;
    case WM_CAPTURECHANGED:if(g_drag){g_drag=false;EndDragPreview();if(g_resizing)Arrange(*box);g_resizing=false;g_resizeFromLeft=false;g_boxDragStart.clear();SaveLayout(*g_layout);FlushInteractivePaint();}return 0;
    }
    return DefWindowProcW(hwnd,message,wp,lp);
}
LRESULT CALLBACK ManagerProc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp) {
    if(message==WM_APP+10){FinishRename(true);return 0;}
    if(message==WM_APP+11){Rebuild();return 0;}
    if(message==WM_APP+13){DeleteSelection();return 0;}
    if(message==WM_APP+20){
        std::unique_ptr<WeatherReply> reply(reinterpret_cast<WeatherReply*>(lp));
        if(reply&&g_layout){for(auto& widget:g_layout->widgets)if(widget.type==L"weather"){widget.temperature=reply->temperature;widget.weather=reply->summary;widget.weatherIcon=reply->icon;}SaveLayout(*g_layout);for(auto& window:g_widgets)if(IsWindow(window->window)){if(auto* model=FindWidget(window->id);model&&model->type==L"weather")PaintWeatherWidget(window->window,*model);else InvalidateRect(window->window,nullptr,TRUE);}}
        return 0;
    }
    if(message==WM_APP+21){UpdateSurfaceVisibility();return 0;}
    if(message==WM_APP+12){
        // Explorer may commit a background-menu command (New, View size,
        // Refresh) after the menu closes. Poll briefly so the owned canvas
        // receives the same change notification it would have received from
        // Explorer's visible ListView.
        g_shellRefreshAttempts=10;SetTimer(hwnd,kShellRefreshTimer,120,nullptr);return 0;
    }
    if(message==WM_TIMER&&wp==kInteractivePaintTimer) {
        KillTimer(hwnd,kInteractivePaintTimer);g_interactivePaintQueued=false;
        if(g_visible){
            if(g_drag)PaintAll();
            else if((g_marquee||g_iconDrag>=0)&&g_background)PaintWindow(g_background);
        }
        return 0;
    }
    if(message==WM_TIMER&&wp==kShellRefreshTimer) {
        if(!g_visible||g_shellRefreshAttempts--<=0){KillTimer(hwnd,kShellRefreshTimer);return 0;}
        DesktopSnapshot next;
        if(PollDesktop(next)&&next.readable) {
            const bool changed=SnapshotVisualsChanged(g_snapshot,next);
            g_snapshot=std::move(next);
            if(changed)g_iconCache.clear();
            // Force an owned-canvas redraw even for a shell command whose
            // result has the same item count, such as a view-size change.
            g_visualDirty=true;PaintAll();
        }
        return 0;
    }
    if(message==WM_TIMER&&wp==kWeatherTimer) {RefreshWeatherNow();return 0;}
    if(message==WM_TIMER) {
        // Host discovery enumerates Explorer windows and sends synchronous
        // messages. Defer maintenance while the pointer gesture is active.
        if(g_drag||g_tabPending||g_marquee||g_iconDrag>=0||std::any_of(g_widgets.begin(),g_widgets.end(),[](const auto& w){return w->dragging;}))return 0;
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
                    bool resourcesChanged=SnapshotVisualsChanged(g_snapshot,next);
                    g_snapshot=std::move(next);
                    if(g_nativeHidden&&resourcesChanged){g_iconCache.clear();g_visualDirty=true;PaintAll();}
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
    SetTimer(g_manager,1,350,nullptr);Attach();PollDesktop(g_snapshot);RefreshWeatherNow();return true;
}
void DestroyCanvas() {
    CancelDesktopMoves();DestroyDecorations();if(g_manager)DestroyWindow(g_manager);g_manager=nullptr;
    if(g_keyboardHook){UnhookWindowsHookEx(g_keyboardHook);g_keyboardHook=nullptr;}
    if(g_foregroundHook){UnhookWinEvent(g_foregroundHook);g_foregroundHook=nullptr;}
    g_interactivePaintQueued=false;g_paintBuffers.clear();g_iconCache.clear();
    if(g_editFont){DeleteObject(g_editFont);g_editFont=nullptr;}
    g_weatherIconFamily.reset();g_weatherIconFonts.reset();g_weatherIconCss.clear();
    if(g_token){Gdiplus::GdiplusShutdown(g_token);g_token=0;}
}
bool CanvasVisible(){return g_visible;}
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
            if(!Attach()) {g_visible=false;UpdateWeatherSchedule();return;}
            DesktopSnapshot current;if(ReadDesktop(current)){g_snapshot=std::move(current);g_visualDirty=true;}
        }
        UpdateWeatherSchedule();if(g_visible)RefreshWeatherNow();
        if(g_visible)PaintAll();
        UpdateSurfaceVisibility();
    }
    if(command==CanvasCommand::NewBox)CreateBoxAt({160,160});
    if(command==CanvasCommand::NewNote||command==CanvasCommand::NewWeather) {Layout::Widget widget;widget.id=std::to_wstring(GetTickCount64());widget.type=command==CanvasCommand::NewNote?L"note":L"weather";widget.rect={220,160,480,command==CanvasCommand::NewNote?390:310};widget.text=L"双击编辑内容";widget.weather=L"正在获取天气";g_layout->widgets.push_back(std::move(widget));SaveLayout(*g_layout);SyncWidgets();UpdateWeatherSchedule();if(command==CanvasCommand::NewWeather)RefreshWeatherNow();}
    if(command==CanvasCommand::ClearWidgets) {g_layout->widgets.clear();SaveLayout(*g_layout);SyncWidgets();UpdateWeatherSchedule();}
    if(command==CanvasCommand::Reload){g_iconCache.clear();*g_layout=LoadLayout();Rebuild();}
    if(command==CanvasCommand::Exit)DestroyCanvas();
}
}

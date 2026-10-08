#include "Settings.h"
#include "DesktopCanvas.h"
#include "DesktopItems.h"
#include "Theme.h"
#include "resource.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <memory>
#include <vector>
#include <filesystem>

namespace nestlone {
namespace {
constexpr wchar_t kClassName[] = L"nestlone-D.Settings";
constexpr int kSlider = 2001;
constexpr int kValue = 2002;
constexpr int kStartup = 2003;
constexpr int kCornerSlider = 2004;
constexpr int kCornerValue = 2005;
constexpr int kDefaultWidth = 2006;
constexpr int kDefaultHeight = 2007;
constexpr int kFontFamily = 2008;
constexpr int kBoxSelect = 2100;
constexpr int kColorBase = 2110;
constexpr int kSidebarTheme = 2201;
constexpr int kSidebarAuto = 2202;
constexpr int kSidebarBackup = 2203;
constexpr int kSidebarAbout = 2204;
constexpr int kSidebarWidgets = 2205;
constexpr int kAboutRepository = 2501;
constexpr int kWidgetNote = 2601;
constexpr int kWidgetWeather = 2602;
constexpr int kWidgetClear = 2603;
constexpr int kBackupList = 2401;
constexpr int kBackupCreate = 2402;
constexpr int kBackupApply = 2403;
constexpr int kBackupDelete = 2404;
constexpr int kAutoEnable = 2301;
constexpr int kAutoFolders = 2302;
constexpr int kAutoExtensions = 2303;
constexpr int kAutoBox = 2304;
constexpr int kAutoAdd = 2305;
constexpr int kAutoRules = 2306;
constexpr int kDefaultBoxBase = 2310;
constexpr int kDefaultColorBase = 2130;
HFONT g_font = nullptr;
HBRUSH g_surface = nullptr;
HWND g_window = nullptr;
HWND g_value = nullptr;
HWND g_cornerValue = nullptr;
HWND g_defaultWidth = nullptr;
HWND g_defaultHeight = nullptr;
HWND g_fontFamily = nullptr;
HWND g_boxSelect = nullptr;
HWND g_autoBox = nullptr;
HWND g_autoExtensions = nullptr;
HWND g_autoRules = nullptr;
HWND g_backupList = nullptr;
HWND g_close = nullptr;
HWND g_sidebarAbout = nullptr;
Layout* g_layout = nullptr;
std::vector<HWND> g_themePage,g_autoPage,g_backupPage,g_aboutPage,g_widgetsPage;
std::unique_ptr<Gdiplus::Bitmap> g_githubLogo;
constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"nestlone-D";
std::wstring StartupCommand() {
    wchar_t path[MAX_PATH]{};
    const DWORD length=GetModuleFileNameW(nullptr,path,MAX_PATH);
    if(!length||length>=MAX_PATH)return {};
    return L"\""+std::wstring(path,length)+L"\"";
}
bool StartupEnabled() {
    HKEY key=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,kRunKey,0,KEY_QUERY_VALUE,&key)!=ERROR_SUCCESS)return false;
    wchar_t value[32768]{};DWORD type=0,size=sizeof(value);
    const bool enabled=RegQueryValueExW(key,kRunValue,nullptr,&type,reinterpret_cast<BYTE*>(value),&size)==ERROR_SUCCESS&&type==REG_SZ;
    RegCloseKey(key);return enabled;
}
bool SetStartupEnabled(bool enabled) {
    HKEY key=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,kRunKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    LONG result=ERROR_SUCCESS;
    if(enabled) {
        const std::wstring command=StartupCommand();
        result=command.empty()?ERROR_FILE_NOT_FOUND:RegSetValueExW(key,kRunValue,0,REG_SZ,reinterpret_cast<const BYTE*>(command.c_str()),static_cast<DWORD>((command.size()+1)*sizeof(wchar_t)));
    } else {
        result=RegDeleteValueW(key,kRunValue);if(result==ERROR_FILE_NOT_FOUND)result=ERROR_SUCCESS;
    }
    RegCloseKey(key);return result==ERROR_SUCCESS;
}
Gdiplus::Bitmap* GitHubLogo() {
    if(g_githubLogo)return g_githubLogo.get();
    HRSRC resource=FindResourceW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(IDB_GITHUB_LOGO),RT_RCDATA);
    if(!resource)return nullptr;
    const DWORD size=SizeofResource(GetModuleHandleW(nullptr),resource);
    const void* data=LockResource(LoadResource(GetModuleHandleW(nullptr),resource));
    if(!data||!size)return nullptr;
    IStream* stream=SHCreateMemStream(static_cast<const BYTE*>(data),size);
    if(!stream)return nullptr;
    auto logo=std::make_unique<Gdiplus::Bitmap>(stream);stream->Release();
    if(logo->GetLastStatus()!=Gdiplus::Ok)return nullptr;
    g_githubLogo=std::move(logo);return g_githubLogo.get();
}
void SelectPage(int page) {
    for(HWND control:g_themePage)ShowWindow(control,page==0?SW_SHOW:SW_HIDE);
    for(HWND control:g_autoPage)ShowWindow(control,page==1?SW_SHOW:SW_HIDE);
    for(HWND control:g_backupPage)ShowWindow(control,page==2?SW_SHOW:SW_HIDE);
    for(HWND control:g_aboutPage)ShowWindow(control,page==3?SW_SHOW:SW_HIDE);
    for(HWND control:g_widgetsPage)ShowWindow(control,page==4?SW_SHOW:SW_HIDE);
    if(g_window)InvalidateRect(g_window,nullptr,TRUE);
}
std::filesystem::path BackupDirectory(){auto path=std::filesystem::path(LayoutPath()).parent_path()/L"backups";std::error_code error;std::filesystem::create_directories(path,error);return path;}
std::wstring BackupStamp(){SYSTEMTIME t{};GetLocalTime(&t);wchar_t value[32]{};swprintf_s(value,L"%04u%02u%02u%02u%02u%02u",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);return value;}
void RefreshBackups(){if(!g_backupList)return;SendMessageW(g_backupList,LB_RESETCONTENT,0,0);std::error_code error;for(const auto& entry:std::filesystem::directory_iterator(BackupDirectory(),error))if(entry.is_regular_file()&&entry.path().extension()==L".backup")SendMessageW(g_backupList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(entry.path().filename().c_str()));}
void AddAutoRuleRow(const Layout::AutoRule& rule) {
    if(!g_autoRules||!g_layout)return;
    std::wstring title=L"规则";for(const auto& box:g_layout->boxes)if(box.id==rule.boxId){title=box.title;break;}
    LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=ListView_GetItemCount(g_autoRules);item.pszText=const_cast<wchar_t*>(title.c_str());
    int row=ListView_InsertItem(g_autoRules,&item);ListView_SetCheckState(g_autoRules,row,TRUE);
    ListView_SetItemText(g_autoRules,row,1,const_cast<wchar_t*>(L"*"));
    std::wstring matcher=rule.folders?(rule.extensions.empty()?L"文件夹":L"文件夹;"+rule.extensions):rule.extensions;
    ListView_SetItemText(g_autoRules,row,2,const_cast<wchar_t*>(matcher.c_str()));
    ListView_SetItemText(g_autoRules,row,3,const_cast<wchar_t*>(title.c_str()));
}
int g_selectedBox = 0;
constexpr COLORREF kColors[] = {RGB(184, 235, 238), RGB(197, 226, 250), RGB(210, 245, 228), RGB(255, 225, 190), RGB(245, 211, 240), RGB(218, 220, 250)};

void UpdateValue() {
    if (!g_layout || !g_value) return;
    wchar_t text[32];
    swprintf_s(text, L"%d%%", g_layout->opacity);
    SetWindowTextW(g_value, text);
    CanvasSetOpacity(g_layout->opacity);
    SaveLayout(*g_layout);
}
void UpdateCornerValue() {
    if(!g_layout||!g_cornerValue)return;
    wchar_t text[32]{};swprintf_s(text,L"%d",g_layout->cornerRadius);
    SetWindowTextW(g_cornerValue,text);
    SaveLayout(*g_layout);
    CanvasSetOpacity(g_layout->opacity);
}
void UpdateDefaultBoxMetrics() {
    if(!g_layout||!g_defaultWidth||!g_defaultHeight)return;
    wchar_t value[32]{};GetWindowTextW(g_defaultWidth,value,32);g_layout->defaultBoxWidth=std::clamp(_wtoi(value),240,800);
    swprintf_s(value,L"%d",g_layout->defaultBoxWidth);SetWindowTextW(g_defaultWidth,value);
    GetWindowTextW(g_defaultHeight,value,32);g_layout->defaultBoxHeight=std::clamp(_wtoi(value),160,800);
    swprintf_s(value,L"%d",g_layout->defaultBoxHeight);SetWindowTextW(g_defaultHeight,value);
    SaveLayout(*g_layout);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize={540,590};
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(hwnd,&paint);RECT client{};GetClientRect(hwnd,&client);
        HBRUSH base=CreateSolidBrush(RGB(247,250,252));FillRect(dc,&client,base);DeleteObject(base);
        RECT navigation{0,0,144,client.bottom};HBRUSH nav=CreateSolidBrush(RGB(230,242,247));FillRect(dc,&navigation,nav);DeleteObject(nav);
        RECT content{156,12,client.right-14,client.bottom-14};HBRUSH card=CreateSolidBrush(RGB(255,255,255));FillRect(dc,&content,card);DeleteObject(card);
        HPEN pen=CreatePen(PS_SOLID,1,RGB(211,226,232));HGDIOBJ old=SelectObject(dc,pen);MoveToEx(dc,144,16,nullptr);LineTo(dc,144,client.bottom-16);SelectObject(dc,old);DeleteObject(pen);
        EndPaint(hwnd,&paint);return 0;
    }
    case WM_SIZE: {
        if(g_close)SetWindowPos(g_close,nullptr,max(156,LOWORD(lParam)-172),max(20,HIWORD(lParam)-70),92,30,SWP_NOZORDER|SWP_NOACTIVATE);
        if(g_sidebarAbout)SetWindowPos(g_sidebarAbout,nullptr,12,max(264,HIWORD(lParam)-54),116,34,SWP_NOZORDER|SWP_NOACTIVATE);
        InvalidateRect(hwnd,nullptr,TRUE);
        return 0;
    }
    case WM_CREATE: {
        HFONT font = g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        HWND sidebar = CreateWindowW(L"STATIC", L"nestlone-D", WS_CHILD | WS_VISIBLE, 20, 24, 120, 28, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(sidebar, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND theme = CreateWindowW(L"BUTTON", L"主题设置", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 12, 60, 116, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarTheme)), nullptr, nullptr);
        SendMessageW(theme, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND automatic = CreateWindowW(L"BUTTON", L"自动分类", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 12, 100, 116, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarAuto)), nullptr, nullptr);
        SendMessageW(automatic, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND backup=CreateWindowW(L"BUTTON",L"备份与还原",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,12,140,116,34,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarBackup)),nullptr,nullptr);SendMessageW(backup,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND widgets=CreateWindowW(L"BUTTON",L"组件",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,12,180,116,34,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarWidgets)),nullptr,nullptr);SendMessageW(widgets,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        g_sidebarAbout=CreateWindowW(L"BUTTON",L"关于",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,12,536,116,34,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarAbout)),nullptr,nullptr);SendMessageW(g_sidebarAbout,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND title = CreateWindowW(L"STATIC", L"主题设置", WS_CHILD | WS_VISIBLE, 170, 20, 250, 28, hwnd, nullptr, nullptr, nullptr);
        g_themePage.push_back(title);
        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND hint = CreateWindowW(L"STATIC", L"背景不透明度", WS_CHILD | WS_VISIBLE, 170, 66, 180, 24, hwnd, nullptr, nullptr, nullptr);
        g_themePage.push_back(hint);
        SendMessageW(hint, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_value = CreateWindowW(L"STATIC", L"88%", WS_CHILD | WS_VISIBLE | SS_RIGHT, 390, 66, 70, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kValue)), nullptr, nullptr);
        g_themePage.push_back(g_value);
        SendMessageW(g_value, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND slider = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_NOTICKS, 170, 98, 290, 40, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSlider)), nullptr, nullptr);
        g_themePage.push_back(slider);
        SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELONG(0, 100));
        SendMessageW(slider, TBM_SETPOS, TRUE, g_layout ? g_layout->opacity : 88);
        SendMessageW(slider, TBM_SETPAGESIZE, 0, 5);
        SendMessageW(slider, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND cornerHint=CreateWindowW(L"STATIC",L"圆角程度",WS_CHILD|WS_VISIBLE,170,148,100,24,hwnd,nullptr,nullptr,nullptr);
        g_themePage.push_back(cornerHint);SendMessageW(cornerHint,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        g_cornerValue=CreateWindowW(L"STATIC",L"12",WS_CHILD|WS_VISIBLE|SS_RIGHT,390,148,70,24,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCornerValue)),nullptr,nullptr);
        g_themePage.push_back(g_cornerValue);SendMessageW(g_cornerValue,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND cornerSlider=CreateWindowExW(0,TRACKBAR_CLASSW,L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|TBS_NOTICKS,170,180,290,40,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCornerSlider)),nullptr,nullptr);
        g_themePage.push_back(cornerSlider);SendMessageW(cornerSlider,TBM_SETRANGE,TRUE,MAKELONG(0,48));SendMessageW(cornerSlider,TBM_SETPOS,TRUE,g_layout?g_layout->cornerRadius:12);SendMessageW(cornerSlider,TBM_SETPAGESIZE,0,4);SendMessageW(cornerSlider,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND boxLabel = CreateWindowW(L"STATIC", L"当前盒子", WS_CHILD | WS_VISIBLE, 170, 228, 90, 24, hwnd, nullptr, nullptr, nullptr);
        g_themePage.push_back(boxLabel);
        SendMessageW(boxLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_boxSelect = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 260, 224, 200, 180, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBoxSelect)), nullptr, nullptr);
        g_themePage.push_back(g_boxSelect);
        for (const Box& box : g_layout->boxes) SendMessageW(g_boxSelect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(box.title.c_str()));
        if (!g_layout->boxes.empty()) SendMessageW(g_boxSelect, CB_SETCURSEL, 0, 0);
        HWND colorLabel = CreateWindowW(L"STATIC", L"盒子背景色", WS_CHILD | WS_VISIBLE, 170, 266, 100, 24, hwnd, nullptr, nullptr, nullptr);
        g_themePage.push_back(colorLabel);
        SendMessageW(colorLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (int i = 0; i < 6; ++i) {
            HWND swatch = CreateWindowW(L"BUTTON", L"  ", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 280 + i * 30, 262, 24, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kColorBase + i)), nullptr, nullptr);
            g_themePage.push_back(swatch);
            SendMessageW(swatch, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        g_close = CreateWindowW(L"BUTTON", L"完成", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 368, 510, 92, 30, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        SendMessageW(g_close, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(g_boxSelect, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_selectedBox = 0;
        wchar_t value[32]{};
        swprintf_s(value,L"%d%%",g_layout->opacity);
        SetWindowTextW(g_value,value);
        UpdateCornerValue();
        HWND note=CreateWindowW(L"STATIC",L"即时预览 · 自动保存 · 图标与文字保持清晰",
            WS_CHILD|WS_VISIBLE,170,308,330,24,hwnd,nullptr,nullptr,nullptr);
        g_themePage.push_back(note);
        SendMessageW(note,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND startup=CreateWindowW(L"BUTTON",L"开机后自动启动 nestlone-D",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,170,344,250,26,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kStartup)),nullptr,nullptr);
        g_themePage.push_back(startup);
        SendMessageW(startup,BM_SETCHECK,StartupEnabled()?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(startup,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND defaultSize=CreateWindowW(L"STATIC",L"新建盒子默认尺寸",WS_CHILD|WS_VISIBLE,170,382,140,24,hwnd,nullptr,nullptr,nullptr);g_themePage.push_back(defaultSize);SendMessageW(defaultSize,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        g_defaultWidth=CreateWindowW(L"EDIT",std::to_wstring(g_layout->defaultBoxWidth).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL,310,378,64,24,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDefaultWidth)),nullptr,nullptr);g_themePage.push_back(g_defaultWidth);SendMessageW(g_defaultWidth,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND by=CreateWindowW(L"STATIC",L"×",WS_CHILD|WS_VISIBLE,380,382,18,24,hwnd,nullptr,nullptr,nullptr);g_themePage.push_back(by);SendMessageW(by,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        g_defaultHeight=CreateWindowW(L"EDIT",std::to_wstring(g_layout->defaultBoxHeight).c_str(),WS_CHILD|WS_VISIBLE|WS_BORDER|ES_NUMBER|ES_AUTOHSCROLL,400,378,64,24,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDefaultHeight)),nullptr,nullptr);g_themePage.push_back(g_defaultHeight);SendMessageW(g_defaultHeight,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND defaultColor=CreateWindowW(L"STATIC",L"默认配色",WS_CHILD|WS_VISIBLE,170,418,90,24,hwnd,nullptr,nullptr,nullptr);g_themePage.push_back(defaultColor);SendMessageW(defaultColor,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        for(int i=0;i<6;++i){HWND swatch=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,260+i*30,414,24,24,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDefaultColorBase+i)),nullptr,nullptr);g_themePage.push_back(swatch);}
        HWND fontLabel=CreateWindowW(L"STATIC",L"图标与标题字体",WS_CHILD|WS_VISIBLE,170,456,120,24,hwnd,nullptr,nullptr,nullptr);g_themePage.push_back(fontLabel);SendMessageW(fontLabel,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        g_fontFamily=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,300,452,164,120,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFontFamily)),nullptr,nullptr);g_themePage.push_back(g_fontFamily);for(const wchar_t* face:{L"Microsoft YaHei UI",L"Segoe UI",L"SimSun"})SendMessageW(g_fontFamily,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(face));int faceIndex=0;if(g_layout->fontFamily==L"Segoe UI")faceIndex=1;else if(g_layout->fontFamily==L"SimSun")faceIndex=2;SendMessageW(g_fontFamily,CB_SETCURSEL,faceIndex,0);SendMessageW(g_fontFamily,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND autoTitle=CreateWindowW(L"STATIC",L"自动分类",WS_CHILD|WS_VISIBLE,170,20,180,24,hwnd,nullptr,nullptr,nullptr);
        g_autoPage.push_back(autoTitle);
        SendMessageW(autoTitle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND enable=CreateWindowW(L"BUTTON",L"开启自动整理新文件",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,170,58,190,24,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoEnable)),nullptr,nullptr);
        g_autoPage.push_back(enable);
        SendMessageW(enable,BM_SETCHECK,g_layout&&g_layout->autoOrganize?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(enable,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND folders=CreateWindowW(L"BUTTON",L"匹配文件夹",WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,170,90,110,24,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoFolders)),nullptr,nullptr);
        g_autoPage.push_back(folders);
        SendMessageW(folders,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        g_autoExtensions=CreateWindowW(L"EDIT",L".pdf;.docx;.zip",WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL,282,90,178,24,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoExtensions)),nullptr,nullptr);
        g_autoPage.push_back(g_autoExtensions);
        SendMessageW(g_autoExtensions,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        g_autoBox=CreateWindowW(L"COMBOBOX",L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,170,124,190,150,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoBox)),nullptr,nullptr);
        g_autoPage.push_back(g_autoBox);
        for(const Box& box:g_layout->boxes)SendMessageW(g_autoBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(box.title.c_str()));
        if(!g_layout->boxes.empty())SendMessageW(g_autoBox,CB_SETCURSEL,0,0);SendMessageW(g_autoBox,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND add=CreateWindowW(L"BUTTON",L"添加规则",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,370,124,90,26,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoAdd)),nullptr,nullptr);SendMessageW(add,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        g_autoPage.push_back(add);
        const wchar_t* defaults[]={L"目录",L"文档",L"图片",L"压缩包"};
        for(int i=0;i<4;++i){HWND preset=CreateWindowW(L"BUTTON",defaults[i],WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,170+(i%2)*100,160+(i/2)*26,92,22,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDefaultBoxBase+i)),nullptr,nullptr);SendMessageW(preset,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_autoPage.push_back(preset);}
        HWND ruleLabel=CreateWindowW(L"STATIC",L"自定义规则",WS_CHILD|WS_VISIBLE,170,218,150,24,hwnd,nullptr,nullptr,nullptr);SendMessageW(ruleLabel,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_autoPage.push_back(ruleLabel);
        g_autoRules=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SHOWSELALWAYS,170,246,290,142,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoRules)),nullptr,nullptr);
        ListView_SetExtendedListViewStyle(g_autoRules,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
        const wchar_t* headers[]={L"标题",L"文件名",L"后缀",L"盒子"};const int widths[]={68,54,104,60};
        for(int i=0;i<4;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=widths[i];column.pszText=const_cast<wchar_t*>(headers[i]);ListView_InsertColumn(g_autoRules,i,&column);}
        for(const auto& rule:g_layout->autoRules)AddAutoRuleRow(rule);g_autoPage.push_back(g_autoRules);
        HWND backupTitle=CreateWindowW(L"STATIC",L"桌面布局备份",WS_CHILD|WS_VISIBLE,170,20,180,24,hwnd,nullptr,nullptr,nullptr);SendMessageW(backupTitle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_backupPage.push_back(backupTitle);
        HWND backupHint=CreateWindowW(L"STATIC",L"每份备份保存当前盒子、分组、规则与桌面位置。",WS_CHILD|WS_VISIBLE,170,52,290,22,hwnd,nullptr,nullptr,nullptr);SendMessageW(backupHint,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_backupPage.push_back(backupHint);
        g_backupList=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|LBS_NOTIFY|WS_VSCROLL,170,84,290,270,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackupList)),nullptr,nullptr);SendMessageW(g_backupList,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_backupPage.push_back(g_backupList);RefreshBackups();
        const int backupIds[]={kBackupCreate,kBackupApply,kBackupDelete};const wchar_t* backupLabels[]={L"立即备份",L"应用备份",L"删除备份"};for(int i=0;i<3;++i){HWND action=CreateWindowW(L"BUTTON",backupLabels[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,170+i*98,370,90,28,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(backupIds[i])),nullptr,nullptr);SendMessageW(action,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_backupPage.push_back(action);}
        HWND aboutTitle=CreateWindowW(L"STATIC",L"关于 nestlone-D",WS_CHILD|WS_VISIBLE,170,20,260,30,hwnd,nullptr,nullptr,nullptr);SendMessageW(aboutTitle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_aboutPage.push_back(aboutTitle);
        HWND aboutVersion=CreateWindowW(L"STATIC",L"nestlone-D · Windows 桌面整理工具",WS_CHILD|WS_VISIBLE,170,62,290,24,hwnd,nullptr,nullptr,nullptr);SendMessageW(aboutVersion,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_aboutPage.push_back(aboutVersion);
        HWND aboutDescription=CreateWindowW(L"STATIC",L"将桌面图标按盒子、分组与规则进行收纳。\r\n保留原生文件、快捷方式与桌面操作体验。",WS_CHILD|WS_VISIBLE,170,102,300,56,hwnd,nullptr,nullptr,nullptr);SendMessageW(aboutDescription,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_aboutPage.push_back(aboutDescription);
        HWND repository=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,170,188,180,48,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutRepository)),nullptr,nullptr);SendMessageW(repository,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_aboutPage.push_back(repository);
        HWND widgetsTitle=CreateWindowW(L"STATIC",L"组件",WS_CHILD|WS_VISIBLE,170,20,180,28,hwnd,nullptr,nullptr,nullptr);SendMessageW(widgetsTitle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_widgetsPage.push_back(widgetsTitle);
        HWND noteWidget=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,170,66,132,170,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWidgetNote)),nullptr,nullptr);SendMessageW(noteWidget,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_widgetsPage.push_back(noteWidget);
        HWND weatherWidget=CreateWindowW(L"BUTTON",L"",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,316,66,132,170,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWidgetWeather)),nullptr,nullptr);SendMessageW(weatherWidget,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_widgetsPage.push_back(weatherWidget);
        HWND clearWidgets=CreateWindowW(L"BUTTON",L"清除全部组件",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,170,252,278,30,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kWidgetClear)),nullptr,nullptr);SendMessageW(clearWidgets,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_widgetsPage.push_back(clearWidgets);
        // All page controls must exist before the first visibility pass.
        SelectPage(0);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc=reinterpret_cast<HDC>(wParam);
        SetTextColor(dc,theme::title);
        SetBkMode(dc,TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) && GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == kSlider) {
            g_layout->opacity = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), TBM_GETPOS, 0, 0));
            UpdateValue();
        }
        if (reinterpret_cast<HWND>(lParam) && GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == kCornerSlider) {
            g_layout->cornerRadius=static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam),TBM_GETPOS,0,0));
            UpdateCornerValue();
        }
        return 0;
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if(draw&&(draw->CtlID==kWidgetNote||draw->CtlID==kWidgetWeather)) {
            const RECT r=draw->rcItem;const bool note=draw->CtlID==kWidgetNote;
            const COLORREF body=note?RGB(255,247,190):RGB(215,241,248),header=note?RGB(246,220,122):RGB(157,216,231),border=note?RGB(227,193,82):RGB(111,193,213);
            HBRUSH brush=CreateSolidBrush(body);HPEN pen=CreatePen(PS_SOLID,1,border);HGDIOBJ oldBrush=SelectObject(draw->hDC,brush),oldPen=SelectObject(draw->hDC,pen);RoundRect(draw->hDC,r.left,r.top,r.right,r.bottom,16,16);SelectObject(draw->hDC,oldBrush);SelectObject(draw->hDC,oldPen);DeleteObject(brush);DeleteObject(pen);
            HBRUSH head=CreateSolidBrush(header);RECT top{r.left+1,r.top+1,r.right-1,r.top+32};FillRect(draw->hDC,&top,head);DeleteObject(head);SetBkMode(draw->hDC,TRANSPARENT);SetTextColor(draw->hDC,RGB(25,49,60));HFONT old=static_cast<HFONT>(SelectObject(draw->hDC,g_font));
            if(note){HPEN line=CreatePen(PS_SOLID,2,RGB(182,139,50));HGDIOBJ oldLine=SelectObject(draw->hDC,line);for(int y=r.top+56;y<r.top+112;y+=18){MoveToEx(draw->hDC,r.left+24,y,nullptr);LineTo(draw->hDC,r.right-24,y);}SelectObject(draw->hDC,oldLine);DeleteObject(line);}else{HBRUSH sun=CreateSolidBrush(RGB(255,201,78));Ellipse(draw->hDC,r.left+40,r.top+55,r.left+80,r.top+95);DeleteObject(sun);HBRUSH cloud=CreateSolidBrush(RGB(255,255,255));Ellipse(draw->hDC,r.left+58,r.top+75,r.left+101,r.top+105);Ellipse(draw->hDC,r.left+40,r.top+80,r.left+82,r.top+108);DeleteObject(cloud);}
            RECT caption{r.left+8,r.bottom-38,r.right-8,r.bottom-10};DrawTextW(draw->hDC,note?L"便签":L"天气",-1,&caption,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SelectObject(draw->hDC,old);return TRUE;
        }
        if(draw&&draw->CtlID==kAboutRepository) {
            if(auto* logo=GitHubLogo()) {
                Gdiplus::Graphics graphics(draw->hDC);graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                const int width=168,height=39,left=draw->rcItem.left+(draw->rcItem.right-draw->rcItem.left-width)/2,top=draw->rcItem.top+(draw->rcItem.bottom-draw->rcItem.top-height)/2;
                graphics.DrawImage(logo,left,top,width,height);
            }
            return TRUE;
        }
        if (draw && (draw->CtlID == kSidebarTheme || draw->CtlID == kSidebarAuto || draw->CtlID == kSidebarBackup || draw->CtlID == kSidebarAbout || draw->CtlID == kSidebarWidgets || draw->CtlID == kWidgetClear || draw->CtlID == kAutoAdd || (draw->CtlID>=kBackupCreate&&draw->CtlID<=kBackupDelete) || draw->CtlID == IDCANCEL)) {
            const COLORREF fill=RGB(219,242,246);
            const COLORREF border=RGB(190,224,230);
            HBRUSH brush=CreateSolidBrush(fill);
            HPEN pen=CreatePen(PS_SOLID,1,border);
            auto ob=SelectObject(draw->hDC,brush); auto op=SelectObject(draw->hDC,pen);
            const RECT& r=draw->rcItem;
            RoundRect(draw->hDC,r.left,r.top,r.right,r.bottom,12,12);
            SelectObject(draw->hDC,ob); SelectObject(draw->hDC,op);
            DeleteObject(brush); DeleteObject(pen);
            auto of=SelectObject(draw->hDC,g_font);
            SetBkMode(draw->hDC,TRANSPARENT); SetTextColor(draw->hDC,theme::title);
            const wchar_t* caption=draw->CtlID==IDCANCEL?L"完成":draw->CtlID==kSidebarAuto?L"自动分类":draw->CtlID==kSidebarBackup?L"备份与还原":draw->CtlID==kSidebarAbout?L"关于":draw->CtlID==kSidebarWidgets?L"组件":draw->CtlID==kWidgetClear?L"清除全部组件":draw->CtlID==kAutoAdd?L"添加规则":draw->CtlID==kBackupCreate?L"立即备份":draw->CtlID==kBackupApply?L"应用备份":draw->CtlID==kBackupDelete?L"删除备份":L"主题设置";
            RECT label=r; DrawTextW(draw->hDC,caption,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(draw->itemState & ODS_FOCUS) { InflateRect(&label,-3,-3); DrawFocusRect(draw->hDC,&label); }
            SelectObject(draw->hDC,of);
            return TRUE;
        }
        if (draw && ((draw->CtlID >= kColorBase && draw->CtlID < kColorBase + 6) || (draw->CtlID >= kDefaultColorBase && draw->CtlID < kDefaultColorBase + 6))) {
            const int colorIndex=draw->CtlID>=kDefaultColorBase?draw->CtlID-kDefaultColorBase:draw->CtlID-kColorBase;
            HBRUSH brush = CreateSolidBrush(kColors[colorIndex]);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);
            FrameRect(draw->hDC, &draw->rcItem, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            return TRUE;
        }
        return FALSE;
    }
    case WM_COMMAND:
        if(LOWORD(wParam)==kSidebarTheme){SelectPage(0);return 0;}
        if(LOWORD(wParam)==kSidebarAuto){SelectPage(1);return 0;}
        if(LOWORD(wParam)==kSidebarBackup){SelectPage(2);return 0;}
        if(LOWORD(wParam)==kSidebarAbout){SelectPage(3);return 0;}
        if(LOWORD(wParam)==kSidebarWidgets){SelectPage(4);return 0;}
        if(LOWORD(wParam)==kWidgetNote){HandleCanvasCommand(CanvasCommand::NewNote);return 0;}
        if(LOWORD(wParam)==kWidgetWeather){HandleCanvasCommand(CanvasCommand::NewWeather);return 0;}
        if(LOWORD(wParam)==kWidgetClear){HandleCanvasCommand(CanvasCommand::ClearWidgets);return 0;}
        if(LOWORD(wParam)==kAboutRepository){ShellExecuteW(hwnd,L"open",L"https://github.com/nestlone/nestlone-Desktop",nullptr,nullptr,SW_SHOWNORMAL);return 0;}
        if(LOWORD(wParam)==kStartup) {
            const bool enabled=SendMessageW(reinterpret_cast<HWND>(lParam),BM_GETCHECK,0,0)==BST_CHECKED;
            if(!SetStartupEnabled(enabled)) {
                SendMessageW(reinterpret_cast<HWND>(lParam),BM_SETCHECK,StartupEnabled()?BST_CHECKED:BST_UNCHECKED,0);
                MessageBoxW(hwnd,L"无法更新开机自启动设置。",L"nestlone-D",MB_OK|MB_ICONWARNING);
            }
            return 0;
        }
        if(LOWORD(wParam)==kBackupCreate&&g_layout){SaveLayout(*g_layout);auto target=BackupDirectory()/(BackupStamp()+L".backup");CopyFileW(LayoutPath().c_str(),target.c_str(),FALSE);RefreshBackups();return 0;}
        if((LOWORD(wParam)==kBackupApply||LOWORD(wParam)==kBackupDelete)&&g_backupList){int index=static_cast<int>(SendMessageW(g_backupList,LB_GETCURSEL,0,0));if(index!=LB_ERR){wchar_t name[MAX_PATH]{};SendMessageW(g_backupList,LB_GETTEXT,index,reinterpret_cast<LPARAM>(name));auto path=BackupDirectory()/name;if(LOWORD(wParam)==kBackupApply){if(CopyFileW(path.c_str(),LayoutPath().c_str(),FALSE))HandleCanvasCommand(CanvasCommand::Reload);}else{DeleteFileW(path.c_str());RefreshBackups();}}return 0;}
        if(LOWORD(wParam)==kAutoEnable&&g_layout){g_layout->autoOrganize=SendMessageW(reinterpret_cast<HWND>(lParam),BM_GETCHECK,0,0)==BST_CHECKED;SaveLayout(*g_layout);return 0;}
        if(LOWORD(wParam)==kAutoAdd&&g_layout&&g_autoBox&&g_autoExtensions){int index=static_cast<int>(SendMessageW(g_autoBox,CB_GETCURSEL,0,0));if(index>=0&&index<static_cast<int>(g_layout->boxes.size())){wchar_t extensions[512]{};GetWindowTextW(g_autoExtensions,extensions,512);HWND folders=GetDlgItem(hwnd,kAutoFolders);g_layout->autoRules.push_back({g_layout->boxes[index].id,SendMessageW(folders,BM_GETCHECK,0,0)==BST_CHECKED,extensions});AddAutoRuleRow(g_layout->autoRules.back());SaveLayout(*g_layout);}return 0;}
        if(LOWORD(wParam)>=kDefaultBoxBase&&LOWORD(wParam)<kDefaultBoxBase+4&&g_layout){int choice=LOWORD(wParam)-kDefaultBoxBase;if(SendMessageW(reinterpret_cast<HWND>(lParam),BM_GETCHECK,0,0)==BST_CHECKED){const wchar_t* titles[]={L"目录",L"文档",L"图片",L"压缩包"};const wchar_t* suffixes[]={L"",L".doc;.docx;.pdf;.txt;.xls;.xlsx;.ppt;.pptx",L".png;.jpg;.jpeg;.gif;.bmp;.webp",L".zip;.rar;.7z;.tar;.gz"};auto found=std::find_if(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const Box& box){return box.title==titles[choice];});if(found==g_layout->boxes.end()){Box box;box.id=std::to_wstring(GetTickCount64())+L"-preset";box.title=titles[choice];box.rect={120+choice*35,120+choice*35,460+choice*35,420+choice*35};g_layout->boxes.push_back(std::move(box));found=std::prev(g_layout->boxes.end());}g_layout->autoRules.push_back({found->id,choice==0,suffixes[choice]});for(const auto& item:EnumerateDesktopItems())if((choice==0&&item.directory)||(choice>0&&!item.directory&&wcsstr(suffixes[choice],item.type.c_str())))AddItem(*found,item.path);SaveLayout(*g_layout);HandleCanvasCommand(CanvasCommand::Reload);}return 0;}
        if (LOWORD(wParam) == kBoxSelect && HIWORD(wParam) == CBN_SELCHANGE) {
            g_selectedBox = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0));
            return 0;
        }
        if(LOWORD(wParam)==kFontFamily&&HIWORD(wParam)==CBN_SELCHANGE&&g_layout){wchar_t family[64]{};const int index=static_cast<int>(SendMessageW(g_fontFamily,CB_GETCURSEL,0,0));SendMessageW(g_fontFamily,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(family));g_layout->fontFamily=family;SaveLayout(*g_layout);CanvasSetOpacity(g_layout->opacity);return 0;}
        if((LOWORD(wParam)==kDefaultWidth||LOWORD(wParam)==kDefaultHeight)&&HIWORD(wParam)==EN_KILLFOCUS){UpdateDefaultBoxMetrics();return 0;}
        if(LOWORD(wParam)>=kDefaultColorBase&&LOWORD(wParam)<kDefaultColorBase+6&&g_layout){g_layout->defaultBoxColor=kColors[LOWORD(wParam)-kDefaultColorBase];SaveLayout(*g_layout);return 0;}
        if (LOWORD(wParam) >= kColorBase && LOWORD(wParam) < kColorBase + 6 && g_layout && g_selectedBox >= 0 && g_selectedBox < static_cast<int>(g_layout->boxes.size())) {
            g_layout->boxes[g_selectedBox].color = kColors[LOWORD(wParam) - kColorBase];
            SaveLayout(*g_layout);
            CanvasSetOpacity(g_layout->opacity);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: if (g_font) { DeleteObject(g_font); g_font=nullptr; } g_window = nullptr; g_close=nullptr;g_sidebarAbout=nullptr; g_value = nullptr; g_cornerValue=nullptr;g_defaultWidth=nullptr;g_defaultHeight=nullptr;g_fontFamily=nullptr; g_boxSelect=nullptr;g_autoBox=nullptr;g_autoExtensions=nullptr;g_autoRules=nullptr;g_backupList=nullptr;g_themePage.clear();g_autoPage.clear();g_backupPage.clear();g_aboutPage.clear();g_widgetsPage.clear();g_layout = nullptr; return 0;
    default: return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

void ShowSettings(HINSTANCE instance, Layout* layout) {
    g_layout = layout;
    if (g_window) { ShowWindow(g_window, SW_SHOWNORMAL); SetForegroundWindow(g_window); return; }
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = SettingsProc;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!g_surface) g_surface=CreateSolidBrush(RGB(246,250,252));
    wc.hbrBackground = g_surface;
    RegisterClassW(&wc);
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"nestlone-D 设置中心", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 540, 590, nullptr, nullptr, instance, nullptr);
    if (!g_window) return;
    ShowWindow(g_window, SW_SHOWNORMAL);
    UpdateWindow(g_window);
}

}

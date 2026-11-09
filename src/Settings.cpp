#include "Settings.h"
#include "DesktopCanvas.h"
#include "DesktopItems.h"
#include "resource.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <gdiplus.h>
#include <memory>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <string_view>
#include <utility>
#include <wrl/client.h>

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
int g_page=0;
HFONT g_headingFont=nullptr;
constexpr int kAboutRepository = 2501;
constexpr int kWidgetNote = 2601;
constexpr int kWidgetClear = 2603;
constexpr int kWidgetReopen = 2604;
constexpr int kNoteList = 2605;
constexpr int kNoteSearch = 2606;
constexpr int kNotePreview = 2607;
constexpr int kNoteSummary = 2608;
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
constexpr int kAutoDelete = 2307;
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
std::vector<std::wstring> g_noteRows;
bool g_refreshingNotes=false;
Microsoft::WRL::ComPtr<IStream> g_githubLogoStream;
std::unique_ptr<Gdiplus::Bitmap> g_githubLogo;
std::vector<std::wstring> g_boxIds;
struct SettingsControl { HWND window; RECT bounds; };
std::vector<SettingsControl> g_contentControls;
constexpr int kNavWidth=152;
constexpr int kContentLeft=178;
constexpr COLORREF kInk=RGB(26,26,26);
constexpr COLORREF kMuted=RGB(96,96,96);
constexpr COLORREF kPaper=RGB(255,255,255);
constexpr COLORREF kNav=RGB(244,244,244);
constexpr COLORREF kLine=RGB(221,221,221);
constexpr const wchar_t* kPresetTitles[]={L"目录",L"文档",L"图片",L"压缩包"};
constexpr const wchar_t* kPresetExtensions[]={L"",L".doc;.docx;.pdf;.txt;.xls;.xlsx;.ppt;.pptx",
    L".png;.jpg;.jpeg;.gif;.bmp;.webp",L".zip;.rar;.7z;.tar;.gz"};
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
    const bool enabled=RegQueryValueExW(key,kRunValue,nullptr,&type,reinterpret_cast<BYTE*>(value),&size)==ERROR_SUCCESS&&
        type==REG_SZ&&value[0]!=L'\0';
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
    g_githubLogoStream.Attach(SHCreateMemStream(static_cast<const BYTE*>(data),size));
    if(!g_githubLogoStream)return nullptr;
    auto logo=std::make_unique<Gdiplus::Bitmap>(g_githubLogoStream.Get());
    if(logo->GetLastStatus()!=Gdiplus::Ok){logo.reset();g_githubLogoStream.Reset();return nullptr;}
    g_githubLogo=std::move(logo);return g_githubLogo.get();
}
void RefreshBoxChoices();
void RefreshRuleRows();
std::wstring SelectedNoteId(HWND hwnd) {
    const int row=ListView_GetNextItem(GetDlgItem(hwnd,kNoteList),-1,LVNI_SELECTED);
    return row>=0&&static_cast<size_t>(row)<g_noteRows.size()?g_noteRows[row]:L"";
}
void UpdateNoteSelection(HWND hwnd) {
    const auto id=SelectedNoteId(hwnd);
    const auto& notes=g_layout->widgets;
    const auto selected=std::find_if(notes.begin(),notes.end(),[&](const auto& note){return note.id==id;});
    const bool hasSelection=selected!=notes.end();
    SetDlgItemTextW(hwnd,kNotePreview,hasSelection?selected->text.c_str():L"");
    EnableWindow(GetDlgItem(hwnd,kWidgetReopen),hasSelection);
    EnableWindow(GetDlgItem(hwnd,kWidgetClear),!g_layout->widgets.empty());
}
void RefreshNotes(HWND hwnd,const std::wstring& preferred={}) {
    const HWND list=GetDlgItem(hwnd,kNoteList);
    if(!list||g_refreshingNotes)return;
    const auto selectedId=preferred.empty()?SelectedNoteId(hwnd):preferred;
    wchar_t query[181]{};
    GetDlgItemTextW(hwnd,kNoteSearch,query,181);
    g_refreshingNotes=true;
    SendMessageW(list,WM_SETREDRAW,FALSE,0);
    ListView_DeleteAllItems(list);
    g_noteRows.clear();
    int selectedRow=0;
    for(auto it=g_layout->widgets.rbegin();it!=g_layout->widgets.rend();++it) {
        const auto& note=*it;
        const auto start=note.text.find_first_not_of(L" \t\r\n");
        const bool empty=start==std::wstring::npos;
        if(*query&&!StrStrIW(empty?L"空白便签":note.text.c_str(),query))continue;
        const auto end=empty?start:note.text.find_first_of(L"\r\n",start);
        std::wstring title=empty?L"空白便签":note.text.substr(start,end==std::wstring::npos?end:end-start);
        LVITEMW item{};
        item.mask=LVIF_TEXT;
        item.iItem=static_cast<int>(g_noteRows.size());
        item.pszText=title.data();
        const int row=ListView_InsertItem(list,&item);
        if(row<0)continue;
        ListView_SetItemText(list,row,1,const_cast<wchar_t*>(note.closed?L"已关闭":L"桌面显示"));
        g_noteRows.push_back(note.id);
        if(note.id==selectedId)selectedRow=row;
    }
    if(!g_noteRows.empty()) {
        ListView_SetItemState(list,selectedRow,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        ListView_EnsureVisible(list,selectedRow,FALSE);
    }
    SendMessageW(list,WM_SETREDRAW,TRUE,0);
    InvalidateRect(list,nullptr,TRUE);
    g_refreshingNotes=false;
    const auto summary=g_layout->widgets.empty()?L"还没有便签":g_noteRows.empty()?L"未找到匹配的便签":
        std::to_wstring(g_noteRows.size())+L" 张便签";
    SetDlgItemTextW(hwnd,kNoteSummary,summary.c_str());
    UpdateNoteSelection(hwnd);
}
void SelectPage(HWND hwnd,int page) {
    g_page=page;
    const std::vector<HWND>* pages[]={&g_themePage,&g_autoPage,&g_backupPage,&g_aboutPage,&g_widgetsPage};
    for(int index=0;index<5;++index)if(index!=page)
        for(HWND control:*pages[index])ShowWindow(control,SW_HIDE);
    if(page==0||page==1)RefreshBoxChoices();
    if(page==1)RefreshRuleRows();
    for(HWND control:*pages[page])ShowWindow(control,SW_SHOW);
    if(page==4)RefreshNotes(hwnd);
    RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
}
std::filesystem::path BackupDirectory() {
    const auto layout=LayoutPath();
    return layout.empty()?std::filesystem::path{}:std::filesystem::path(layout).parent_path()/L"backups";
}
std::wstring BackupStamp() {
    SYSTEMTIME time{};GetLocalTime(&time);
    wchar_t stamp[32]{};
    swprintf_s(stamp,L"%04u%02u%02u%02u%02u%02u-%03u",time.wYear,time.wMonth,time.wDay,
        time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
    return stamp;
}
void UpdateBackupActions(HWND hwnd) {
    const bool selected=SendMessageW(g_backupList,LB_GETCURSEL,0,0)!=LB_ERR;
    EnableWindow(GetDlgItem(hwnd,kBackupApply),selected);
    EnableWindow(GetDlgItem(hwnd,kBackupDelete),selected);
}
void RefreshBackups(HWND hwnd) {
    if(!g_backupList)return;
    SendMessageW(g_backupList,LB_RESETCONTENT,0,0);
    std::vector<std::wstring> names;
    const auto directory=BackupDirectory();
    if(!directory.empty()) {
        std::error_code error;
        for(std::filesystem::directory_iterator it(directory,error),end;!error&&it!=end;it.increment(error)) {
            std::error_code entryError;
            if(it->is_regular_file(entryError)&&it->path().extension()==L".backup")
                names.push_back(it->path().filename().wstring());
        }
    }
    std::sort(names.rbegin(),names.rend());
    for(const auto& name:names)SendMessageW(g_backupList,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
    UpdateBackupActions(hwnd);
}
void AddAutoRuleRow(const Layout::AutoRule& rule) {
    if(!g_autoRules||!g_layout)return;
    std::wstring title=L"规则";for(const auto& box:g_layout->boxes)if(box.id==rule.boxId){title=box.title;break;}
    LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=ListView_GetItemCount(g_autoRules);item.pszText=const_cast<wchar_t*>(title.c_str());
    int row=ListView_InsertItem(g_autoRules,&item);
    if(row<0)return;
    ListView_SetCheckState(g_autoRules,row,rule.enabled);
    std::wstring matcher=rule.folders?(rule.extensions.empty()?L"文件夹":L"文件夹;"+rule.extensions):rule.extensions;
    ListView_SetItemText(g_autoRules,row,1,const_cast<wchar_t*>(matcher.c_str()));
    ListView_SetItemText(g_autoRules,row,2,const_cast<wchar_t*>(title.c_str()));
}
bool g_updatingRules=false;
void RefreshRuleRows() {
    if(!g_autoRules)return;
    g_updatingRules=true;
    ListView_DeleteAllItems(g_autoRules);
    for(const auto& rule:g_layout->autoRules)AddAutoRuleRow(rule);
    g_updatingRules=false;
    EnableWindow(GetDlgItem(GetParent(g_autoRules),kAutoDelete),FALSE);
}
std::wstring SelectedBoxId(HWND combo) {
    const LRESULT index=SendMessageW(combo,CB_GETCURSEL,0,0);
    return index>=0&&static_cast<size_t>(index)<g_boxIds.size()?g_boxIds[index]:L"";
}
Box* SelectedBox(HWND combo) {
    const auto id=SelectedBoxId(combo);
    const auto box=std::find_if(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const Box& b){return b.id==id;});
    return box==g_layout->boxes.end()?nullptr:&*box;
}
void RefreshBoxChoices() {
    if(!g_boxSelect||!g_autoBox)return;
    const auto themeBox=SelectedBoxId(g_boxSelect),autoBox=SelectedBoxId(g_autoBox);
    g_boxIds.clear();
    for(const auto& box:g_layout->boxes)g_boxIds.push_back(box.id);
    for(const auto& [combo,selected]:{std::pair{g_boxSelect,themeBox},std::pair{g_autoBox,autoBox}}) {
        SendMessageW(combo,CB_RESETCONTENT,0,0);
        int index=g_boxIds.empty()?-1:0;
        for(const auto& box:g_layout->boxes) {
            const auto row=SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(box.title.c_str()));
            if(box.id==selected&&row>=0)index=static_cast<int>(row);
        }
        SendMessageW(combo,CB_SETCURSEL,index,0);
        EnableWindow(combo,!g_boxIds.empty());
    }
    HWND hwnd=GetParent(g_boxSelect);
    EnableWindow(GetDlgItem(hwnd,kAutoAdd),!g_boxIds.empty());
    for(int i=0;i<6;++i)EnableWindow(GetDlgItem(hwnd,kColorBase+i),!g_boxIds.empty());
}
Box* SelectedColorBox() {
    auto* box=SelectedBox(g_boxSelect);
    if(!box||box->groupId.empty())return box;
    for(auto& root:g_layout->boxes)if(root.id==box->groupId)return &root;
    return box;
}
constexpr COLORREF kColors[] = {RGB(184, 235, 238), RGB(197, 226, 250), RGB(210, 245, 228), RGB(255, 225, 190), RGB(245, 211, 240), RGB(218, 220, 250)};

void UpdateValue() {
    if (!g_layout || !g_value) return;
    wchar_t text[32];
    swprintf_s(text, L"%d%%", g_layout->opacity);
    SetWindowTextW(g_value, text);
}
void UpdateCornerValue() {
    if(!g_layout||!g_cornerValue)return;
    wchar_t text[32]{};swprintf_s(text,L"%d",g_layout->cornerRadius);
    SetWindowTextW(g_cornerValue,text);
}
void UpdateDefaultBoxMetrics() {
    if(!g_layout||!g_defaultWidth||!g_defaultHeight)return;
    BOOL valid=FALSE;
    const HWND hwnd=GetParent(g_defaultWidth);
    const UINT width=GetDlgItemInt(hwnd,kDefaultWidth,&valid,FALSE);
    if(valid)g_layout->defaultBoxWidth=static_cast<int>(std::clamp(width,240u,800u));
    wchar_t value[32]{};
    swprintf_s(value,L"%d",g_layout->defaultBoxWidth);SetWindowTextW(g_defaultWidth,value);
    const UINT height=GetDlgItemInt(hwnd,kDefaultHeight,&valid,FALSE);
    if(valid)g_layout->defaultBoxHeight=static_cast<int>(std::clamp(height,160u,800u));
    swprintf_s(value,L"%d",g_layout->defaultBoxHeight);SetWindowTextW(g_defaultHeight,value);
    SaveLayout(*g_layout);
}
void Fill(HDC dc,const RECT& rect,COLORREF color) {
    SetDCBrushColor(dc,color);
    FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
void LayoutSettings(HWND hwnd) {
    RECT client{};GetClientRect(hwnd,&client);
    if(client.right<=0||client.bottom<=0)return;
    const int width=max(330L,client.right-kContentLeft-28);
    const int extraHeight=std::clamp(static_cast<int>(client.bottom)-650,0,210);
    for(const auto& control:g_contentControls) {
        const RECT& r=control.bounds;
        const int id=GetDlgCtrlID(control.window);
        const int x=kContentLeft+MulDiv(r.left-170,width,330);
        int y=r.top+8,height=r.bottom-r.top;
        int size=MulDiv(r.right-r.left,width,330);
        wchar_t cls[32]{};GetClassNameW(control.window,cls,32);
        if(_wcsicmp(cls,L"ComboBox")==0)height=200;
        if(id==kBackupList||id==kAutoRules)height+=extraHeight;
        if((id>=kBackupCreate&&id<=kBackupDelete)||id==kAutoDelete)y+=extraHeight;
        if(id==kNoteList||id==kNotePreview)height=max(160L,client.bottom-y-126);
        if(id==kWidgetClear||id==kWidgetReopen||id==kNoteSummary)y=client.bottom-109;
        SetWindowPos(control.window,nullptr,x,y,size,height,SWP_NOZORDER|SWP_NOACTIVATE);
    }
    if(g_autoRules) {
        RECT list{};GetClientRect(g_autoRules,&list);
        const int w=max(120L,list.right-GetSystemMetrics(SM_CXVSCROLL));
        ListView_SetColumnWidth(g_autoRules,0,w*25/100);
        ListView_SetColumnWidth(g_autoRules,1,w*49/100);
        ListView_SetColumnWidth(g_autoRules,2,w-w*25/100-w*49/100);
    }
    if(HWND notes=GetDlgItem(hwnd,kNoteList)) {
        RECT list{};GetClientRect(notes,&list);
        const int listWidth=max(100L,list.right-GetSystemMetrics(SM_CXVSCROLL));
        ListView_SetColumnWidth(notes,0,max(48,listWidth-62));
        ListView_SetColumnWidth(notes,1,62);
    }
    if(g_close)SetWindowPos(g_close,nullptr,client.right-116,client.bottom-42,88,30,SWP_NOZORDER|SWP_NOACTIVATE);
    if(g_sidebarAbout)SetWindowPos(g_sidebarAbout,nullptr,12,client.bottom-46,128,34,SWP_NOZORDER|SWP_NOACTIVATE);
    InvalidateRect(hwnd,nullptr,FALSE);
}

void RefreshSettingsValues(HWND hwnd) {
    UpdateValue();UpdateCornerValue();
    SendDlgItemMessageW(hwnd,kSlider,TBM_SETPOS,TRUE,g_layout->opacity);
    SendDlgItemMessageW(hwnd,kCornerSlider,TBM_SETPOS,TRUE,g_layout->cornerRadius);
    SendDlgItemMessageW(hwnd,kAutoEnable,BM_SETCHECK,g_layout->autoOrganize?BST_CHECKED:BST_UNCHECKED,0);
    SetWindowTextW(g_defaultWidth,std::to_wstring(g_layout->defaultBoxWidth).c_str());
    SetWindowTextW(g_defaultHeight,std::to_wstring(g_layout->defaultBoxHeight).c_str());
    SendMessageW(g_fontFamily,CB_SELECTSTRING,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(g_layout->fontFamily.c_str()));
    RefreshBoxChoices();
    RefreshRuleRows();
    RedrawWindow(hwnd,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN);
}
bool SaveSettings(HWND hwnd) {
    if(SaveLayout(*g_layout))return true;
    MessageBoxW(hwnd,L"无法保存设置，请检查配置目录是否可写。",L"设置中心",MB_OK|MB_ICONWARNING);
    return false;
}
void ApplyPreset(HWND hwnd,int index) {
    auto& boxes=g_layout->boxes;
    auto box=std::find_if(boxes.begin(),boxes.end(),[&](const Box& item){return item.title==kPresetTitles[index];});
    if(box==boxes.end()) {
        Box created;
        created.id=L"preset-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(index);
        created.title=kPresetTitles[index];
        const LONG origin=120+index*35;
        created.rect={origin,origin,origin+g_layout->defaultBoxWidth,origin+g_layout->defaultBoxHeight};
        created.color=g_layout->defaultBoxColor;
        boxes.push_back(std::move(created));
        box=std::prev(boxes.end());
    }
    const Layout::AutoRule preset{box->id,index==0,kPresetExtensions[index]};
    auto& rules=g_layout->autoRules;
    auto rule=std::find_if(rules.begin(),rules.end(),[&](const auto& item){
        return item.boxId==preset.boxId&&item.folders==preset.folders&&item.extensions==preset.extensions;
    });
    if(rule==rules.end())rules.push_back(preset);
    else rule->enabled=true;
    const int target=static_cast<int>(box-boxes.begin());
    for(const auto& item:EnumerateDesktopItems()) {
        if(!MatchesAutoRule(preset,item.directory,item.type))continue;
        const bool alreadyOrganized=std::any_of(boxes.begin(),boxes.end(),[&](const Box& owner){
            return std::any_of(owner.items.begin(),owner.items.end(),[&](const auto& path){return _wcsicmp(path.c_str(),item.path.c_str())==0;});
        });
        if(!alreadyOrganized)AssignDesktopItem(*g_layout,item.path,target,{});
    }
    HandleCanvasCommand(CanvasCommand::RefreshLayout);
    RefreshBoxChoices();
    RefreshRuleRows();
    SaveSettings(hwnd);
}

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_ACTIVATE:
        if(g_layout&&g_page==4&&LOWORD(wParam)!=WA_INACTIVE)RefreshNotes(hwnd);
        return DefWindowProcW(hwnd,message,wParam,lParam);
    case WM_GETMINMAXINFO: {
        auto* info=reinterpret_cast<MINMAXINFO*>(lParam);
        info->ptMinTrackSize={620,698};
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};HDC dc=BeginPaint(hwnd,&paint);RECT client{};GetClientRect(hwnd,&client);
        Fill(dc,client,kPaper);
        Fill(dc,{0,0,kNavWidth,client.bottom},kNav);
        Fill(dc,{kNavWidth,0,kNavWidth+1,client.bottom},kLine);
        Fill(dc,{kContentLeft,56,client.right-28,57},kLine);
        Fill(dc,{kNavWidth+1,client.bottom-52,client.right,client.bottom-51},kLine);
        EndPaint(hwnd,&paint);return 0;
    }
    case WM_SIZE: {
        LayoutSettings(hwnd);
        return 0;
    }
    case WM_CREATE: {
        HFONT font = g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        g_headingFont=CreateFontW(-19,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
        HWND sidebar = CreateWindowW(L"STATIC", L"nestlone-D", WS_CHILD | WS_VISIBLE, 20, 24, 120, 28, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(sidebar, WM_SETFONT, reinterpret_cast<WPARAM>(g_headingFont), TRUE);
        HWND theme = CreateWindowW(L"BUTTON", L"主题设置", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 12, 60, 116, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarTheme)), nullptr, nullptr);
        SendMessageW(theme, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND automatic = CreateWindowW(L"BUTTON", L"自动分类", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 12, 100, 116, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarAuto)), nullptr, nullptr);
        SendMessageW(automatic, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND backup=CreateWindowW(L"BUTTON",L"备份与还原",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,12,140,116,34,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarBackup)),nullptr,nullptr);SendMessageW(backup,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        HWND widgets=CreateWindowW(L"BUTTON",L"便签",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,12,180,116,34,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarWidgets)),nullptr,nullptr);SendMessageW(widgets,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
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
        for(int i=0;i<4;++i){HWND preset=CreateWindowW(L"BUTTON",kPresetTitles[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,170+i*74,168,68,28,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDefaultBoxBase+i)),nullptr,nullptr);g_autoPage.push_back(preset);}
        HWND ruleLabel=CreateWindowW(L"STATIC",L"自定义规则",WS_CHILD|WS_VISIBLE,170,218,150,24,hwnd,nullptr,nullptr,nullptr);SendMessageW(ruleLabel,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_autoPage.push_back(ruleLabel);
        g_autoRules=CreateWindowW(WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|WS_BORDER|LVS_REPORT|LVS_SHOWSELALWAYS|LVS_SINGLESEL,170,246,290,178,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoRules)),nullptr,nullptr);
        ListView_SetExtendedListViewStyle(g_autoRules,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(g_autoRules,kPaper);ListView_SetTextBkColor(g_autoRules,kPaper);ListView_SetTextColor(g_autoRules,kInk);
        const wchar_t* headers[]={L"启用",L"匹配条件",L"目标盒子"};
        for(int i=0;i<3;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=90;column.pszText=const_cast<wchar_t*>(headers[i]);ListView_InsertColumn(g_autoRules,i,&column);}
        g_autoPage.push_back(g_autoRules);
        g_autoPage.push_back(CreateWindowW(L"BUTTON",L"删除所选规则",WS_CHILD|BS_OWNERDRAW,170,440,106,30,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoDelete)),nullptr,nullptr));
        HWND backupTitle=CreateWindowW(L"STATIC",L"桌面布局备份",WS_CHILD|WS_VISIBLE,170,20,180,24,hwnd,nullptr,nullptr,nullptr);SendMessageW(backupTitle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_backupPage.push_back(backupTitle);
        HWND backupHint=CreateWindowW(L"STATIC",L"每份备份保存当前盒子、分组、规则与桌面位置。",WS_CHILD|WS_VISIBLE,170,52,290,22,hwnd,nullptr,nullptr,nullptr);SendMessageW(backupHint,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_backupPage.push_back(backupHint);
        g_backupList=CreateWindowW(L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL,170,84,290,270,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBackupList)),nullptr,nullptr);g_backupPage.push_back(g_backupList);
        const int backupIds[]={kBackupCreate,kBackupApply,kBackupDelete};const wchar_t* backupLabels[]={L"立即备份",L"应用备份",L"删除备份"};for(int i=0;i<3;++i){HWND action=CreateWindowW(L"BUTTON",backupLabels[i],WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,170+i*98,370,90,28,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(backupIds[i])),nullptr,nullptr);SendMessageW(action,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_backupPage.push_back(action);}
        HWND aboutTitle=CreateWindowW(L"STATIC",L"关于 nestlone-D",WS_CHILD|WS_VISIBLE,170,20,260,30,hwnd,nullptr,nullptr,nullptr);SendMessageW(aboutTitle,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_aboutPage.push_back(aboutTitle);
        HWND aboutVersion=CreateWindowW(L"STATIC",L"nestlone-D · Windows 桌面整理工具",WS_CHILD|WS_VISIBLE,170,62,290,24,hwnd,nullptr,nullptr,nullptr);SendMessageW(aboutVersion,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_aboutPage.push_back(aboutVersion);
        HWND aboutDescription=CreateWindowW(L"STATIC",L"将桌面图标按盒子、分组与规则进行收纳。\r\n保留原生文件、快捷方式与桌面操作体验。",WS_CHILD|WS_VISIBLE,170,102,300,56,hwnd,nullptr,nullptr,nullptr);SendMessageW(aboutDescription,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_aboutPage.push_back(aboutDescription);
        HWND repository=CreateWindowW(L"BUTTON",L"GitHub",WS_CHILD|WS_VISIBLE|BS_OWNERDRAW,170,188,118,48,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAboutRepository)),nullptr,nullptr);g_aboutPage.push_back(repository);
        const auto addNoteControl=[&](const wchar_t* cls,const wchar_t* caption,DWORD style,int id,int x,int y,int w,int h) {
            const HWND control=CreateWindowW(cls,caption,WS_CHILD|style,x,y,w,h,hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),nullptr,nullptr);
            g_widgetsPage.push_back(control);
            return control;
        };
        addNoteControl(L"STATIC",L"便签",0,0,170,20,180,28);
        addNoteControl(L"BUTTON",L"+  新建便签",BS_OWNERDRAW,kWidgetNote,170,67,90,30);
        addNoteControl(L"STATIC",L"搜索",SS_CENTERIMAGE,0,274,67,39,30);
        const HWND search=addNoteControl(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL,kNoteSearch,313,67,187,30);
        SendMessageW(search,EM_SETLIMITTEXT,180,0);
        const HWND noteList=addNoteControl(WC_LISTVIEWW,L"",WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,
            kNoteList,170,109,150,385);
        ListView_SetExtendedListViewStyle(noteList,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        ListView_SetBkColor(noteList,kPaper);
        ListView_SetTextBkColor(noteList,kPaper);
        ListView_SetTextColor(noteList,kInk);
        for(int i=0;i<2;++i) {
            LVCOLUMNW column{};
            column.mask=LVCF_TEXT|LVCF_WIDTH;
            column.cx=75;
            column.pszText=const_cast<wchar_t*>(i==0?L"内容":L"状态");
            ListView_InsertColumn(noteList,i,&column);
        }
        addNoteControl(L"EDIT",L"",WS_BORDER|WS_VSCROLL|ES_MULTILINE|ES_AUTOVSCROLL|ES_READONLY,
            kNotePreview,330,109,170,385);
        addNoteControl(L"STATIC",L"",SS_CENTERIMAGE,kNoteSummary,170,512,108,32);
        addNoteControl(L"BUTTON",L"显示到桌面",BS_OWNERDRAW,kWidgetReopen,286,512,106,32);
        addNoteControl(L"BUTTON",L"清除全部便签",BS_OWNERDRAW,kWidgetClear,400,512,100,32);
        for(auto* page:{&g_themePage,&g_autoPage,&g_backupPage,&g_aboutPage,&g_widgetsPage}) {
            for(HWND control:*page) {
                RECT r{};GetWindowRect(control,&r);
                MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&r),2);
                g_contentControls.push_back({control,r});
                SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(control==page->front()?g_headingFont:font),TRUE);
                wchar_t cls[32]{};GetClassNameW(control,cls,32);
                if(_wcsicmp(cls,L"Static")!=0)SetWindowLongPtrW(control,GWL_STYLE,GetWindowLongPtrW(control,GWL_STYLE)|WS_TABSTOP);
            }
        }
        for(int id=kSidebarTheme;id<=kSidebarWidgets;++id) {
            HWND button=GetDlgItem(hwnd,id);
            SetWindowLongPtrW(button,GWL_STYLE,GetWindowLongPtrW(button,GWL_STYLE)|WS_TABSTOP);
        }
        LayoutSettings(hwnd);
        RefreshBackups(hwnd);
        // All page controls must exist before the first visibility pass.
        SelectPage(hwnd,0);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        HDC dc=reinterpret_cast<HDC>(wParam);
        const HWND control=reinterpret_cast<HWND>(lParam);
        SetTextColor(dc,IsWindowEnabled(control)?kInk:kMuted);
        RECT bounds{};GetWindowRect(control,&bounds);MapWindowPoints(nullptr,hwnd,reinterpret_cast<POINT*>(&bounds),2);
        COLORREF background=message==WM_CTLCOLORSTATIC&&bounds.left<kNavWidth?kNav:kPaper;
        SetBkColor(dc,background);SetDCBrushColor(dc,background);
        return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) && GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == kSlider) {
            g_layout->opacity = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), TBM_GETPOS, 0, 0));
            UpdateValue();
            CanvasSetOpacity(g_layout->opacity);
            if(LOWORD(wParam)!=TB_THUMBTRACK)SaveSettings(hwnd);
        }
        if (reinterpret_cast<HWND>(lParam) && GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == kCornerSlider) {
            g_layout->cornerRadius=static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam),TBM_GETPOS,0,0));
            UpdateCornerValue();
            CanvasSetOpacity(g_layout->opacity);
            if(LOWORD(wParam)!=TB_THUMBTRACK)SaveSettings(hwnd);
        }
        return 0;
    case WM_DRAWITEM: {
        const auto& draw=*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if(draw.CtlType!=ODT_BUTTON)return FALSE;
        const UINT id=draw.CtlID;
        const bool navigation=id>=kSidebarTheme&&id<=kSidebarWidgets;
        const bool selected=navigation&&g_page==static_cast<int>(id-kSidebarTheme);
        const bool pressed=(draw.itemState&ODS_SELECTED)!=0;
        const bool disabled=(draw.itemState&ODS_DISABLED)!=0;
        const bool primary=selected||id==IDCANCEL||id==kWidgetNote;
        const RECT bounds=draw.rcItem;
        const HDC dc=draw.hDC;
        const COLORREF fill=disabled?RGB(246,246,246):primary?(pressed?RGB(62,62,62):kInk):
            pressed?RGB(224,224,224):navigation?kNav:kPaper;
        Fill(dc,bounds,fill);
        SetDCBrushColor(dc,primary?kInk:navigation?fill:kLine);
        FrameRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

        const bool swatch=id>=kColorBase&&id<kColorBase+6;
        const bool defaultSwatch=id>=kDefaultColorBase&&id<kDefaultColorBase+6;
        if(swatch||defaultSwatch) {
            const auto* box=swatch?SelectedColorBox():nullptr;
            const COLORREF color=kColors[id-(swatch?kColorBase:kDefaultColorBase)];
            const bool active=swatch?(box&&box->color==color):g_layout->defaultBoxColor==color;
            RECT chip=bounds;InflateRect(&chip,-3,-3);
            Fill(dc,chip,disabled?kNav:color);
            if(active&&!disabled) {
                SetDCBrushColor(dc,kInk);
                FrameRect(dc,&bounds,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
                SetBkMode(dc,TRANSPARENT);SetTextColor(dc,kInk);
                DrawTextW(dc,L"✓",-1,&chip,DT_SINGLELINE|DT_CENTER|DT_VCENTER);
            }
        } else {
            const auto previous=SelectObject(dc,g_font);
            SetBkMode(dc,TRANSPARENT);
            SetTextColor(dc,disabled?RGB(140,140,140):primary?kPaper:kInk);
            wchar_t caption[128]{};
            GetWindowTextW(draw.hwndItem,caption,128);
            RECT label=bounds;
            if(navigation)label.left+=14;
            bool drewLogo=false;
            if(id==kAboutRepository)if(auto* logo=GitHubLogo()) {
                Gdiplus::Graphics graphics(dc);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                const int width=min(126L,bounds.right-bounds.left-12);
                const int height=width*39/168;
                graphics.DrawImage(logo,(bounds.left+bounds.right-width)/2,(bounds.top+bounds.bottom-height)/2,width,height);
                drewLogo=true;
            }
            if(!drewLogo)DrawTextW(dc,caption,-1,&label,DT_SINGLELINE|DT_VCENTER|(navigation?DT_LEFT:DT_CENTER));
            SelectObject(dc,previous);
        }
        if(draw.itemState&ODS_FOCUS) {
            RECT focus=bounds;InflateRect(&focus,-3,-3);
            SetTextColor(dc,primary?kPaper:kInk);
            SetBkColor(dc,fill);
            DrawFocusRect(dc,&focus);
        }
        return TRUE;
    }
    case WM_NOTIFY: {
        const auto* header=reinterpret_cast<const NMHDR*>(lParam);
        if(g_layout&&header->idFrom==kNoteList&&header->code==LVN_ITEMCHANGED&&!g_refreshingNotes) {
            const auto* change=reinterpret_cast<const NMLISTVIEW*>(lParam);
            if((change->uChanged&LVIF_STATE)&&((change->uNewState^change->uOldState)&LVIS_SELECTED))
                UpdateNoteSelection(hwnd);
            return 0;
        }
        if(g_layout&&header->idFrom==kAutoRules&&header->code==LVN_ITEMCHANGED&&!g_updatingRules) {
            const auto* change=reinterpret_cast<const NMLISTVIEW*>(lParam);
            const int row=change->iItem;
            const UINT state=change->uNewState&LVIS_STATEIMAGEMASK;
            if(row>=0&&static_cast<size_t>(row)<g_layout->autoRules.size()&&
                (change->uChanged&LVIF_STATE)&&state!=0&&state!=(change->uOldState&LVIS_STATEIMAGEMASK)) {
                g_layout->autoRules[row].enabled=state==INDEXTOSTATEIMAGEMASK(2);
                SaveSettings(hwnd);
            }
            EnableWindow(GetDlgItem(hwnd,kAutoDelete),ListView_GetNextItem(g_autoRules,-1,LVNI_SELECTED)>=0);
        }
        return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wParam)>=kSidebarTheme&&LOWORD(wParam)<=kSidebarWidgets&&HIWORD(wParam)==BN_CLICKED) {
            SelectPage(hwnd,LOWORD(wParam)-kSidebarTheme);
            return 0;
        }
        if(LOWORD(wParam)==kNoteSearch&&HIWORD(wParam)==EN_CHANGE) {
            RefreshNotes(hwnd);
            return 0;
        }
        if(LOWORD(wParam)==kWidgetNote&&HIWORD(wParam)==BN_CLICKED) {
            if(!CanvasCreateNote(hwnd)) {
                const wchar_t* feedback=CanvasVisible()?L"暂时无法在桌面上创建便签，请稍后重试。":
                    L"桌面组件已隐藏。请先通过托盘显示桌面盒子，再新建便签。";
                MessageBoxW(hwnd,feedback,L"新建便签",MB_OK|MB_ICONINFORMATION);
            } else {
                SetDlgItemTextW(hwnd,kNoteSearch,L"");
                RefreshNotes(hwnd,g_layout->widgets.back().id);
            }
            return 0;
        }
        if(LOWORD(wParam)==kWidgetReopen&&HIWORD(wParam)==BN_CLICKED) {
            const auto id=SelectedNoteId(hwnd);
            if(id.empty())return 0;
            if(!CanvasShowNote(id)) {
                const wchar_t* feedback=CanvasVisible()?L"暂时无法显示这张便签。":L"桌面组件已隐藏，请先从托盘恢复显示。";
                MessageBoxW(hwnd,feedback,L"显示便签",MB_OK|MB_ICONINFORMATION);
            }
            RefreshNotes(hwnd,id);
            return 0;
        }
        if(LOWORD(wParam)==kWidgetClear) {
            if(!g_layout->widgets.empty()&&MessageBoxW(hwnd,L"清除所有便签？便签内容也会被清除。",L"便签",
                MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)==IDYES) {
                HandleCanvasCommand(CanvasCommand::ClearWidgets);
                RefreshNotes(hwnd);
            }
            return 0;
        }
        if(LOWORD(wParam)==kAboutRepository){ShellExecuteW(hwnd,L"open",L"https://github.com/nestlone/nestlone-Desktop",nullptr,nullptr,SW_SHOWNORMAL);return 0;}
        if(LOWORD(wParam)==kStartup) {
            const bool enabled=SendMessageW(reinterpret_cast<HWND>(lParam),BM_GETCHECK,0,0)==BST_CHECKED;
            if(!SetStartupEnabled(enabled)) {
                SendMessageW(reinterpret_cast<HWND>(lParam),BM_SETCHECK,StartupEnabled()?BST_CHECKED:BST_UNCHECKED,0);
                MessageBoxW(hwnd,L"无法更新开机自启动设置。",L"nestlone-D",MB_OK|MB_ICONWARNING);
            }
            return 0;
        }
        if(LOWORD(wParam)==kBackupList&&HIWORD(wParam)==LBN_SELCHANGE){UpdateBackupActions(hwnd);return 0;}
        if(LOWORD(wParam)==kBackupCreate) {
            const auto directory=BackupDirectory();
            std::error_code error;
            if(!directory.empty())std::filesystem::create_directories(directory,error);
            if(directory.empty()||error) {
                MessageBoxW(hwnd,L"无法创建备份目录。",L"备份与还原",MB_OK|MB_ICONWARNING);
            } else if(SaveSettings(hwnd)) {
                const auto target=directory/(BackupStamp()+L".backup");
                if(!CopyFileW(LayoutPath().c_str(),target.c_str(),TRUE))
                    MessageBoxW(hwnd,L"未能创建布局备份。",L"备份与还原",MB_OK|MB_ICONWARNING);
                RefreshBackups(hwnd);
            }
            return 0;
        }
        if(LOWORD(wParam)==kBackupApply||LOWORD(wParam)==kBackupDelete) {
            const LRESULT index=SendMessageW(g_backupList,LB_GETCURSEL,0,0);
            if(index==LB_ERR)return 0;
            const LRESULT length=SendMessageW(g_backupList,LB_GETTEXTLEN,index,0);
            if(length==LB_ERR)return 0;
            std::wstring name(static_cast<size_t>(length)+1,L'\0');
            if(SendMessageW(g_backupList,LB_GETTEXT,index,reinterpret_cast<LPARAM>(name.data()))==LB_ERR)return 0;
            name.resize(static_cast<size_t>(length));
            const auto path=BackupDirectory()/name;
            const bool apply=LOWORD(wParam)==kBackupApply;
            if(MessageBoxW(hwnd,apply?L"使用所选备份替换当前布局及配置？":L"删除所选备份？",
                L"备份与还原",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return 0;
            if(apply) {
                if(CopyFileW(path.c_str(),LayoutPath().c_str(),FALSE)) {
                    HandleCanvasCommand(CanvasCommand::Reload);
                    RefreshSettingsValues(hwnd);
                } else MessageBoxW(hwnd,L"未能应用所选备份。",L"备份与还原",MB_OK|MB_ICONWARNING);
            } else {
                if(!DeleteFileW(path.c_str()))MessageBoxW(hwnd,L"未能删除所选备份。",L"备份与还原",MB_OK|MB_ICONWARNING);
                RefreshBackups(hwnd);
            }
            return 0;
        }
        if(LOWORD(wParam)==kAutoEnable&&HIWORD(wParam)==BN_CLICKED) {
            g_layout->autoOrganize=IsDlgButtonChecked(hwnd,kAutoEnable)==BST_CHECKED;
            SaveSettings(hwnd);
            return 0;
        }
        if(LOWORD(wParam)==kAutoAdd) {
            const auto* box=SelectedBox(g_autoBox);
            if(!box)return 0;
            wchar_t extensions[512]{};
            GetWindowTextW(g_autoExtensions,extensions,512);
            const bool folders=IsDlgButtonChecked(hwnd,kAutoFolders)==BST_CHECKED;
            if(!folders&&std::wstring_view(extensions).find_first_not_of(L";, \t\r\n")==std::wstring_view::npos) {
                MessageBoxW(hwnd,L"请填写文件后缀，或勾选匹配文件夹。",L"自动分类",MB_OK|MB_ICONINFORMATION);
                return 0;
            }
            auto& rules=g_layout->autoRules;
            const auto existing=std::find_if(rules.begin(),rules.end(),[&](const auto& rule){
                return rule.boxId==box->id&&rule.folders==folders&&_wcsicmp(rule.extensions.c_str(),extensions)==0;
            });
            if(existing==rules.end())rules.push_back({box->id,folders,extensions});
            else existing->enabled=true;
            RefreshRuleRows();
            SaveSettings(hwnd);
            return 0;
        }
        if(LOWORD(wParam)==kAutoDelete) {
            const int row=ListView_GetNextItem(g_autoRules,-1,LVNI_SELECTED);
            auto& rules=g_layout->autoRules;
            if(row>=0&&static_cast<size_t>(row)<rules.size()) {
                rules.erase(rules.begin()+row);
                RefreshRuleRows();
                SaveSettings(hwnd);
            }
            return 0;
        }
        if(LOWORD(wParam)>=kDefaultBoxBase&&LOWORD(wParam)<kDefaultBoxBase+4) {
            ApplyPreset(hwnd,LOWORD(wParam)-kDefaultBoxBase);
            return 0;
        }
        if(LOWORD(wParam)==kBoxSelect&&HIWORD(wParam)==CBN_SELCHANGE) {
            for(int i=0;i<6;++i)InvalidateRect(GetDlgItem(hwnd,kColorBase+i),nullptr,FALSE);
            return 0;
        }
        if(LOWORD(wParam)==kFontFamily&&HIWORD(wParam)==CBN_SELCHANGE) {
            wchar_t family[64]{};
            GetWindowTextW(g_fontFamily,family,64);
            if(*family) {
                g_layout->fontFamily=family;
                CanvasSetOpacity(g_layout->opacity);
                SaveSettings(hwnd);
            }
            return 0;
        }
        if((LOWORD(wParam)==kDefaultWidth||LOWORD(wParam)==kDefaultHeight)&&HIWORD(wParam)==EN_KILLFOCUS){UpdateDefaultBoxMetrics();return 0;}
        if(LOWORD(wParam)>=kDefaultColorBase&&LOWORD(wParam)<kDefaultColorBase+6) {
            g_layout->defaultBoxColor=kColors[LOWORD(wParam)-kDefaultColorBase];
            for(int i=0;i<6;++i)InvalidateRect(GetDlgItem(hwnd,kDefaultColorBase+i),nullptr,FALSE);
            SaveSettings(hwnd);
            return 0;
        }
        if(LOWORD(wParam)>=kColorBase&&LOWORD(wParam)<kColorBase+6) {
            if(auto* box=SelectedColorBox()) {
                box->color=kColors[LOWORD(wParam)-kColorBase];
                for(auto& member:g_layout->boxes)if(member.groupId==box->id)member.color=box->color;
                for(int i=0;i<6;++i)InvalidateRect(GetDlgItem(hwnd,kColorBase+i),nullptr,FALSE);
                CanvasSetOpacity(g_layout->opacity);
                SaveSettings(hwnd);
            }
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        g_contentControls.clear();g_boxIds.clear();g_noteRows.clear();
        g_githubLogo.reset();g_githubLogoStream.Reset();
        if(g_headingFont){DeleteObject(g_headingFont);g_headingFont=nullptr;}
        if(g_font){DeleteObject(g_font);g_font=nullptr;}
        g_window=nullptr;g_close=nullptr;g_sidebarAbout=nullptr;g_value=nullptr;g_cornerValue=nullptr;
        g_defaultWidth=nullptr;g_defaultHeight=nullptr;g_fontFamily=nullptr;g_boxSelect=nullptr;g_autoBox=nullptr;
        g_autoExtensions=nullptr;g_autoRules=nullptr;g_backupList=nullptr;g_layout=nullptr;
        g_themePage.clear();g_autoPage.clear();g_backupPage.clear();g_aboutPage.clear();g_widgetsPage.clear();
        return 0;
    default: return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}
}

bool ProcessSettingsMessage(MSG& message) {
    return g_window&&(message.hwnd==g_window||IsChild(g_window,message.hwnd))&&IsDialogMessageW(g_window,&message);
}
void CloseSettings() {
    if(g_window)DestroyWindow(g_window);
}
void ShowSettings(HINSTANCE instance, HWND owner, Layout* layout) {
    g_layout = layout;
    if (g_window) { ShowWindow(g_window, SW_SHOWNORMAL); SetForegroundWindow(g_window); return; }
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES|ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&common);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = SettingsProc;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_NESTLONE));
    if (!g_surface) g_surface=CreateSolidBrush(kPaper);
    wc.hbrBackground = g_surface;
    RegisterClassExW(&wc);
    // An owned settings window keeps the tray workflow and uses a full-size
    // system caption. The tool-window style would shrink its close button.
    g_window = CreateWindowExW(0, kClassName, L"nestlone-D 设置中心",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MAXIMIZEBOX|WS_CLIPCHILDREN,
        CW_USEDEFAULT,CW_USEDEFAULT,740,728,owner,nullptr,instance,nullptr);
    if (!g_window) return;
    ShowWindow(g_window, SW_SHOWNORMAL);
    UpdateWindow(g_window);
}

}

#include "Settings.h"
#include "DesktopCanvas.h"
#include "DesktopItems.h"
#include "Theme.h"
#include <commctrl.h>
#include <vector>

namespace nestlone {
namespace {
constexpr wchar_t kClassName[] = L"nestlone-D.Settings";
constexpr int kSlider = 2001;
constexpr int kValue = 2002;
constexpr int kBoxSelect = 2100;
constexpr int kColorBase = 2110;
constexpr int kSidebarTheme = 2201;
constexpr int kSidebarAuto = 2202;
constexpr int kAutoEnable = 2301;
constexpr int kAutoFolders = 2302;
constexpr int kAutoExtensions = 2303;
constexpr int kAutoBox = 2304;
constexpr int kAutoAdd = 2305;
constexpr int kAutoRules = 2306;
constexpr int kDefaultBoxBase = 2310;
HFONT g_font = nullptr;
HBRUSH g_surface = nullptr;
HWND g_window = nullptr;
HWND g_value = nullptr;
HWND g_boxSelect = nullptr;
HWND g_autoBox = nullptr;
HWND g_autoExtensions = nullptr;
HWND g_autoRules = nullptr;
Layout* g_layout = nullptr;
std::vector<HWND> g_themePage,g_autoPage;
void SelectPage(bool automatic) {
    for(HWND control:g_themePage)ShowWindow(control,automatic?SW_HIDE:SW_SHOW);
    for(HWND control:g_autoPage)ShowWindow(control,automatic?SW_SHOW:SW_HIDE);
}
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

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE: {
        HFONT font = g_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        HWND sidebar = CreateWindowW(L"STATIC", L"nestlone-D", WS_CHILD | WS_VISIBLE, 20, 24, 120, 28, hwnd, nullptr, nullptr, nullptr);
        SendMessageW(sidebar, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND theme = CreateWindowW(L"BUTTON", L"主题设置", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 12, 60, 116, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarTheme)), nullptr, nullptr);
        SendMessageW(theme, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND automatic = CreateWindowW(L"BUTTON", L"自动分类", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 12, 100, 116, 34, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSidebarAuto)), nullptr, nullptr);
        SendMessageW(automatic, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
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
        SendMessageW(slider, TBM_SETRANGE, TRUE, MAKELONG(20, 100));
        SendMessageW(slider, TBM_SETPOS, TRUE, g_layout ? g_layout->opacity : 88);
        SendMessageW(slider, TBM_SETPAGESIZE, 0, 5);
        SendMessageW(slider, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        HWND boxLabel = CreateWindowW(L"STATIC", L"当前盒子", WS_CHILD | WS_VISIBLE, 170, 150, 90, 24, hwnd, nullptr, nullptr, nullptr);
        g_themePage.push_back(boxLabel);
        SendMessageW(boxLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_boxSelect = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 260, 146, 200, 180, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBoxSelect)), nullptr, nullptr);
        g_themePage.push_back(g_boxSelect);
        for (const Box& box : g_layout->boxes) SendMessageW(g_boxSelect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(box.title.c_str()));
        if (!g_layout->boxes.empty()) SendMessageW(g_boxSelect, CB_SETCURSEL, 0, 0);
        HWND colorLabel = CreateWindowW(L"STATIC", L"盒子背景色", WS_CHILD | WS_VISIBLE, 170, 188, 100, 24, hwnd, nullptr, nullptr, nullptr);
        g_themePage.push_back(colorLabel);
        SendMessageW(colorLabel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        for (int i = 0; i < 6; ++i) {
            HWND swatch = CreateWindowW(L"BUTTON", L"  ", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 280 + i * 30, 184, 24, 24, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kColorBase + i)), nullptr, nullptr);
            g_themePage.push_back(swatch);
            SendMessageW(swatch, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
        HWND close = CreateWindowW(L"BUTTON", L"完成", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 368, 430, 92, 30, hwnd, reinterpret_cast<HMENU>(IDCANCEL), nullptr, nullptr);
        SendMessageW(close, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        SendMessageW(g_boxSelect, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        g_selectedBox = 0;
        wchar_t value[32]{};
        swprintf_s(value,L"%d%%",g_layout->opacity);
        SetWindowTextW(g_value,value);
        HWND note=CreateWindowW(L"STATIC",L"即时预览 · 自动保存 · 图标与文字保持清晰",
            WS_CHILD|WS_VISIBLE,170,270,330,24,hwnd,nullptr,nullptr,nullptr);
        g_themePage.push_back(note);
        SendMessageW(note,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
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
        g_autoPage.push_back(add);SelectPage(false);
        const wchar_t* defaults[]={L"目录",L"文档",L"图片",L"压缩包"};
        for(int i=0;i<4;++i){HWND preset=CreateWindowW(L"BUTTON",defaults[i],WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,170+(i%2)*100,160+(i/2)*26,92,22,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDefaultBoxBase+i)),nullptr,nullptr);SendMessageW(preset,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_autoPage.push_back(preset);}
        HWND ruleLabel=CreateWindowW(L"STATIC",L"自定义规则",WS_CHILD|WS_VISIBLE,170,218,150,24,hwnd,nullptr,nullptr,nullptr);SendMessageW(ruleLabel,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);g_autoPage.push_back(ruleLabel);
        g_autoRules=CreateWindowExW(WS_EX_CLIENTEDGE,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT|LVS_SHOWSELALWAYS,170,246,290,142,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kAutoRules)),nullptr,nullptr);
        ListView_SetExtendedListViewStyle(g_autoRules,LVS_EX_CHECKBOXES|LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
        const wchar_t* headers[]={L"标题",L"文件名",L"后缀",L"盒子"};const int widths[]={68,54,104,60};
        for(int i=0;i<4;++i){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.cx=widths[i];column.pszText=const_cast<wchar_t*>(headers[i]);ListView_InsertColumn(g_autoRules,i,&column);}
        for(const auto& rule:g_layout->autoRules)AddAutoRuleRow(rule);g_autoPage.push_back(g_autoRules);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC dc=reinterpret_cast<HDC>(wParam);
        SetTextColor(dc,theme::title);
        SetBkColor(dc,RGB(246,250,252));
        return reinterpret_cast<LRESULT>(g_surface);
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) && GetDlgCtrlID(reinterpret_cast<HWND>(lParam)) == kSlider) {
            g_layout->opacity = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), TBM_GETPOS, 0, 0));
            UpdateValue();
        }
        return 0;
    case WM_DRAWITEM: {
        const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
        if (draw && (draw->CtlID == kSidebarTheme || draw->CtlID == kSidebarAuto || draw->CtlID == kAutoAdd || draw->CtlID == IDCANCEL)) {
            HBRUSH brush=CreateSolidBrush(RGB(217,242,245));
            HPEN pen=CreatePen(PS_SOLID,1,RGB(190,224,230));
            auto ob=SelectObject(draw->hDC,brush); auto op=SelectObject(draw->hDC,pen);
            const RECT& r=draw->rcItem;
            RoundRect(draw->hDC,r.left,r.top,r.right,r.bottom,12,12);
            SelectObject(draw->hDC,ob); SelectObject(draw->hDC,op);
            DeleteObject(brush); DeleteObject(pen);
            auto of=SelectObject(draw->hDC,g_font);
            SetBkMode(draw->hDC,TRANSPARENT); SetTextColor(draw->hDC,theme::title);
            const wchar_t* caption=draw->CtlID==IDCANCEL?L"完成":draw->CtlID==kSidebarAuto?L"自动分类":draw->CtlID==kAutoAdd?L"添加规则":L"主题设置";
            RECT label=r; DrawTextW(draw->hDC,caption,-1,&label,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            if(draw->itemState & ODS_FOCUS) { InflateRect(&label,-3,-3); DrawFocusRect(draw->hDC,&label); }
            SelectObject(draw->hDC,of);
            return TRUE;
        }
        if (draw && draw->CtlID >= kColorBase && draw->CtlID < kColorBase + 6) {
            HBRUSH brush = CreateSolidBrush(kColors[draw->CtlID - kColorBase]);
            FillRect(draw->hDC, &draw->rcItem, brush);
            DeleteObject(brush);
            FrameRect(draw->hDC, &draw->rcItem, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            return TRUE;
        }
        return FALSE;
    }
    case WM_COMMAND:
        if(LOWORD(wParam)==kSidebarTheme){SelectPage(false);return 0;}
        if(LOWORD(wParam)==kSidebarAuto){SelectPage(true);return 0;}
        if(LOWORD(wParam)==kAutoEnable&&g_layout){g_layout->autoOrganize=SendMessageW(reinterpret_cast<HWND>(lParam),BM_GETCHECK,0,0)==BST_CHECKED;SaveLayout(*g_layout);return 0;}
        if(LOWORD(wParam)==kAutoAdd&&g_layout&&g_autoBox&&g_autoExtensions){int index=static_cast<int>(SendMessageW(g_autoBox,CB_GETCURSEL,0,0));if(index>=0&&index<static_cast<int>(g_layout->boxes.size())){wchar_t extensions[512]{};GetWindowTextW(g_autoExtensions,extensions,512);HWND folders=GetDlgItem(hwnd,kAutoFolders);g_layout->autoRules.push_back({g_layout->boxes[index].id,SendMessageW(folders,BM_GETCHECK,0,0)==BST_CHECKED,extensions});AddAutoRuleRow(g_layout->autoRules.back());SaveLayout(*g_layout);}return 0;}
        if(LOWORD(wParam)>=kDefaultBoxBase&&LOWORD(wParam)<kDefaultBoxBase+4&&g_layout){int choice=LOWORD(wParam)-kDefaultBoxBase;if(SendMessageW(reinterpret_cast<HWND>(lParam),BM_GETCHECK,0,0)==BST_CHECKED){const wchar_t* titles[]={L"目录",L"文档",L"图片",L"压缩包"};const wchar_t* suffixes[]={L"",L".doc;.docx;.pdf;.txt;.xls;.xlsx;.ppt;.pptx",L".png;.jpg;.jpeg;.gif;.bmp;.webp",L".zip;.rar;.7z;.tar;.gz"};auto found=std::find_if(g_layout->boxes.begin(),g_layout->boxes.end(),[&](const Box& box){return box.title==titles[choice];});if(found==g_layout->boxes.end()){Box box;box.id=std::to_wstring(GetTickCount64())+L"-preset";box.title=titles[choice];box.rect={120+choice*35,120+choice*35,460+choice*35,420+choice*35};g_layout->boxes.push_back(std::move(box));found=std::prev(g_layout->boxes.end());}g_layout->autoRules.push_back({found->id,choice==0,suffixes[choice]});for(const auto& item:EnumerateDesktopItems())if((choice==0&&item.directory)||(choice>0&&!item.directory&&wcsstr(suffixes[choice],item.type.c_str())))AddItem(*found,item.path);SaveLayout(*g_layout);HandleCanvasCommand(CanvasCommand::Reload);}return 0;}
        if (LOWORD(wParam) == kBoxSelect && HIWORD(wParam) == CBN_SELCHANGE) {
            g_selectedBox = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0));
            return 0;
        }
        if (LOWORD(wParam) >= kColorBase && LOWORD(wParam) < kColorBase + 6 && g_layout && g_selectedBox >= 0 && g_selectedBox < static_cast<int>(g_layout->boxes.size())) {
            g_layout->boxes[g_selectedBox].color = kColors[LOWORD(wParam) - kColorBase];
            SaveLayout(*g_layout);
            CanvasSetOpacity(g_layout->opacity);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) DestroyWindow(hwnd);
        return 0;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY: if (g_font) { DeleteObject(g_font); g_font=nullptr; } g_window = nullptr; g_value = nullptr; g_boxSelect=nullptr;g_autoBox=nullptr;g_autoExtensions=nullptr;g_autoRules=nullptr;g_themePage.clear();g_autoPage.clear();g_layout = nullptr; return 0;
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
    g_window = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"nestlone-D 设置中心", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 540, 500, nullptr, nullptr, instance, nullptr);
    if (!g_window) return;
    ShowWindow(g_window, SW_SHOWNORMAL);
    UpdateWindow(g_window);
}

}

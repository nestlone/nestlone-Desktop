#include <windows.h>
#include <shlobj.h>
#include <exdisp.h>
#include <shlguid.h>
#include <shlwapi.h>
#include <wrl/client.h>
#include <cstdio>
using Microsoft::WRL::ComPtr;
int wmain(int argc,wchar_t** argv) {
    if(argc<2 || argc>3)return 2;
    CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    int result=1;
    {
    ComPtr<IShellWindows> windows;
    HRESULT hr=CoCreateInstance(CLSID_ShellWindows,nullptr,CLSCTX_LOCAL_SERVER,IID_PPV_ARGS(&windows));
    if(FAILED(hr)){printf("ShellWindows %08lx\n",hr);return 1;}
    VARIANT empty{};long handle=0;ComPtr<IDispatch> dispatch;
    hr=windows->FindWindowSW(&empty,&empty,SWC_DESKTOP,&handle,SWFO_NEEDDISPATCH,&dispatch);
    ComPtr<IServiceProvider> provider;ComPtr<IShellBrowser> browser;ComPtr<IShellView> shellView;ComPtr<IFolderView2> view;
    if(FAILED(hr)||FAILED(dispatch.As(&provider))||
       FAILED(provider->QueryService(SID_STopLevelBrowser,IID_PPV_ARGS(&browser)))||
       FAILED(browser->QueryActiveShellView(&shellView))||FAILED(shellView.As(&view)))return 1;
    DWORD flags=0;view->GetCurrentFolderFlags(&flags);
    printf("flags=%08lx autoarrange=%d\n",flags,(flags&FWF_AUTOARRANGE)!=0);
    if(flags&FWF_AUTOARRANGE){printf("Auto-arrange prevents this experiment; no settings changed.\n");return 3;}
    ComPtr<IShellFolder> folder;view->GetFolder(IID_PPV_ARGS(&folder));
    ComPtr<IEnumIDList> items;view->Items(SVGIO_ALLVIEW,IID_PPV_ARGS(&items));
    PITEMID_CHILD item=nullptr;
    while(items->Next(1,&item,nullptr)==S_OK) {
        STRRET name{};wchar_t path[32768]{};
        folder->GetDisplayNameOf(item,SHGDN_FORPARSING,&name);
        StrRetToBufW(&name,item,path,32768);
        if(_wcsicmp(path,argv[1])==0) {
            POINT original{},actual{},hidden{GetSystemMetrics(SM_XVIRTUALSCREEN)+GetSystemMetrics(SM_CXVIRTUALSCREEN)+512,256};
            const bool negative=argc==3 && wcscmp(argv[2],L"negative")==0;
            if(negative)hidden={-20000,-20000};
            view->GetItemPosition(item,&original);
            PCUITEMID_CHILD array[]={item};
            hr=view->SelectAndPositionItems(1,array,&hidden,SVSI_POSITIONITEM);
            Sleep(400);
            view->GetItemPosition(item,&actual);
            printf("original=%ld,%ld requested=%ld,%ld actual=%ld,%ld hr=%08lx\n",
                original.x,original.y,hidden.x,hidden.y,actual.x,actual.y,hr);
            HRESULT restore=view->SelectAndPositionItems(1,array,&original,SVSI_POSITIONITEM);
            Sleep(300);POINT restored{};view->GetItemPosition(item,&restored);
            printf("restore=%08lx actual=%ld,%ld\n",restore,restored.x,restored.y);
            const bool outside=negative ? actual.x<0 && actual.y<0 :
                actual.x>=GetSystemMetrics(SM_XVIRTUALSCREEN)+GetSystemMetrics(SM_CXVIRTUALSCREEN);
            result=SUCCEEDED(hr)&&outside&&restored.x==original.x&&restored.y==original.y?0:4;
            CoTaskMemFree(item);break;
        }
        CoTaskMemFree(item);
    }
    }
    CoUninitialize();
    return result;
}

#include "../../src/DesktopCanvas.cpp"
#include <cstdio>
#include <vector>

int main() {
    Gdiplus::GdiplusStartupInput input;
    ULONG_PTR token=0;
    if (Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok) return 1;
    int failures=0;
    auto check=[&](bool ok,const char* what){printf("%s %s\n",ok?"PASS":"FAIL",what);if(!ok)++failures;};
    nestlone::Layout layout;
    nestlone::Box box;
    box.rect={20,20,300,220}; box.title=L"Transparency";
    layout.boxes.push_back(box);
    std::vector<DWORD> low(320*240), high(low.size());
    layout.opacity=20;
    check(nestlone::RenderPixels(low.data(),320,240,layout),"render 20%");
    layout.opacity=100;
    check(nestlone::RenderPixels(high.data(),320,240,layout),"render 100%");
    check((low[150*320+100]>>24)==51,"background alpha at 20% is 51");
    check((high[150*320+100]>>24)==255,"background alpha at 100% is 255");
    check(low[0]==0 && high[0]==0,"outside remains transparent");
    int opaque=0;
    for(int y=25;y<47;++y)for(int x=32;x<160;++x)
        if((low[y*320+x]>>24)==255 && low[y*320+x]==high[y*320+x])++opaque;
    check(opaque>20,"title has identical opaque pixels at both settings");
    bool premultiplied=true;
    for(DWORD p:low) { DWORD a=p>>24; if((p&255)>a||((p>>8)&255)>a||((p>>16)&255)>a)premultiplied=false; }
    check(premultiplied,"all pixels use premultiplied alpha");
    BYTE andMask[32],xorMask[32]{};
    memset(andMask,255,sizeof(andMask));
    for(int y=4;y<12;++y) {andMask[y*2]=0xf0;andMask[y*2+1]=0x0f;}
    HICON legacy=CreateIcon(nullptr,16,16,1,1,andMask,xorMask);
    auto icon=nestlone::IconPixels(legacy,16);
    check(icon.size()==256 && icon[0]==0,"legacy icon mask preserves transparent corners");
    check(icon.size()==256 && icon[8*16+8]==0xff000000,"opaque black icon pixels are not removed");
    DestroyIcon(legacy);
    SHFILEINFOW shell{};
    SHGetFileInfoW(L"C:\\Windows",0,&shell,sizeof(shell),SHGFI_ICON|SHGFI_LARGEICON);
    auto folder=nestlone::IconPixels(shell.hIcon,32);
    check(folder.size()==1024 && (folder[0]>>24)==0,"Shell folder has no opaque rectangular background");
    DestroyIcon(shell.hIcon);
    layout.boxes[0].collapsed=true;
    nestlone::RenderPixels(low.data(),320,240,layout);
    check(low[150*320+100]==0,"collapsed body and border are absent");
    Gdiplus::GdiplusShutdown(token);
    return failures ? 1 : 0;
}

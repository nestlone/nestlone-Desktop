#include "../../src/DesktopCanvas.cpp"
#include <cstdio>
int main() {
    Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
    if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok)return 1;
    int failures=0;auto check=[&](bool ok,const char* text){printf("%s %s\n",ok?"PASS":"FAIL",text);if(!ok)++failures;};
    nestlone::Layout layout;nestlone::Box box;box.rect={20,20,300,220};box.title=L"Native";layout.boxes.push_back(box);
    std::vector<DWORD> low(320*240),high(low.size()),withItems(low.size());
    layout.opacity=20;check(nestlone::RenderPixels(low.data(),320,240,layout),"render background at 20%");
    layout.opacity=100;check(nestlone::RenderPixels(high.data(),320,240,layout),"render background at 100%");
    check((low[150*320+100]>>24)==51 && (high[150*320+100]>>24)==255,"only background opacity is rendered");
    layout.boxes[0].items={L"C:\\anything.lnk",L"::virtual-item"};
    nestlone::RenderPixels(withItems.data(),320,240,layout);
    check(withItems==high,"adding icons to a box never draws their pixels or labels");
    check(low[0]==0 && high[0]==0,"outside boxes stays transparent");
    bool premultiplied=true;for(DWORD p:low){DWORD a=p>>24;if((p&255)>a||((p>>8)&255)>a||((p>>16)&255)>a)premultiplied=false;}
    check(premultiplied,"premultiplied alpha is valid");
    layout.boxes[0].collapsed=true;nestlone::RenderPixels(low.data(),320,240,layout);
    check(low[150*320+100]==0,"collapse removes only background body");
    Gdiplus::GdiplusShutdown(token);return failures?1:0;
}

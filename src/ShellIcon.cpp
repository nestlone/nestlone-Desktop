#include "DesktopSession.h"
#include <algorithm>
namespace nestlone {
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
}

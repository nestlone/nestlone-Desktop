#pragma once
#include <windows.h>
#include <roapi.h>
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Geolocation.h>
#pragma pop_macro("max")
#pragma pop_macro("min")
#include <mutex>
#include <atomic>
#include <string>
#include <chrono>
#include <cmath>
#pragma comment(lib,"windowsapp.lib")

namespace nestlone::location {
inline std::mutex mutex;
inline std::wstring status=L"尚未获取位置";
inline std::atomic_bool requesting=false;
inline void Status(const std::wstring& text) {std::lock_guard<std::mutex> lock(mutex);status=text;}
inline std::wstring Status() {std::lock_guard<std::mutex> lock(mutex);return status;}
inline std::wstring Error(HRESULT code) {
    if(code==E_ACCESSDENIED)return L"定位访问被拒绝，请检查 Windows 定位权限";
    if(code==HRESULT_FROM_WIN32(ERROR_TIMEOUT))return L"定位超时，暂时无法获得当前位置";
    wchar_t text[100]{};swprintf_s(text,L"暂时无法获取位置（0x%08X）",static_cast<unsigned>(code));return text;
}
// Called by an explicit settings action on the foreground UI thread.
inline void RequestAccess() {
    using namespace winrt::Windows::Devices::Geolocation;
    if(requesting.exchange(true))return;
    Status(L"正在请求 Windows 定位权限…");
    HRESULT initialized=RoInitialize(RO_INIT_SINGLETHREADED);
    try {
        auto operation=Geolocator::RequestAccessAsync();
        operation.Completed([](const auto& result,winrt::Windows::Foundation::AsyncStatus state) {
            try {
                if(state==winrt::Windows::Foundation::AsyncStatus::Completed) {
                    const auto access=result.GetResults();
                    Status(access==GeolocationAccessStatus::Allowed?L"已获授权，可点击“获取当前位置”":access==GeolocationAccessStatus::Denied?L"定位权限被拒绝，请打开系统定位设置":L"Windows 未确认定位权限，请重试");
                } else Status(Error(result.ErrorCode()));
            } catch(const winrt::hresult_error& e){Status(Error(e.code()));}
            requesting=false;
        });
    } catch(const winrt::hresult_error& e){requesting=false;Status(Error(e.code()));}
    if(SUCCEEDED(initialized))RoUninitialize();
}
// Only used on a worker thread. No weather API call is made by this function.
inline bool Read(double& latitude,double& longitude,std::wstring& error) {
    HRESULT initialized=RoInitialize(RO_INIT_MULTITHREADED);
    bool found=false;
    try {
        using namespace winrt::Windows::Devices::Geolocation;
        Status(L"正在获取当前位置…");
        Geolocator locator;locator.DesiredAccuracy(PositionAccuracy::Default);
        auto fix=locator.GetGeopositionAsync(std::chrono::seconds(0),std::chrono::seconds(15)).get();
        auto coordinate=fix.Coordinate();auto point=coordinate.Point().Position();
        latitude=point.Latitude;longitude=point.Longitude;
        const auto age=winrt::clock::now()-coordinate.Timestamp();
        found=coordinate.PositionSource()!=PositionSource::Default&&age<=std::chrono::minutes(5)&&std::isfinite(latitude)&&std::isfinite(longitude)&&latitude>=-90&&latitude<=90&&longitude>=-180&&longitude<=180;
        if(found){wchar_t text[160]{};swprintf_s(text,L"Windows 定位：经度 %.5f，纬度 %.5f\r\n精度约 %.0f 米 · 本次获取",longitude,latitude,coordinate.Accuracy());Status(text);}
        else error=L"Windows 未返回新鲜的实际位置（默认位置或过期数据）";
    } catch(const winrt::hresult_error& e){error=Error(e.code());}
    if(SUCCEEDED(initialized))RoUninitialize();
    if(!found)Status(error);
    return found;
}
inline bool ParseCoordinate(const std::wstring& text,double limit,double& value) {
    wchar_t* end=nullptr;value=wcstod(text.c_str(),&end);
    if(end==text.c_str())return false;
    while(*end==L' '||*end==L'\t')++end;
    return !*end&&std::isfinite(value)&&value>=-limit&&value<=limit;
}
}

#pragma once
#include <windows.h>

namespace heben {
// LR_SHARED handles belong to Windows and must not be destroyed by callers.
inline HICON LoadAppIcon(HINSTANCE instance,int width,int height) {
    return static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(101),IMAGE_ICON,
                                         width,height,LR_SHARED));
}
inline HICON LoadSmallAppIcon(HINSTANCE instance) {
    return LoadAppIcon(instance,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON));
}
inline HICON LoadLargeAppIcon(HINSTANCE instance) {
    return LoadAppIcon(instance,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON));
}
}

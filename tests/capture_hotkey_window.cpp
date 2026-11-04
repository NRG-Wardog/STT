// Capture a verified test-owned settings or newly opened Explorer window.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

int wmain(int argc,wchar_t **argv) {
    if(argc!=4 && argc!=5)return 2;
    HWND hwnd=reinterpret_cast<HWND>(static_cast<UINT_PTR>(_wcstoui64(argv[1],nullptr,10)));
    DWORD expected=static_cast<DWORD>(_wtoi(argv[2])),owner=0;GetWindowThreadProcessId(hwnd,&owner);
    wchar_t name[128]{};GetClassNameW(hwnd,name,128);
    const wchar_t *allowed=argc==5?argv[4]:L"HebENHotkeySettings";
    if(wcscmp(allowed,L"HebENHotkeySettings")&&wcscmp(allowed,L"CabinetWClass"))return 3;
    if(!IsWindow(hwnd)||owner!=expected||wcscmp(name,allowed))return 3;
    RECT rect{};if(!GetWindowRect(hwnd,&rect))return 4;int w=rect.right-rect.left,h=rect.bottom-rect.top;
    if(w<=0||h<=0||w>2000||h>2000)return 5;
    HDC screen=GetDC(nullptr),mem=CreateCompatibleDC(screen);HBITMAP bmp=CreateCompatibleBitmap(screen,w,h);
    if(!screen||!mem||!bmp)return 6;HGDIOBJ old=SelectObject(mem,bmp);
    BOOL rendered=PrintWindow(hwnd,mem,2);SelectObject(mem,old);
    BITMAPINFO bmi{};bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bmi.bmiHeader.biWidth=w;bmi.bmiHeader.biHeight=-h;
    bmi.bmiHeader.biPlanes=1;bmi.bmiHeader.biBitCount=32;bmi.bmiHeader.biCompression=BI_RGB;
    std::vector<unsigned char> pixels(static_cast<size_t>(w)*h*4);
    int lines=rendered?GetDIBits(mem,bmp,0,h,pixels.data(),&bmi,DIB_RGB_COLORS):0;
    DeleteObject(bmp);DeleteDC(mem);ReleaseDC(nullptr,screen);if(lines!=h)return 7;
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER);
    file.bfSize=file.bfOffBits+static_cast<DWORD>(pixels.size());
    HANDLE out=CreateFileW(argv[3],GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(out==INVALID_HANDLE_VALUE)return 8;
    DWORD n=0;bool ok=WriteFile(out,&file,sizeof(file),&n,nullptr)&&WriteFile(out,&bmi.bmiHeader,sizeof(bmi.bmiHeader),&n,nullptr)&&
        WriteFile(out,pixels.data(),static_cast<DWORD>(pixels.size()),&n,nullptr);CloseHandle(out);return ok?0:9;
}

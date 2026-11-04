// Read-only default playback endpoint state. Does not alter volume or routing.
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <cstdio>

int main() {
    HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);IMMDeviceEnumerator *enumerator=nullptr;
    HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(&enumerator));
    IMMDevice *device=nullptr;IAudioEndpointVolume *volume=nullptr;
    if(SUCCEEDED(hr))hr=enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device);
    if(SUCCEEDED(hr))hr=device->Activate(__uuidof(IAudioEndpointVolume),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&volume));
    BOOL muted=FALSE;float scalar=0;
    HRESULT muteHr=SUCCEEDED(hr)?volume->GetMute(&muted):hr;
    HRESULT volumeHr=SUCCEEDED(hr)?volume->GetMasterVolumeLevelScalar(&scalar):hr;
    std::printf("{\"status\":\"%s\",\"endpoint_hr\":%ld,\"mute_hr\":%ld,\"volume_hr\":%ld,\"muted\":%s,\"volume_scalar\":%.4f}\n",
        SUCCEEDED(muteHr)&&SUCCEEDED(volumeHr)?"PASS_READ_ONLY":"FAIL",hr,muteHr,volumeHr,muted?"true":"false",scalar);
    if(volume)volume->Release();if(device)device->Release();if(enumerator)enumerator->Release();if(SUCCEEDED(init))CoUninitialize();
    return SUCCEEDED(muteHr)&&SUCCEEDED(volumeHr)?0:1;
}

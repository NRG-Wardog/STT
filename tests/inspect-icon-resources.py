"""Inspect group icon presence and ICO frames without starting either app."""
import ctypes, hashlib, json, struct, sys
from pathlib import Path
from PIL import Image
import io

kernel=ctypes.WinDLL('kernel32',use_last_error=True)
kernel.LoadLibraryExW.argtypes=[ctypes.c_wchar_p,ctypes.c_void_p,ctypes.c_uint32];kernel.LoadLibraryExW.restype=ctypes.c_void_p
kernel.FindResourceW.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p];kernel.FindResourceW.restype=ctypes.c_void_p
kernel.FreeLibrary.argtypes=[ctypes.c_void_p]
output=[]
for arg in sys.argv[1:]:
    path=Path(arg).resolve(); record={'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    if path.suffix.lower()=='.ico':
        data=path.read_bytes();reserved,kind,count=struct.unpack_from('<HHH',data)
        frames=[]
        for i in range(count):
            w,h,_,_,planes,bits,size,offset=struct.unpack_from('<BBBBHHII',data,6+16*i)
            frame=Image.open(io.BytesIO(data[offset:offset+size])).convert('RGBA'); alpha=frame.getchannel('A')
            frames.append({'width':w or 256,'height':h or 256,'bits':bits,'alpha_range':list(alpha.getextrema()),'transparent_pixels':sum(1 for a in alpha.getdata() if a==0)})
        record.update(reserved=reserved,type=kind,frames=frames)
    else:
        lib=kernel.LoadLibraryExW(str(path),None,0x22)
        if not lib:record['error']=ctypes.get_last_error()
        else:
            record['group_icon_101']=bool(kernel.FindResourceW(lib,ctypes.c_void_p(101),ctypes.c_void_p(14)))
            kernel.FreeLibrary(lib)
    output.append(record)
print(json.dumps(output,indent=2))

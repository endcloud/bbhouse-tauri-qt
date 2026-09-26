"""Windows hidden native mpv D3D11 comparison, NOT the Qt render API path.

Plays only an explicitly supplied local fixture, with no audio, network/config,
credentials or raw logger. No conclusions about Qt overlay/HDR integration.
"""
import argparse
import ctypes as C
import json
from pathlib import Path
import sys
import time
from probe_mpv_memory import memory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", required=True, type=Path)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--hw-threads", type=int, default=4)
    args = parser.parse_args()
    fixture = args.fixture.resolve()
    if not fixture.is_file() or str(fixture).startswith("\\\\") or not 1 <= args.seconds <= 120:
        return 2
    user = C.WinDLL("user32")
    user.CreateWindowExW.argtypes = [C.c_ulong, C.c_wchar_p, C.c_wchar_p, C.c_ulong,
        C.c_int, C.c_int, C.c_int, C.c_int, C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p]
    user.CreateWindowExW.restype = C.c_void_p
    user.DestroyWindow.argtypes = [C.c_void_p]
    window = user.CreateWindowExW(0, "STATIC", "Memory probe", 0x80000000,
                                  0, 0, 1920, 1080, None, None, None, None)
    if not window:
        return 3
    lib = C.CDLL(str(args.dll.resolve()))
    lib.mpv_create.restype = C.c_void_p
    lib.mpv_initialize.argtypes = [C.c_void_p]
    lib.mpv_set_option_string.argtypes = [C.c_void_p, C.c_char_p, C.c_char_p]
    lib.mpv_get_property_string.argtypes = [C.c_void_p, C.c_char_p]
    lib.mpv_get_property_string.restype = C.c_void_p
    lib.mpv_free.argtypes = [C.c_void_p]
    lib.mpv_command.argtypes = [C.c_void_p, C.POINTER(C.c_char_p)]
    lib.mpv_terminate_destroy.argtypes = [C.c_void_p]
    handle = lib.mpv_create()
    if not handle:
        user.DestroyWindow(window)
        return 4
    try:
        options = {"config": "no", "load-scripts": "no", "terminal": "no", "msg-level": "all=no",
            "vo": "gpu", "gpu-api": "d3d11", "gpu-context": "d3d11", "hwdec": "auto-safe",
            "hwdec-threads": str(args.hw_threads), "ao": "null", "idle": "yes", "keep-open": "always",
            "cache": "no", "ytdl": "no", "sub-auto": "no", "audio-file-auto": "no",
            "access-references": "no", "http-proxy": "", "stream-lavf-o": "http_proxy=",
            "wid": str(window)}
        for key, value in options.items():
            if lib.mpv_set_option_string(handle, key.encode(), value.encode()) < 0:
                return 5
        if lib.mpv_initialize(handle) < 0:
            return 6
        command = (C.c_char_p * 4)(b"loadfile", str(fixture).encode(), b"replace", None)
        if lib.mpv_command(handle, command) < 0:
            return 7
        samples = []
        start = time.monotonic()
        while time.monotonic() - start < args.seconds:
            sample = memory()
            sample["elapsed_ms"] = round((time.monotonic() - start) * 1000)
            samples.append(sample)
            time.sleep(.1)
        properties = {}
        for key in ["hwdec-current", "current-vo", "video-params/pixelformat", "video-params/gamma",
                    "video-dec-params/pixelformat", "frame-drop-count", "decoder-frame-drop-count", "time-pos"]:
            pointer = lib.mpv_get_property_string(handle, key.encode())
            properties[key] = C.string_at(pointer).decode() if pointer else None
            if pointer:
                lib.mpv_free(pointer)
        print(json.dumps({"mode": "hidden-native-d3d11-vo-gpu", "hw_threads_option": args.hw_threads,
                          "output_width": 1920, "output_height": 1080,
                          "properties": properties, "samples": samples}, indent=2))
        return 0 if properties["time-pos"] and float(properties["time-pos"]) > 0 else 8
    finally:
        lib.mpv_terminate_destroy(handle)
        user.DestroyWindow(window)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(json.dumps({"failed": True, "error_type": type(error).__name__}))
        sys.exit(1)

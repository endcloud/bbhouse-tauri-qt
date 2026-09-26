"""Offline, standard-library-only libmpv option / synthetic decoder probe.

This deliberately does not model the application's OpenGL renderer or real CDN.
Never pass user media: --fixture is intended only for a generated local fixture.
No config, scripts, external references, captions, network URLs, or raw mpv logs.
Only the explicit diagnostic allowlist below is printed (no paths or URLs).
"""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import time

OPTIONS = [
    "cache", "cache-secs", "demuxer-readahead-secs", "demuxer-max-bytes",
    "demuxer-max-back-bytes", "demuxer-donate-buffer", "demuxer-hysteresis-secs",
    "vd-lavc-threads", "vd-lavc-dr", "hwdec-extra-frames", "video-sync", "interpolation",
    "fbo-format", "scale", "dscale", "hwdec", "gpu-api", "gpu-context",
]
PROPERTIES = [
    "mpv-version", "ffmpeg-version", "hwdec-current", "current-vo", "video-codec",
    "video-params/w", "video-params/h", "video-params/pixelformat", "estimated-vf-fps",
    "decoder-frame-drop-count", "frame-drop-count", "demuxer-cache-duration",
]


class MemoryCounters(C.Structure):
    _fields_ = [("cb", C.c_ulong), ("PageFaultCount", C.c_ulong)] + [
        (name, C.c_size_t) for name in (
            "PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage",
            "QuotaPagedPoolUsage", "QuotaPeakNonPagedPoolUsage",
            "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage", "PrivateUsage")]


def memory():
    kernel = C.WinDLL("kernel32", use_last_error=True)
    psapi = C.WinDLL("psapi", use_last_error=True)
    kernel.GetCurrentProcess.restype = C.c_void_p
    psapi.GetProcessMemoryInfo.argtypes = [C.c_void_p, C.POINTER(MemoryCounters), C.c_ulong]
    counters = MemoryCounters()
    counters.cb = C.sizeof(counters)
    if not psapi.GetProcessMemoryInfo(kernel.GetCurrentProcess(), C.byref(counters), counters.cb):
        raise C.WinError(C.get_last_error())
    return {"private_mib": round(counters.PrivateUsage / 1048576, 2),
            "working_set_mib": round(counters.WorkingSetSize / 1048576, 2)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--fixture", type=Path)
    parser.add_argument("--threads", type=int, default=0)
    parser.add_argument("--seconds", type=float, default=6)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("this probe uses Windows process memory counters")
    if args.fixture:
        fixture = args.fixture.resolve()
        if str(fixture).startswith("\\\\") or not fixture.is_file():
            parser.error("fixture must be an existing, generated local file")
    lib = C.CDLL(str(args.dll.resolve()))
    lib.mpv_create.restype = C.c_void_p
    for name, signature, result in [
        ("mpv_set_option_string", [C.c_void_p, C.c_char_p, C.c_char_p], C.c_int),
        ("mpv_initialize", [C.c_void_p], C.c_int),
        ("mpv_get_property_string", [C.c_void_p, C.c_char_p], C.c_void_p),
        ("mpv_command", [C.c_void_p, C.POINTER(C.c_char_p)], C.c_int),
        ("mpv_free", [C.c_void_p], None),
        ("mpv_terminate_destroy", [C.c_void_p], None),
    ]:
        function = getattr(lib, name)
        function.argtypes, function.restype = signature, result
    baseline = memory()
    handle = lib.mpv_create()
    if not handle:
        raise RuntimeError("mpv_create failed")

    def get(key):
        pointer = lib.mpv_get_property_string(handle, key.encode())
        if not pointer:
            return None
        try:
            return C.string_at(pointer).decode("utf-8", errors="replace")
        finally:
            lib.mpv_free(pointer)

    def command(*values):
        array = (C.c_char_p * (len(values) + 1))(*[v.encode("utf-8") for v in values], None)
        result = lib.mpv_command(handle, array)
        if result < 0:
            raise RuntimeError("mpv command failed with code " + str(result))

    try:
        opts = {"config": "no", "load-scripts": "no", "terminal": "no",
                "msg-level": "all=no", "idle": "yes", "ytdl": "no",
                "sub-auto": "no", "audio-file-auto": "no", "access-references": "no",
                "http-proxy": "", "stream-lavf-o": "http_proxy=",
                "vo": "libmpv", "hwdec": "auto-safe"}
        if args.fixture:
            opts.update({"vo": "null", "ao": "null", "hwdec": "no", "cache": "no",
                         "keep-open": "always", "vd-lavc-threads": str(args.threads)})
        for name, value in opts.items():
            result = lib.mpv_set_option_string(handle, name.encode(), value.encode())
            if result < 0:
                raise RuntimeError("mpv option " + name + " failed with code " + str(result))
        if lib.mpv_initialize(handle) < 0:
            raise RuntimeError("mpv_initialize failed")
        output = {"mode": "software-decoder-only" if args.fixture else "idle-default-options",
                  "logical_cpu_count": os.cpu_count(), "baseline": baseline,
                  "initialized": memory(),
                  "options": {key: get("options/" + key) for key in OPTIONS}}
        if args.fixture:
            command("loadfile", str(fixture), "replace")
            samples = []
            started = time.monotonic()
            while time.monotonic() - started < args.seconds:
                samples.append({"elapsed": round(time.monotonic() - started, 2),
                                "playback_time": get("time-pos"), **memory()})
                time.sleep(0.1)
            output["samples"] = samples
            output["peak_private_mib"] = max(s["private_mib"] for s in samples)
            output["peak_working_set_mib"] = max(s["working_set_mib"] for s in samples)
        output["properties"] = {key: get(key) for key in PROPERTIES}
        if args.fixture:
            command("stop")
            time.sleep(0.2)
            output["after_stop"] = memory()
    finally:
        lib.mpv_terminate_destroy(handle)
    output["after_destroy"] = memory()
    print(json.dumps(output, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()

"""Fetch a user-specified Bilibili video for isolated memory measurement.

Reads an existing cookie only when --cookie-file is explicitly supplied. Signed
URLs and credentials never leave memory or enter subprocess arguments/logs.
Only GET requests are made; no player heartbeat/history API is called.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

MIXIN = [46,47,18,2,53,8,23,32,15,50,10,31,58,3,45,35,27,43,5,49,33,9,42,19,29,28,14,39,12,38,41,13]
HEADERS = {"User-Agent": "Mozilla/5.0", "Referer": "https://www.bilibili.com/", "Origin": "https://www.bilibili.com"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bvid", required=True)
    parser.add_argument("--cookie-file", type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--download", action="store_true")
    parser.add_argument("--sample-mib", type=int, default=64,
                        help="Read only this many initial MiB per representation (1..256)")
    parser.add_argument("--quality", type=int, action="append", default=[])
    args = parser.parse_args()
    if not 1 <= args.sample_mib <= 256:
        raise ValueError("invalid sample size")
    if not re.fullmatch(r"BV[0-9A-Za-z]{10}", args.bvid):
        raise ValueError("invalid bvid")
    cookie = args.cookie_file.read_text(encoding="utf-8-sig").strip() if args.cookie_file else ""
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def get(path, params=None):
        url = "https://api.bilibili.com" + path
        if params:
            url += "?" + urllib.parse.urlencode(params, quote_via=urllib.parse.quote)
        headers = dict(HEADERS)
        if cookie:
            headers["Cookie"] = cookie
        with opener.open(urllib.request.Request(url, headers=headers), timeout=30) as response:
            envelope = json.load(response)
        if envelope.get("code") != 0:
            # Server text can contain private material; retain only numeric code.
            raise ValueError("api code " + str(int(envelope.get("code", -1))))
        return envelope["data"]

    view = get("/x/web-interface/view", {"bvid": args.bvid})
    nav = get("/x/web-interface/nav")
    raw = "".join(Path(urllib.parse.urlparse(nav["wbi_img"][key]).path).stem for key in ("img_url", "sub_url"))
    mixin = "".join(raw[i] for i in MIXIN)
    params = dict(avid=str(view["aid"]), cid=str(view["cid"]), qn="120", fnval="4048", fnver="0", fourk="1", platform="pc", wts=str(int(time.time())))
    query = urllib.parse.urlencode(sorted(params.items()), quote_via=urllib.parse.quote)
    params["w_rid"] = hashlib.md5((query + mixin).encode()).hexdigest()
    data = get("/x/player/wbi/playurl", params)
    streams = data.get("dash", {}).get("video", [])
    summary = {"bvid": args.bvid, "duration_seconds": view.get("duration"), "streams": [
        {key: stream.get(key) for key in ("id", "codecs", "width", "height", "frame_rate", "bandwidth", "size")}
        for stream in streams], "downloads": []}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    if args.download:
        for quality in args.quality or [80, 126]:
            matches = [s for s in streams if s.get("id") == quality]
            matches.sort(key=lambda s: not str(s.get("codecs", "")).startswith(("hev", "hvc", "dv")))
            if not matches:
                continue
            stream = matches[0]
            target = args.output_dir / (args.bvid + "-qn" + str(quality) + ".m4s")
            if target.exists():
                raise ValueError("fixture already exists")
            urls = [stream.get("baseUrl") or stream.get("base_url")] + (stream.get("backupUrl") or stream.get("backup_url") or [])
            # Follow the production preference for ordinary cloud CDN over P2P.
            urls = sorted((u for u in urls if u), key=lambda u: "mcdn" in urllib.parse.urlparse(u).hostname)
            downloaded = False
            for url in urls:
                try:
                    headers = dict(HEADERS, Range="bytes=0-" + str(args.sample_mib * 1048576 - 1))
                    with opener.open(urllib.request.Request(url, headers=headers), timeout=30) as response, target.open("wb") as output:
                        count = 0
                        while count < args.sample_mib * 1048576:
                            chunk = response.read(min(1048576, args.sample_mib * 1048576 - count))
                            if not chunk:
                                break
                            count += len(chunk)
                            output.write(chunk)
                    downloaded = True
                    break
                except (urllib.error.URLError, TimeoutError):
                    target.unlink(missing_ok=True)
            if not downloaded:
                raise ValueError("all media candidates failed")
            summary["downloads"].append({"quality": quality, "prefix_only": True, "bytes": target.stat().st_size,
                "sha256": hashlib.sha256(target.read_bytes()).hexdigest()})
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        # Never print exception text: HTTP exceptions can include signed URLs.
        print(json.dumps({"error_type": type(error).__name__, "failed": True}))
        sys.exit(1)

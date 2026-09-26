"""Audit a Windows package without trusting developer PATH or System32 extras."""
import argparse
import http.server
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading

# Windows 10/11 components. In particular Vulkan and VC++ redistributables are
# NOT accepted merely because they happen to exist on the packaging machine.
SYSTEM_DLLS = set("""
advapi32 avicap32 avrt bcrypt bcryptprimitives cabinet cfgmgr32 comctl32 comdlg32 crypt32 cryptbase
d2d1 d3d9 d3d11 d3d12 d3dcompiler_47 dcomp dhcpcsvc dnsapi dwmapi dwrite dxgi dxva2
gdi32 hid imm32 iphlpapi kernel32 kernelbase mf mfplat mfreadwrite mfuuid mpr
msimg32 msvcrt mswsock ncrypt netapi32 normaliz ntdll ole32 oleaut32 opengl32 powrprof
propsys psapi rasapi32 rpcrt4 runtimeobject secur32 setupapi shcore shell32 shlwapi
srvcli taskschd user32 userenv usp10 uxtheme version winhttp wininet winmm winspool.drv
wintrust wldap32 ws2_32 wsock32 wtsapi32 urlmon uiautomationcore authz dbghelp
""".split())
SYSTEM_DLLS = {x if '.' in x else x + '.dll' for x in SYSTEM_DLLS}


def pe_imports(path):
    data = path.read_bytes()
    if data[:2] != b'MZ':
        raise ValueError(f'Not a PE image: {path}')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError(f'Invalid PE: {path}')
    machine, sections = struct.unpack_from('<HH', data, pe + 4)
    if machine != 0x8664:
        raise ValueError(f'Package requires x64 PE images: {path}')
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    if struct.unpack_from('<H', data, optional)[0] != 0x20b:
        raise ValueError(f'Expected PE32+: {path}')
    image_base = struct.unpack_from('<Q', data, optional + 24)[0]
    section_table = optional + optional_size

    def offset(rva):
        for i in range(sections):
            size, start, raw_size, raw_start = struct.unpack_from('<IIII', data, section_table + i * 40 + 8)
            if start <= rva < start + max(size, raw_size):
                return raw_start + rva - start
        raise ValueError(f'Invalid RVA {rva:x} in {path}')

    def name(rva):
        start = offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii').lower()

    imports = set()
    normal, _ = struct.unpack_from('<II', data, optional + 112 + 8)
    if normal:
        pos = offset(normal)
        while any(data[pos:pos + 20]):
            imports.add(name(struct.unpack_from('<I', data, pos + 12)[0]))
            pos += 20
    delay, _ = struct.unpack_from('<II', data, optional + 112 + 13 * 8)
    if delay:
        pos = offset(delay)
        while any(data[pos:pos + 32]):
            flags, name_address = struct.unpack_from('<II', data, pos)
            imports.add(name(name_address if flags & 1 else name_address - image_base))
            pos += 32
    return sorted(imports)


def audit(root):
    inventory = []
    missing = []
    for binary in sorted(root.rglob('*')):
        if binary.suffix.lower() not in {'.dll', '.exe'}:
            continue
        dependencies = pe_imports(binary)
        available = {p.name.lower() for folder in {root, binary.parent} for p in folder.iterdir() if p.is_file()}
        for dependency in dependencies:
            if dependency not in available and dependency not in SYSTEM_DLLS and not dependency.startswith(('api-ms-win-', 'ext-ms-win-')):
                missing.append(f'{binary.relative_to(root)} -> {dependency}')
        inventory.append({'file': binary.relative_to(root).as_posix(), 'imports': dependencies})
    if missing:
        raise RuntimeError('Missing non-system imports:\n' + '\n'.join(missing))
    return inventory


def clean_environment(root):
    env = dict(os.environ)
    for key in list(env):
        if key.upper().startswith(('QT_', 'QML', 'BBHOUSE_', 'MPV_')):
            env.pop(key)
    system_root = os.environ['SystemRoot']
    env['PATH'] = str(Path(system_root) / 'System32') + os.pathsep + system_root
    return env


def check_tools(root, scratch):
    env = clean_environment(root)
    tools = {'ffmpeg': root / 'tools/ffmpeg.exe', 'aria2': root / 'tools/aria2c.exe',
             'curl': Path(os.environ['SystemRoot']) / 'System32/curl.exe'}

    def run(tool, *args, cwd=None):
        result = subprocess.run([str(tools[tool]), *map(str, args)], cwd=cwd, env=env,
                                capture_output=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            raise RuntimeError(f'{tool} fixture failed: {result.stderr.decode(errors="replace")[-2500:]}')
        return result.stdout.decode(errors='replace')

    versions = {key: run(key, '-version' if key == 'ffmpeg' else '--version').splitlines()[0] for key in tools}
    with tempfile.TemporaryDirectory(prefix='package-tools-', dir=scratch) as folder:
        work = Path(folder)
        source = work / 'fixture.mp4'
        run('ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'color=c=red:s=64x64:r=5',
            '-f', 'lavfi', '-i', 'anullsrc=r=48000:cl=stereo', '-t', '0.4', '-c:v', 'libx264', '-c:a', 'aac', source)
        run('ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', source, '-c', 'copy', work / 'remux.mp4')
        run('ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', source, '-vn', '-c:a', 'copy', work / 'audio.m4a')
        run('ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', source, '-frames:v', '1', '-vf', 'scale=32:32', work / 'cover.jpg')
        if not (work / 'cover.jpg').read_bytes().startswith(b'\xff\xd8'):
            raise RuntimeError('FFmpeg cover fixture is not JPEG')

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                body = source.read_bytes()
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *_args):
                pass

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            url = f'http://127.0.0.1:{server.server_port}/fixture.mp4'
            run('aria2', '--no-conf=true', '--all-proxy=', '--http-proxy=', '--https-proxy=',
                '--enable-color=false', '--summary-interval=0', '--dir=' + str(work), '--out=download.mp4', url)
            run('curl', '-q', '--noproxy', '*', '--fail', '--silent', '--output', work / 'curl.mp4', url)
            for file in ('download.mp4', 'curl.mp4'):
                if (work / file).read_bytes() != source.read_bytes():
                    raise RuntimeError(f'{file} fixture content mismatch')
        finally:
            server.shutdown()
            server.server_close()
            thread.join()
    return versions


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('package', type=Path)
    parser.add_argument('--scratch', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    args = parser.parse_args()
    root = args.package.resolve(strict=True)
    inventory = audit(root)
    versions = check_tools(root, args.scratch.resolve(strict=True))
    args.report.write_text(json.dumps({'pe_images': inventory, 'tool_versions': versions,
                                     'offline_tools': 'passed'}, indent=2), encoding='utf-8')
    print(f'PACKAGE_CHECK_OK: {len(inventory)} x64 PE images; dependency closure; FFmpeg merge/audio/cover; aria2/curl loopback download')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Build a relocatable local arm64 .app and drag-install DMG inside this project."""
import argparse
import datetime
import hashlib
import http.server
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[1]
MAGIC = {b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca'}
SYSTEM = ('/System/Library/', '/usr/lib/')
APP_ICON = ROOT / 'app/resources/icons/icon.icns'
MEDIA_TOOLS = {
    'aria2c': Path('/opt/homebrew/bin/aria2c'),
    'ffmpeg': Path('/opt/homebrew/bin/ffmpeg'),
    'curl': Path('/opt/homebrew/opt/curl/bin/curl'),
}


def run(*args, **kwargs):
    try:
        return subprocess.check_output([str(a) for a in args], text=True, stderr=subprocess.STDOUT, **kwargs)
    except subprocess.CalledProcessError as error:
        print(error.output, flush=True)
        raise


def machos(directory):
    files = []
    for path in directory.rglob('*'):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open('rb') as stream:
            if stream.read(4) in MAGIC:
                files.append(path)
    return sorted(files)


def dependencies(path):
    return [line.strip().split(' (compatibility', 1)[0]
            for line in run('otool', '-arch', 'arm64', '-L', path).splitlines()[1:]
            if ' (compatibility' in line]


def rpaths(path):
    return re.findall(r'cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.*?) \(offset',
                      run('otool', '-arch', 'arm64', '-l', path))


def minimum_os(path):
    commands = run('otool', '-arch', 'arm64', '-l', path)
    match = re.search(r'\bminos ([\d.]+)', commands)
    if not match:
        match = re.search(r'cmd LC_VERSION_MIN_MACOSX\n\s+cmdsize \d+\n\s+version ([\d.]+)', commands)
    if not match:
        raise RuntimeError(f'Cannot determine minimum macOS: {path.name}')
    return tuple(int(part) for part in match.group(1).split('.'))


def copy_media(app, library):
    frameworks = app / 'Contents/Frameworks'
    pending = [(library.resolve(), frameworks / library.resolve().name)]
    pending.extend((source.resolve(), app / 'Contents/MacOS' / name)
                   for name, source in MEDIA_TOOLS.items())
    # aria2 explicitly loads OpenSSL's legacy provider via dlopen; otool cannot
    # discover this runtime module as a linked dependency.
    pending.append((Path('/opt/homebrew/opt/openssl@3/lib/ossl-modules/legacy.dylib').resolve(),
                    frameworks / 'ossl-modules/legacy.dylib'))
    copied = {}
    aliases = {}
    formula_roots = set()
    while pending:
        source, target = pending.pop()
        source = source.resolve()
        if source in copied:
            continue
        if target.exists():
            raise RuntimeError(f'Conflicting runtime library name: {source.name}')
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        copied[source] = target
        for parent in source.parents:
            if parent.parent.parent.name == 'Cellar':
                formula_roots.add(parent)
                break
        for dep in dependencies(source):
            if dep.startswith(SYSTEM):
                continue
            if dep.startswith('/'):
                resolved = Path(dep).resolve(strict=True)
            elif dep.startswith('@loader_path/'):
                resolved = (source.parent / dep[len('@loader_path/'):]).resolve(strict=True)
            elif dep.startswith('@rpath/'):
                candidates = []
                for value in rpaths(source):
                    base = (source.parent / value[len('@loader_path/'):]
                            if value.startswith('@loader_path/') else Path(value))
                    if base.is_absolute():
                        candidate = base / dep[len('@rpath/'):]
                        if candidate.is_file():
                            candidates.append(candidate.resolve())
                if len(set(candidates)) != 1:
                    raise RuntimeError(f'Ambiguous or missing dependency in {source.name}: {dep}')
                resolved = candidates[0]
            else:
                raise RuntimeError(f'Unresolved media dependency in {source.name}: {dep}')
            if dep in aliases and aliases[dep] != frameworks / resolved.name:
                raise RuntimeError(f'Conflicting dependency alias: {dep}')
            aliases[dep] = frameworks / resolved.name
            if resolved != source:
                pending.append((resolved, frameworks / resolved.name))
    for source, target in copied.items():
        aliases[str(source)] = target
    return aliases, formula_roots


def relocate(app, aliases):
    frameworks = app / 'Contents/Frameworks'
    executable_dir = app / 'Contents/MacOS'
    for binary in machos(app):
        archs = run('lipo', '-archs', binary).split()
        if 'arm64' not in archs:
            raise RuntimeError(f'Missing arm64: {binary.relative_to(app)}')
        if len(archs) > 1:
            thin = binary.with_name(binary.name + '.arm64-tmp')
            run('lipo', binary, '-thin', 'arm64', '-output', thin)
            thin.replace(binary)
        # Strip inherited signatures before editing load commands; re-sign at the end.
        subprocess.run(['codesign', '--remove-signature', str(binary)], capture_output=True)
        for dep in dependencies(binary):
            if dep.startswith(SYSTEM):
                continue
            target = aliases.get(dep)
            if target is None:
                if dep.startswith('@rpath/'):
                    target = frameworks / dep[len('@rpath/'):]
                elif dep.startswith('@loader_path/'):
                    target = binary.parent / dep[len('@loader_path/'):]
                elif dep.startswith('@executable_path/'):
                    target = executable_dir / dep[len('@executable_path/'):]
                elif '.framework/' in dep:
                    # Qt absolute install names, if any survived macdeployqt.
                    target = frameworks / re.search(r'([^/]+\.framework/.*)', dep).group(1)
                else:
                    target = frameworks / Path(dep).name
            target = target.resolve()
            if not target.is_relative_to(app.resolve()) or not target.is_file():
                raise RuntimeError(f'Unbundled dependency in {binary.name}: {dep}')
            relative = os.path.relpath(target, executable_dir)
            new = '@executable_path/' + relative
            if dep != new:
                run('install_name_tool', '-change', dep, new, binary)
        if binary.suffix == '.dylib' or '.framework/' in str(binary):
            run('install_name_tool', '-id', '@executable_path/' + os.path.relpath(binary, executable_dir), binary)
        for old in rpaths(binary):
            if old.startswith('/') and not old.startswith(SYSTEM):
                run('install_name_tool', '-delete_rpath', old, binary)


def verify_bundle_icon(app):
    contents = app / 'Contents'
    plist = plistlib.loads((contents / 'Info.plist').read_bytes())
    if plist.get('CFBundleIconFile') != APP_ICON.name:
        raise RuntimeError('App bundle does not declare the application icon')
    icon = (contents / 'Resources' / APP_ICON.name).read_bytes()
    if len(icon) <= 8 or icon[:4] != b'icns' or int.from_bytes(icon[4:8], 'big') != len(icon):
        raise RuntimeError('App bundle contains an invalid ICNS icon')
    if icon != APP_ICON.read_bytes():
        raise RuntimeError('App bundle icon differs from the application resource')


def verify(app):
    verify_bundle_icon(app)
    executable_dir = app / 'Contents/MacOS'
    binaries = machos(app)
    max_os = (0,)
    inventory = []
    for binary in binaries:
        if run('lipo', '-archs', binary).strip() != 'arm64':
            raise RuntimeError(f'Unexpected architecture: {binary.name}')
        max_os = max(max_os, minimum_os(binary))
        deps = dependencies(binary)
        for dep in deps:
            if dep.startswith(SYSTEM):
                continue
            if not dep.startswith('@executable_path/'):
                raise RuntimeError(f'Non-relocatable dependency: {dep}')
            resolved = (executable_dir / dep[len('@executable_path/'):]).resolve()
            if not resolved.is_relative_to(app.resolve()) or not resolved.is_file():
                raise RuntimeError(f'Missing bundled dependency: {dep}')
        for value in rpaths(binary):
            if value.startswith('/') and not value.startswith(SYSTEM):
                raise RuntimeError(f'External RPATH: {value}')
        inventory.append({'path': str(binary.relative_to(app)), 'dependencies': deps,
                          'minimum_macos': '.'.join(map(str, minimum_os(binary)))})
    return '.'.join(map(str, max_os)), inventory


def notices(app, qt, formula_roots):
    resources = app / 'Contents/Resources'
    for name in ['LICENSE', 'THIRD_PARTY_NOTICES.md']:
        shutil.copy2(ROOT / name, resources / name)
    shutil.copytree(ROOT / 'licenses', resources / 'licenses')
    qt_notices = resources / 'licenses/Qt'
    qt_notices.mkdir()
    qt_license_dir = qt.parent.parent / 'Licenses'
    if qt_license_dir.is_dir():
        for source in qt_license_dir.iterdir():
            if source.is_file():
                shutil.copy2(source, qt_notices / source.name)
    # Qt's installed SPDX includes individual third-party attributions and versions.
    for source in (qt / 'sbom').glob('*.spdx.json'):
        shutil.copy2(source, qt_notices / source.name)
    manifest = []
    for formula in sorted(formula_roots):
        name, version = formula.parent.name, formula.name
        target = resources / 'licenses/Homebrew' / f'{name}-{version}'
        target.mkdir(parents=True)
        for source in formula.iterdir():
            if source.is_file() and re.match(r'(?i)(licen[cs]e|copying|copyright|notice|authors)', source.name):
                shutil.copy2(source, target / source.name)
        sbom = formula / 'sbom.spdx.json'
        if sbom.exists():
            shutil.copy2(sbom, target / sbom.name)
        recipe = formula / '.brew' / f'{name}.rb'
        if recipe.is_file():
            shutil.copy2(recipe, target / recipe.name)
        receipt = formula / 'INSTALL_RECEIPT.json'
        metadata = json.loads(receipt.read_text()) if receipt.exists() else {}
        source_info = metadata.get('source', {})
        manifest.append({'formula': name, 'version': version,
                         'source': {key: source_info.get(key)
                                    for key in ('tap', 'spec', 'versions', 'tap_git_head')}})
    (resources / 'media-dependencies.json').write_text(json.dumps(manifest, indent=2) + '\n')


def smoke(app, scratch, qt):
    verify_bundle_icon(app)
    # Sandbox makes this an actual check against accidental developer-library fallback.
    forbidden = ['/opt/homebrew', '/usr/local', str(qt), str(ROOT / 'build/bin')]
    profile = '(version 1) (allow default) (deny network*)\n' + ''.join(
        '(deny file-read* (subpath ' + json.dumps(path) + '))\n' for path in forbidden)
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(('QT_', 'QML_', 'DYLD_', 'BBHOUSE_'))}
    env.update(PATH='/usr/bin:/bin:/usr/sbin:/sbin', QT_QPA_PLATFORM='cocoa',
               QT_QUICK_BACKEND='software', QML_DISABLE_DISK_CACHE='1')
    output = run('/usr/bin/sandbox-exec', '-p', profile,
                 app / 'Contents/MacOS/bbhouse-qt', '--deployment-smoke-test',
                 '--scratch-dir', scratch, env=env, cwd=scratch, timeout=45)
    if 'DEPLOYMENT_SMOKE_OK' not in output:
        raise RuntimeError('Bundled deployment smoke did not report success:\n' + output)
    return output


def download_smoke(app, scratch, qt):
    """Exercise real HTTP transfers without external services or user data."""
    payload = b'bbhouse-bundled-download-fixture\n' * 4096

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header('Content-Length', str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)

        def log_message(self, *_):
            pass

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        port = server.server_address[1]
        profile = ('(version 1) (allow default) (deny network*)\n'
                   f'(allow network-outbound (remote ip "localhost:{port}"))\n')
        for path in ['/opt/homebrew', '/usr/local', str(qt), str(ROOT / 'build/bin')]:
            profile += '(deny file-read* (subpath ' + json.dumps(path) + '))\n'
        # A minimal environment prevents proxy, TLS and per-user tool config inheritance.
        env = {'PATH': '/usr/bin:/bin:/usr/sbin:/sbin', 'HOME': str(scratch),
               'XDG_CONFIG_HOME': str(scratch), 'CURL_HOME': str(scratch), 'NO_PROXY': '*',
               'OPENSSL_MODULES': str(app / 'Contents/Frameworks/ossl-modules'),
               'SSL_CERT_FILE': '/etc/ssl/cert.pem', 'SSL_CERT_DIR': '/etc/ssl/certs'}
        url = f'http://127.0.0.1:{port}/fixture'
        commands = {
            'curl': ['-q', '--fail', '--silent', '--noproxy', '*', '--proxy', '',
                     '--max-time', '15', '--output', str(scratch / 'curl-fixture'), url],
            'aria2c': ['--no-conf=true', '--all-proxy=', '--http-proxy=', '--https-proxy=',
                       '--enable-rpc=false', '--file-allocation=none', '--max-tries=1',
                       '--console-log-level=error', '--download-result=hide',
                       '--summary-interval=0', '--dir=' + str(scratch),
                       '--out=aria2c-fixture', url],
        }
        for name, arguments in commands.items():
            run('/usr/bin/sandbox-exec', '-p', profile, app / 'Contents/MacOS' / name,
                *arguments, env=env, cwd=scratch, timeout=25)
            fixture = scratch / f'{name}-fixture'
            if fixture.read_bytes() != payload:
                raise RuntimeError(f'Bundled {name} HTTP fixture mismatch')
            fixture.unlink()
        return 'DOWNLOAD_SMOKE_OK: bundled aria2c/curl loopback HTTP transfers, exact bytes; Homebrew denied\n'
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qt-prefix', type=Path, default=Path.home() / 'Qt/6.11.2/macos')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build')
    parser.add_argument('--skip-build', action='store_true', help='Use already verified Release products')
    parser.add_argument('--clean-old', action='store_true',
                        help='Remove previous macOS packages after the new DMG passes all checks')
    parser.add_argument('--dmgbuild', type=Path, default=ROOT / 'build/macos-package-tools/bin/dmgbuild')
    args = parser.parse_args()
    qt, build = args.qt_prefix.resolve(), args.build_dir.resolve()
    if not build.is_relative_to(ROOT / 'build'):
        raise RuntimeError('Build and temporary files must stay inside project build/')
    if not args.dmgbuild.is_file():
        raise RuntimeError('Install dmgbuild in build/macos-package-tools as documented')
    for name, source in MEDIA_TOOLS.items():
        if not source.is_file():
            raise RuntimeError(f'Install missing media tool with Homebrew: {name}')
    tool_versions = {name: run(source, *({'aria2c': ['--no-conf=true', '--version'],
                        'curl': ['-q', '--version'], 'ffmpeg': ['-version']}[name]))
                     for name, source in MEDIA_TOOLS.items()}
    if '--enable-nonfree' in tool_versions['ffmpeg']:
        raise RuntimeError('Refusing a non-redistributable FFmpeg nonfree build')
    if 'AppleSecTrust' not in tool_versions['curl']:
        raise RuntimeError('Bundled curl must support native macOS certificate trust')
    cmake = Path.home() / 'Qt/Tools/CMake/CMake.app/Contents/bin/cmake'
    if not args.skip_build:
        print('Building Release application and regression targets…', flush=True)
        run(cmake, '-S', ROOT, '-B', build, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
            f'-DCMAKE_PREFIX_PATH={qt}', '-DCMAKE_OSX_ARCHITECTURES=arm64',
            f'-DCMAKE_MAKE_PROGRAM={Path.home() / "Qt/Tools/Ninja/ninja"}')
        run(cmake, '--build', build, '--target', 'bbhouse-qt', 'regression-tests', '-j', '6')
        test_env = {key: value for key, value in os.environ.items()
                    if key not in ('QT_QUICK_BACKEND', 'QSG_RHI_BACKEND')}
        # Each GUI test declares Cocoa itself; do not force software onto its
        # native Qt Quick OpenGL renderer. Other tests keep offscreen defaults.
        result = run(cmake.parent / 'ctest', '--test-dir', build, '--output-on-failure',
                     env={**test_env, 'QT_QPA_PLATFORM': 'offscreen'})
        print(result, flush=True)
    cache = (build / 'CMakeCache.txt').read_text()
    if 'CMAKE_BUILD_TYPE:STRING=Release' not in cache:
        raise RuntimeError('Only Release build products may be packaged')
    version = re.search(r'project\(bbhouse-qt VERSION ([\d.]+)', (ROOT / 'CMakeLists.txt').read_text()).group(1)
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    release = ROOT / 'build/release' / f'bbhouse-qt-macos-arm64_{stamp}'
    old_releases = [path for path in release.parent.glob('bbhouse-qt-macos-arm64_*')
                    if re.fullmatch(r'bbhouse-qt-macos-arm64_\d{8}_\d{6}', path.name)
                    and path.is_dir() and not path.is_symlink()]
    release.mkdir(parents=True, exist_ok=False)
    app = release / 'bbhouse-qt.app'
    contents = app / 'Contents'
    for name in ['MacOS', 'Frameworks', 'Resources']:
        (contents / name).mkdir(parents=True)
    shutil.copy2(build / 'bin/bbhouse-qt', contents / 'MacOS/bbhouse-qt')
    shutil.copy2(APP_ICON, contents / 'Resources' / APP_ICON.name)
    plist = {'CFBundleExecutable': 'bbhouse-qt', 'CFBundleName': 'bbhouse-qt',
             'CFBundleDisplayName': 'bbhouse-qt', 'CFBundleIdentifier': 'io.github.endcloud.bbhouse-qt',
             'CFBundlePackageType': 'APPL', 'CFBundleShortVersionString': version,
             'CFBundleVersion': version, 'CFBundleIconFile': APP_ICON.name,
             'NSHighResolutionCapable': True,
             'NSPrincipalClass': 'NSApplication'}
    (contents / 'Info.plist').write_bytes(plistlib.dumps(plist))
    print('Deploying Qt / QML…', flush=True)
    output = run(qt / 'bin/macdeployqt', app, '-no-codesign', '-verbose=1',
                 f'-qmldir={ROOT / "app/qml"}', f'-qmlimport={build / "bin"}',
                 f'-libpath={build / "bin/FluentUI"}')
    (release / 'qt-deploy.log').write_text(output)
    # Qt deploys every SQL driver, including ones requiring separately licensed
    # external database clients. This application exclusively uses QSQLITE.
    sql_plugins = contents / 'PlugIns/sqldrivers'
    for plugin in sql_plugins.glob('*'):
        if plugin.is_file() and plugin.name != 'libqsqlite.dylib':
            plugin.unlink()
    print('Bundling and relocating media dependencies…', flush=True)
    aliases, formula_roots = copy_media(app, Path('/opt/homebrew/lib/libmpv.2.dylib'))
    relocate(app, aliases)
    minimum, inventory = verify(app)
    plist['LSMinimumSystemVersion'] = minimum
    (contents / 'Info.plist').write_bytes(plistlib.dumps(plist))
    notices(app, qt, formula_roots)
    (contents / 'Resources/media-tool-versions.json').write_text(
        json.dumps(tool_versions, indent=2) + '\n')
    (contents / 'Resources/bundle-dependencies.json').write_text(json.dumps(inventory, indent=2) + '\n')
    commit = run('git', '-C', ROOT, 'rev-parse', 'HEAD').strip()
    dirty = bool(run('git', '-C', ROOT, 'status', '--porcelain').strip())
    description = (f'# bbhouse-qt {version} — macOS arm64\n\n'
                   f'将 bbhouse-qt.app 拖入 Applications。要求 macOS {minimum} 或更新版本（Apple Silicon）。\n\n'
                   '本包包含 Qt、FluentUI、libmpv、aria2c、FFmpeg、curl 和动态依赖，无需 Homebrew 或 Qt SDK。'
                   '采用本地 ad-hoc 签名，未使用 Developer ID，未通过 Apple 公证。\n\n'
                   '本地验收包：现有微软图标字体再分发授权与默认头像授权仍待解决；'
                   '公开发行前还需补齐所带依赖的完整对应源码。不得据此包推断许可审计已完成。'
                   '组件许可证和依赖清单位于应用 Contents/Resources。\n\n'
                   '账号 Cookie 与个人数据库不随包附带；既有设置和数据仍保存在用户目录。'
                   '应用安装后可在首次登录窗口导入 Cookie，设置末尾可重新登录。也可将 bilibili.cookie.txt 放到 '
                   '~/Library/Application Support/shizi/bbhouse-qt/，不要放进 .app 内部。'
                   '若设置了 BBHOUSE_DATA_DIR，则使用该目录。程序不自动复制开发机凭据。\n\n'
                   f'构建基线：{commit}；构建时存在未提交修改：{dirty}。\n')
    (release / '使用说明.md').write_text(description)
    (contents / 'Resources/BUILD-INFO.md').write_text(description)
    print('Signing and validating app…', flush=True)
    for binary in sorted(machos(app), key=lambda p: len(p.parts), reverse=True):
        # codesign treats the main executable as the whole bundle. Sign its
        # sibling helper executables first, then sign the app below.
        if binary == contents / 'MacOS/bbhouse-qt':
            continue
        run('codesign', '--force', '--sign', '-', '--timestamp=none', binary)
    for framework in (contents / 'Frameworks').glob('*.framework'):
        run('codesign', '--force', '--sign', '-', '--timestamp=none', framework)
    run('codesign', '--force', '--sign', '-', '--timestamp=none', app)
    run('codesign', '--verify', '--deep', '--strict', '--verbose=2', app)
    with tempfile.TemporaryDirectory(prefix='macos-deployment-', dir=ROOT / 'build') as temp:
        scratch = Path(temp)
        (release / 'deployment-smoke.log').write_text(smoke(app, scratch, qt))
        (release / 'download-smoke.log').write_text(download_smoke(app, scratch, qt))
        print('Creating drag-install DMG…', flush=True)
        dmg = release / f'bbhouse-qt-{version}-macos-arm64.dmg'
        dmg_env = {**os.environ, 'TMPDIR': str(scratch)}
        run(args.dmgbuild, '-s', ROOT / 'scripts/macos/dmg-settings.py',
            '-D', f'app={app}', '-D', f'readme={release / "使用说明.md"}',
            'bbhouse-qt', dmg, env=dmg_env)
        run('hdiutil', 'verify', dmg)
        mount = scratch / 'mounted'
        mount.mkdir()
        run('hdiutil', 'attach', '-readonly', '-nobrowse', '-mountpoint', mount, dmg)
        try:
            if not (mount / 'Applications').is_symlink() or os.readlink(mount / 'Applications') != '/Applications':
                raise RuntimeError('DMG Applications shortcut is missing')
            mounted_app = mount / app.name
            run('codesign', '--verify', '--deep', '--strict', mounted_app)
            # Model drag-install by copying off the read-only volume before executing.
            installed = scratch / 'relocated-apps' / app.name
            installed.parent.mkdir()
            run('ditto', mounted_app, installed)
            (release / 'relocated-smoke.log').write_text(smoke(installed, scratch, qt))
            (release / 'relocated-download-smoke.log').write_text(download_smoke(installed, scratch, qt))
        finally:
            run('hdiutil', 'detach', mount)
    digest = hashlib.sha256(dmg.read_bytes()).hexdigest()
    (release / 'SHA256SUMS').write_text(f'{digest}  {dmg.name}\n')
    if args.clean_old:
        for previous in old_releases:
            shutil.rmtree(previous)
        print(f'Cleaned previous macOS package directories: {len(old_releases)}', flush=True)
    print(f'APP: {app}\nDMG: {dmg}\nMinimum macOS: {minimum}\nMach-O files: {len(inventory)}', flush=True)


if __name__ == '__main__':
    main()

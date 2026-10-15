"""Fetch pinned upstream sources. No runtime downloads or bundled Zygisk runtime."""
import hashlib, io, json, pathlib, tarfile, urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / 'third_party'
def get(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'GlassMic-build/0.1'})
    return urllib.request.urlopen(req, timeout=90).read()
def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)
def main():
    DEST.mkdir(exist_ok=True)
    lock_path = ROOT / 'dependencies.lock.json'
    if lock_path.exists():
        lock = json.loads(lock_path.read_text(encoding='utf-8'))
    else:
        lock = {'zygisk_commit': '7bb941ac8edfcffd1d23761e401c45ca95409dc1', 'miniaudio_tag': '0.11.23', 'shadowhook_tag': 'v1.0.10', 'sha256': {}}
    lock.setdefault('kernelsu_js','3.0.2')
    urls = {
        'zygisk.hpp': 'https://raw.githubusercontent.com/topjohnwu/zygisk-module-sample/'+lock['zygisk_commit']+'/module/jni/zygisk.hpp',
        'miniaudio.h': 'https://raw.githubusercontent.com/mackron/miniaudio/'+lock['miniaudio_tag']+'/miniaudio.h',
        'shadowhook.tar.gz': 'https://codeload.github.com/bytedance/android-inline-hook/tar.gz/refs/tags/'+lock['shadowhook_tag'],
        'kernelsu.tgz': 'https://registry.npmjs.org/kernelsu/-/kernelsu-'+lock['kernelsu_js']+'.tgz',
    }
    for name, url in urls.items():
        cached = DEST / (('shadowhook-'+lock['shadowhook_tag']+'.tar.gz') if name=='shadowhook.tar.gz' else name)
        data = cached.read_bytes() if cached.exists() else get(url)
        digest = hashlib.sha256(data).hexdigest()
        expected = lock['sha256'].get(name)
        if expected and digest != expected:
            raise RuntimeError('Source hash mismatch: '+name)
        lock['sha256'][name] = digest
        write(cached, data)
        if name.endswith('.tar.gz'):
            with tarfile.open(fileobj=io.BytesIO(data)) as archive:
                prefix = 'android-inline-hook-'+lock['shadowhook_tag'].lstrip('v')+'/'
                for member in archive.getmembers():
                    rel = member.name.removeprefix(prefix)
                    if member.isfile() and (rel.startswith('shadowhook/src/main/cpp/') or rel == 'LICENSE'):
                        target = DEST / ('shadowhook-'+lock['shadowhook_tag']) / rel
                        if not target.resolve().is_relative_to(DEST.resolve()):
                            raise RuntimeError('Unsafe archive path')
                        write(target, archive.extractfile(member).read())
                        if rel=='LICENSE':write(ROOT/'module/licenses/shadowhook-MIT.txt',archive.extractfile(member).read())
        if name == 'kernelsu.tgz':
            with tarfile.open(fileobj=io.BytesIO(data)) as archive:
                write(ROOT/'module/webroot/vendor/index.js', archive.extractfile('package/index.js').read())
    lock_path.write_text(json.dumps(lock, indent=2)+'\n', encoding='utf-8')
    print('Verified pinned dependencies:', ', '.join(urls))
if __name__ == '__main__': main()

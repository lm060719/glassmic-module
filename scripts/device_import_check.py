"""Copy/decode fixtures in private test locations on an explicitly selected device."""
import argparse, hashlib, json, os, pathlib, shlex, time
parser=argparse.ArgumentParser()
parser.add_argument('--serial',required=True)
args=parser.parse_args()
os.environ['GLASSMIC_SERIAL']=args.serial
from device_check import ROOT, REMOTE, adb, root

source='/sdcard/Download/.glassmic-import-check-20261014-'+str(os.getpid())
state=REMOTE+'/state'
ctl=REMOTE+'/glassmicctl --data '+shlex.quote(state)
fixtures=[('flac16.flac','16-bit.flac'),('flac24.flac',"24-bit's.FLAC"),('flac192k.flac','192k.flac'),('long.mp3','large.mp3')]
created=[];imports=[];results=[]
try:
    root('mkdir -p '+shlex.quote(source))
    for local_name,remote_name in fixtures:
        local=ROOT/'build/decoder-host'/local_name
        if not local.is_file():raise RuntimeError('Run scripts/host_decoder_check.ps1 first')
        remote=source+'/'+remote_name
        adb('push',str(local),remote);created.append(remote)
        started=time.perf_counter()
        result=json.loads(root(ctl+' import '+shlex.quote(remote)))
        wall_ms=round((time.perf_counter()-started)*1000,2)
        assert result['ok'],result
        imports.append(result['file'])
        destination=state+'/media/'+result['file']
        expected=hashlib.sha256(local.read_bytes()).hexdigest()
        copied=root('sha256sum '+shlex.quote(destination)).split()[0]
        assert copied==expected,'Copied file hash mismatch'
        assert result['bytes']==local.stat().st_size
        decode=root(REMOTE+'/glassmic-selftest '+shlex.quote(destination))
        assert 'ALL TESTS PASSED' in decode
        row={'source':remote_name,'bytes':result['bytes'],'nativeMs':result['elapsedMs'],'adbWallMs':wall_ms,'sha256Matched':True,'decodeAndSeek':True}
        results.append(row);print(json.dumps(row,ensure_ascii=False),flush=True)
    listing=json.loads(root(ctl+' browse '+shlex.quote(source)))
    assert {r[1] for r in fixtures}=={e['name'] for e in listing['entries']}
    duplicate=json.loads(root(ctl+' import '+shlex.quote(created[0])))
    imports.append(duplicate['file'])
    assert duplicate['file']!=imports[0],'Duplicate import overwrote original'
    for path in created:root('test -f '+shlex.quote(path))
    rejected=root(ctl+' import /system/build.prop',check=False)
    assert not json.loads(rejected)['ok'],'File browser allowed a system path'
    evidence=ROOT/'device-evidence/import-regression.json'
    evidence.write_text(json.dumps({'fixtures':results,'duplicatePreserved':True,'sourcesPreserved':True,'flacVisible':True,'systemPathRejected':True},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('PASS: direct import, FLAC listing, decoding, hashes, duplicates, source preservation and storage boundary',flush=True)
finally:
    # Delete only exact files created by this run, with no recursive deletion.
    for path in created:
        assert pathlib.PurePosixPath(path).parent==pathlib.PurePosixPath(source)
        root('rm -f '+shlex.quote(path),check=False)
    root('rmdir '+shlex.quote(source),check=False)
    for name in imports:
        assert '/' not in name and name not in ('.','..')
        root('rm -f '+shlex.quote(state+'/media/'+name),check=False)

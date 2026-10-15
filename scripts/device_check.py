"""Read-only device discovery and isolated native tests; no UI/service/zygote restart."""
import json, math, os, pathlib, re, shlex, shutil, struct, subprocess, sys, wave
ROOT=pathlib.Path(__file__).resolve().parents[1]
ADB=os.environ.get('GLASSMIC_ADB',shutil.which('adb') or r'E:\platform-tools\adb.exe')
SERIAL=os.environ.get('GLASSMIC_SERIAL')
if not SERIAL:
    devices=subprocess.check_output([ADB,'devices'],text=True,encoding='utf-8')
    online=[line.split()[0] for line in devices.splitlines() if len(line.split())>=2 and line.split()[1]=='device']
    if len(online)!=1:raise RuntimeError('Connect exactly one device or set GLASSMIC_SERIAL')
    SERIAL=online[0]
REMOTE='/data/local/tmp/glassmic-dev-20261013'
def adb(*args,check=True):
    p=subprocess.run([ADB,'-s',SERIAL,*args],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=100)
    if check and p.returncode: raise RuntimeError(p.stdout+p.stderr)
    return p.stdout.strip()
def root(command,check=True):
    return adb('shell','su -c '+shlex.quote(command),check=check)
def stage(command,evidence,name,failures):
    """Runs one root test stage; records its output and keeps going on failure."""
    p=subprocess.run([ADB,'-s',SERIAL,'shell','su -c '+shlex.quote(command)],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=100)
    report=(p.stdout+p.stderr).strip()
    print(report);(evidence/name).write_text(report+'\n',encoding='utf-8')
    if p.returncode:
        failures.append(name+' (exit '+str(p.returncode)+')');print('STAGE FAILED: '+name)
def main():
    evidence=ROOT/'device-evidence'/re.sub(r'[^\w.-]','_',SERIAL);evidence.mkdir(parents=True,exist_ok=True)
    failures=[]
    root('if [ -x '+REMOTE+'/glassmicctl ]; then '+REMOTE+'/glassmicctl --data '+REMOTE+'/state stop; fi',check=False)
    import time
    time.sleep(.2)
    adb('shell','mkdir','-p',REMOTE)
    for name in ('glassmicd','glassmicctl','glassmic-selftest','libglassmic.so','libglassmic-backend.so','libshadowhook_nothing.so'):
        adb('push',str(ROOT/'build/native'/name),REMOTE+'/'+name)
    # The SELinux probe maps the helper library in the zygote domain, as Zygisk
    # does with module files; a system library label allows that for the test copy.
    root('chcon u:object_r:system_lib_file:s0 '+REMOTE+'/libshadowhook_nothing.so')
    # Adds only the module's dedicated shared-file type and permissions; leaves
    # Android Enforcing and never relabels/restarts the existing live daemon.
    policy=(ROOT/'module/sepolicy.rule').read_text(encoding='utf-8')
    rule=ROOT/'build/sepolicy.rule'
    if root('test -x /data/adb/ksud && echo ksu',check=False)=='ksu':
        rule.write_text(policy,encoding='utf-8',newline='\n')
        root('/data/adb/ksud sepolicy check '+shlex.quote(policy))
        adb('push',str(rule),REMOTE+'/sepolicy.rule')
        root('/data/adb/ksud sepolicy apply '+REMOTE+'/sepolicy.rule')
    else:
        # Magisk test devices: root runs as the magisk domain instead of ksu. The
        # SELinux probe also hands FDs to its zygote child over a magisk-owned
        # socketpair, which ksu already permits; this line is test-only.
        rule.write_text(re.sub(r'\bksu\b','magisk',policy).rstrip('\n')+'\nallow zygote magisk unix_stream_socket { read write getattr }\n',encoding='utf-8',newline='\n')
        adb('push',str(rule),REMOTE+'/sepolicy.rule')
        root('magiskpolicy --live --apply '+REMOTE+'/sepolicy.rule')
    tone=ROOT/'build/test-tone.wav'
    with wave.open(str(tone),'wb') as wav:
        wav.setparams((1,2,48000,0,'NONE','NONE'))
        wav.writeframes(b''.join(struct.pack('<h',int(9000*math.sin(2*math.pi*440*n/48000))) for n in range(48000*3)))
    adb('push',str(tone),REMOTE+'/test-tone.wav')
    root('chmod 755 '+REMOTE+'/glassmicd '+REMOTE+'/glassmicctl '+REMOTE+'/glassmic-selftest')
    stage(REMOTE+'/glassmic-selftest '+REMOTE+'/test-tone.wav '+REMOTE+'/libglassmic-backend.so',evidence,'native-selftest.txt',failures)
    state=REMOTE+'/state'
    root('mkdir -p '+state+'/media')
    root('cp '+REMOTE+'/test-tone.wav '+state+'/media/test.wav')
    # Starts only our development daemon, with a private data path.
    root(REMOTE+'/glassmicd --data '+state+' > '+REMOTE+'/daemon.log 2>&1 < /dev/null &')
    import time
    time.sleep(.3)
    ctl=REMOTE+'/glassmicctl --data '+state
    for args in ['status','load test.wav','gain -6','speed 1.25','loop 1','targets io.mo.glassmic.probe','play','pause','silence','real']:
        result=json.loads(root(ctl+' '+args))
        if not result.get('ok'):raise RuntimeError(result)
        print('PASS: daemon command '+args)
    # A later integration test can attach simulated consumers to this daemon.
    snapshot=json.loads(root(ctl+' status'))
    (evidence/'daemon-status.json').write_text(json.dumps(snapshot,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    try:
        stage(REMOTE+'/glassmic-selftest --selinux '+state+' '+REMOTE+'/libglassmic-backend.so',evidence,'native-selinux.txt',failures)
        stage(REMOTE+'/glassmic-selftest --integration '+state+' '+REMOTE+'/glassmicd',evidence,'native-integration.txt',failures)
    finally:
        # Integration stops the daemon itself; this covers a failed stage.
        root(ctl+' stop',check=False)
    print('Isolated daemon stopped. Evidence: '+str(evidence))
    if failures:raise SystemExit('FAILED stages: '+', '.join(failures))
    print('ALL DEVICE STAGES PASSED')
if __name__=='__main__': main()

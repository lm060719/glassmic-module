import hashlib, json, pathlib, zipfile
root=pathlib.Path(__file__).resolve().parents[1]
stage=root/'build/module'
dist=root/'dist';dist.mkdir(exist_ok=True)
properties=dict(line.split('=',1) for line in (stage/'module.prop').read_text(encoding='utf-8').splitlines() if '=' in line)
target=dist/('glassmic-'+properties['version']+'-arm64.zip')
with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as out:
    for path in sorted(stage.rglob('*')):
        if path.is_file():
            name=path.relative_to(stage).as_posix()
            entry=zipfile.ZipInfo(name)
            entry.create_system=3
            entry.external_attr=((0o100755 if name.endswith('.sh') or (name.startswith('bin/') and not name.endswith('.jar')) else 0o100644)<<16)
            entry.compress_type=zipfile.ZIP_DEFLATED
            payload=path.read_bytes()
            if name in ('module.prop','sepolicy.rule','targets.txt') or name.endswith('.sh'):
                normalized=payload.replace(b'\r\n',b'\n').replace(b'\r',b'\n')
                if normalized!=payload:path.write_bytes(normalized)
                payload=normalized
            out.writestr(entry,payload)
digest=hashlib.sha256(target.read_bytes()).hexdigest()
(target.with_suffix('.zip.sha256')).write_text(digest+'  '+target.name+'\n')
print(json.dumps({'zip':str(target),'bytes':target.stat().st_size,'sha256':digest}))

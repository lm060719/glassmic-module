import { exec } from './vendor/index.js';
const MODULE='/data/adb/modules/glassmic';
const CTL=MODULE+'/bin/glassmicctl';
const $=id=>document.getElementById(id);
const quote=value=>"'"+String(value).replaceAll("'","'\\''")+"'";
let state=null, busy=false, refreshing=false, connected=false, apps=[], chosen=new Set(), targetsDirty=false, seeking=false;
let filesKey='', targetsKey='', messageTimer;
let positionAnchor=0, lastProgressPaint=0, browser=null, browserLoading=false;
let actionRevision=0;
const importChoices=new Map();
function notify(text,error=false) {
  clearTimeout(messageTimer);$('message').textContent=text;$('message').hidden=false;$('message').classList.toggle('error',error);
  if(!error)messageTimer=setTimeout(()=>$('message').hidden=true,5000);
}
async function shell(command) {
  const result=await exec(command);
  let data;
  try { data=JSON.parse(result.stdout.trim().split('\n').filter(line=>line.startsWith('{')).at(-1)); }
  catch { throw new Error(result.stderr?.trim()||'模块命令没有返回有效数据'); }
  if(!data.ok)throw new Error(data.error||'操作失败');
  if(result.errno!==0)throw new Error(result.stderr?.trim()||data.error||'操作失败');
  return data;
}
async function command(...args) {
  const result=await shell([CTL,...args].map(quote).join(' '));
  if(result.mode!==undefined)connected=true;
  return result;
}
async function action(task) {
  if(busy)return;
  busy=true;++actionRevision;document.querySelectorAll('button').forEach(b=>b.disabled=true);syncBrowserControls();
  try {await task();await refresh();}
  catch(e){notify(e.message,true);}
  finally {busy=false;document.querySelectorAll('button').forEach(b=>b.disabled=false);if(state)render(state);syncBrowserControls();}
}
function time(ms){const n=Math.max(0,Math.floor(ms/1000));return String(Math.floor(n/60)).padStart(2,'0')+':'+String(n%60).padStart(2,'0');}
function render(s) {
  positionAnchor=performance.now();
  $('connection').textContent=connected?'服务已连接':'服务未连接';$('connection').className=connected?'badge online':'badge error';
  $('source-mode').textContent=['真实麦克风','音频文件','静音输入'][s.mode]||'未知状态';
  $('track-name').textContent=s.selected||'选择一段声音';
  $('track-description').textContent=s.mode===0?'当前使用手机的真实麦克风':s.mode===2?'目标应用收到静音':s.paused?'已暂停 · 点击播放开始供音':'正在播放 · 目标应用录音时接收当前进度的声音';
  const reads=s.clients.reduce((sum,c)=>sum+c.reads,0);
  $('activity').textContent=s.clients.length?s.clients.length+' 个后端 · '+reads+' 次读取':'等待目标应用启动';
  $('level-fill').style.width=Math.min(100,Math.max(0,s.peak*100))+'%';
  if(!seeking){$('seek').max=Math.max(1,s.durationMs);$('seek').value=s.positionMs;}
  $('seek').disabled=!s.selected||busy||!connected;
  $('position').textContent=time(s.positionMs);$('duration').textContent=time(s.durationMs);
  $('play').textContent=s.mode===1&&!s.paused?'暂停':'播放';$('play').setAttribute('aria-label',$('play').textContent);
  $('play').disabled=!s.selected||busy||!connected;
  if(document.activeElement!==$('loop'))$('loop').checked=s.loop;
  for(const [id,value] of [['gain',s.gainDb],['speed',s.speed]]) {
    if(document.activeElement!==$(id))$(id).value=value;
  }
  $('gain-value').textContent=Math.round(Number($('gain').value))+' dB';
  $('speed-value').textContent=Number($('speed').value).toFixed(2)+'×';
  const nextTargets=JSON.stringify(s.targets);
  if(!targetsDirty&&nextTargets!==targetsKey){targetsKey=nextTargets;chosen=new Set(s.targets);$('target-packages').value=s.targets.join('\n');renderApps();}
  const key=JSON.stringify([s.files,s.selected]);
  if(key!==filesKey){filesKey=key;renderFiles(s.files,s.selected);}
  $('diagnostics').textContent='版本：'+s.version+'\n已连接后端：'+s.clients.length+
    '\n'+s.clients.map(c=>c.package+' · PID '+c.pid+'\n  Hook '+c.hooks+' · 读取 '+c.reads+' · 欠载 '+c.missing+' 帧 · 跳帧 '+c.skipped+
      (c.stage?'\n  '+(['','已连接，等待初始化','正在初始化','后端已初始化','初始化失败'][c.stage]||'未知阶段')+' · AudioRecord '+(c.metadataReady?'就绪':'未就绪'):'')+
      (c.hookError?' · 错误码 '+c.hookError:'')).join('\n');
}
function paintProgress(now) {
  if(state&&connected&&!document.hidden&&!seeking&&now-lastProgressPaint>=100) {
    lastProgressPaint=now;
    let position=state.positionMs;
    if(state.mode===1&&!state.paused){position+=(now-positionAnchor)*state.speed;
      if(state.loop&&state.durationMs>0)position%=state.durationMs;}
    position=Math.min(Math.max(0,position),state.durationMs||0);
    $('seek').value=position;$('position').textContent=time(position);
    $('seek').setAttribute('aria-valuetext',time(position)+' / '+time(state.durationMs));
  }
}
function renderFiles(files,selected) {
  const container=$('files');container.replaceChildren();
  if(!files.length) {
    const empty=document.createElement('div');empty.className='empty card';
    const symbol=document.createElement('div');symbol.className='empty-symbol';symbol.textContent='♫';
    const title=document.createElement('h2');title.textContent='音频库还是空的';
    const hint=document.createElement('p');hint.className='muted';hint.textContent='导入一段音频，或试试文字转语音。';
    empty.append(symbol,title,hint);container.append(empty);return;
  }
  for(const file of [...files].sort((a,b)=>a.name.localeCompare(b.name))) {
    const row=document.createElement('div');row.className='file-row card'+(file.name===selected?' selected':'');
    const info=document.createElement('div');info.className='file-info';
    const name=document.createElement('div');name.className='file-name';name.textContent=file.name;
    const size=document.createElement('div');size.className='file-size';size.textContent=(file.bytes/1024/1024).toFixed(2)+' MB';
    info.append(name,size);
    const select=document.createElement('button');select.textContent=file.name===selected?'已选择':'选择';
    select.onclick=()=>action(async()=>{state=await command('load',file.name);notify('已选择音频，点击播放开始');});
    const remove=document.createElement('button');remove.className='delete';remove.textContent='×';remove.setAttribute('aria-label','删除 '+file.name);
    remove.onclick=()=>action(async()=>{state=await command('delete',file.name);filesKey='';notify('已删除模块中的音频副本');});
    row.append(info,select,remove);container.append(row);
  }
}
function renderApps() {
  const term=$('app-search').value.trim().toLowerCase();const list=$('apps');list.replaceChildren();
  const packages=[...new Set([...apps,...chosen])].sort().filter(p=>p.toLowerCase().includes(term));
  for(const pkg of packages) {
    const label=document.createElement('label');label.className='app-item';
    const check=document.createElement('input');check.type='checkbox';check.checked=chosen.has(pkg);
    check.onchange=()=>{targetsDirty=true;check.checked?chosen.add(pkg):chosen.delete(pkg);$('target-packages').value=[...chosen].join('\n');};
    const span=document.createElement('span');span.textContent=pkg;label.append(check,span);list.append(label);
  }
  if(!packages.length){const p=document.createElement('p');p.className='muted';p.textContent='没有匹配的应用，可以在下方手动填写包名。';list.append(p);}
}
async function refresh() {
  if(refreshing||document.hidden)return;
  refreshing=true;const revision=actionRevision;
  try {const next=await command('status');if(revision===actionRevision){state=next;render(state);}}
  catch(e){connected=false;$('connection').textContent='服务未连接';$('connection').className='badge error';$('play').disabled=true;$('seek').disabled=true;
    if(!state){$('files').replaceChildren();notify('音频服务尚未启动。安装后需要重启一次；如已重启，请检查模块是否启用。'+e.message,true);}}
  finally{refreshing=false;}
}
async function upload(files) {
  $('upload-progress').hidden=false;
  try {
    for(const file of files) {
      if(file.size<=0||file.size>512*1024*1024)throw new Error('文件大小必须在 1 B 到 512 MB 之间');
      if(!/\.(wav|mp3|flac)$/i.test(file.name))throw new Error('当前支持 WAV、MP3 和 FLAC');
      let name=file.name.replace(/[\\/\x00-\x1f\x7f]/g,'_');
      if(new TextEncoder().encode(name).length>160)name=Date.now()+'.'+file.name.split('.').at(-1);
      if(state?.files.some(f=>f.name===name))name=Date.now()+'-'+name;
      const chunk=81920,started=performance.now();
      for(let offset=0;offset<file.size;offset+=chunk) {
        const bytes=new Uint8Array(await file.slice(offset,offset+chunk).arrayBuffer());
        let binary='';for(const byte of bytes)binary+=String.fromCharCode(byte);
        await command('upload',name,offset,btoa(binary));
        const progress=Math.round(Math.min(file.size,offset+bytes.length)/file.size*100);
        const copied=Math.min(file.size,offset+bytes.length),seconds=(performance.now()-started)/1000;
        $('upload-label').textContent='正在导入 '+file.name+' · '+progress+'% · '+(copied/1024/Math.max(.001,seconds)).toFixed(0)+' KB/s';
        $('upload-progress').querySelector('progress').value=progress;
      }
      await command('commit',name);filesKey='';
    }
    notify('音频已保存到模块音频库');
  } finally {$('upload-progress').hidden=true;$('file-input').value='';}
}
function syncBrowserControls() {
  $('browser-up').disabled=busy||browserLoading||!browser?.parent;
  $('browser-import').disabled=busy||browserLoading||!importChoices.size;
  $('browser-import').textContent=importChoices.size?'导入 '+importChoices.size+' 个音频':'导入所选音频';
  if(busy&&$('file-browser').open)$('browser-import').textContent='正在导入…';
  $('browser-close').disabled=busy;
  $('system-import').disabled=busy||browserLoading;
  document.querySelectorAll('[data-folder]').forEach(b=>b.disabled=busy||browserLoading);
  document.querySelectorAll('.browser-entry input').forEach(input=>input.disabled=busy||browserLoading);
}
function renderBrowser() {
  const list=$('browser-entries');list.replaceChildren();
  const term=$('browser-search').value.trim().toLowerCase();
  const entries=(browser?.entries||[]).filter(e=>e.name.toLowerCase().includes(term));
  for(const entry of entries) {
    const path=browser.path+'/'+entry.name;
    const row=document.createElement(entry.directory?'button':'label');row.className='browser-entry';
    if(entry.directory){row.textContent='文件夹 · '+entry.name;row.disabled=busy||browserLoading;row.onclick=()=>openFolder(path);}
    else {
      const check=document.createElement('input');check.type='checkbox';check.checked=importChoices.has(path);check.disabled=busy;
      check.onchange=()=>{check.checked?importChoices.set(path,entry):importChoices.delete(path);syncBrowserControls();};
      const name=document.createElement('span');name.textContent=entry.name;
      const size=document.createElement('small');size.textContent=(entry.bytes/1024/1024).toFixed(2)+' MB';
      name.append(size);row.append(check,name);
    }
    list.append(row);
  }
  if(!entries.length){const hint=document.createElement('p');hint.className='muted';hint.textContent=term?'没有匹配的文件':'这里没有音频，可进入文件夹或使用系统文件选择器。';list.append(hint);}
}
async function openFolder(path) {
  if(busy||browserLoading)return;
  browserLoading=true;$('browser-status').textContent='正在读取文件夹…';syncBrowserControls();
  $('browser-entries').replaceChildren();
  try {
    browser=await command('browse',path);$('browser-path').textContent=browser.path;$('browser-search').value='';
    $('browser-status').textContent=browser.truncated?'文件过多，仅显示前 10000 项。':'勾选音频后点击导入，可跨文件夹选择。';
  } catch(e){$('browser-status').textContent=e.message;}
  finally{browserLoading=false;renderBrowser();syncBrowserControls();}
}
async function importLocalFiles() {
  const files=[...importChoices.entries()];$('upload-progress').hidden=false;
  const progress=$('upload-progress').querySelector('progress');progress.removeAttribute('value');
  try {
    for(let i=0;i<files.length;++i) {
      const [path,file]=files[i];$('upload-label').textContent='正在复制 '+file.name+' · '+(i+1)+' / '+files.length;
      $('browser-status').textContent=$('upload-label').textContent;
      await command('import',path);importChoices.delete(path);filesKey='';
    }
    $('file-browser').close();notify('已直接复制到音频库，原文件已保留');
  } catch(e){$('browser-status').textContent=e.message;throw e;}
  finally{$('upload-progress').hidden=true;progress.value=0;renderBrowser();syncBrowserControls();}
}
document.querySelectorAll('[data-tab]').forEach(button=>button.onclick=()=>{
  document.querySelectorAll('[data-tab]').forEach(b=>{const active=b===button;b.classList.toggle('active',active);b.setAttribute('aria-selected',String(active));});
  document.querySelectorAll('.tab-panel').forEach(panel=>panel.hidden=panel.id!==button.dataset.tab);
});
$('play').onclick=()=>action(async()=>{state=await command(state.mode===1&&!state.paused?'pause':'play');});
$('real').onclick=()=>action(async()=>{state=await command('real');});
$('silence').onclick=()=>action(async()=>{state=await command('silence');});
$('loop').onchange=()=>action(async()=>{state=await command('loop',$('loop').checked?1:0);});
$('seek').oninput=()=>{seeking=true;$('position').textContent=time(Number($('seek').value));};
$('seek').onchange=()=>action(async()=>{try{state=await command('seek',$('seek').value);}finally{seeking=false;}});
$('gain').oninput=()=>$('gain-value').textContent=$('gain').value+' dB';
$('speed').oninput=()=>$('speed-value').textContent=Number($('speed').value).toFixed(2)+'×';
for(const control of ['gain','speed'])$(control).onchange=()=>action(async()=>{state=await command(control,$(control).value);});
$('import').onclick=()=>{importChoices.clear();$('file-browser').showModal();openFolder(browser?.path||'/sdcard/Download');};
$('browser-close').onclick=()=>$('file-browser').close();
$('file-browser').oncancel=e=>{if(busy)e.preventDefault();};
$('browser-up').onclick=()=>{if(browser?.parent)openFolder(browser.parent);};
$('browser-search').oninput=renderBrowser;
document.querySelectorAll('[data-folder]').forEach(b=>b.onclick=()=>openFolder(b.dataset.folder));
$('system-import').onclick=()=>{$('file-browser').close();$('file-input').click();};
$('browser-import').onclick=()=>action(importLocalFiles);
$('file-input').onchange=()=>action(()=>upload([...$('file-input').files]));
$('app-search').oninput=renderApps;
$('target-packages').oninput=()=>{targetsDirty=true;chosen=new Set($('target-packages').value.split(/\s+/).filter(Boolean));renderApps();};
$('save-targets').onclick=()=>action(async()=>{
  const names=[...chosen];if(names.some(p=>!/^([a-zA-Z0-9_]+\.)+[a-zA-Z0-9_]+$/.test(p)))throw new Error('请填写有效的应用包名');
  state=await command('targets',...names);targetsDirty=false;notify('生效应用已保存');
});
$('synthesize').onclick=()=>action(async()=>{
  const text=$('speech-text').value.trim();if(!text)throw new Error('请先输入文字');
  $('synthesize').textContent='正在生成语音…';
  try {
    const data=await shell([MODULE+'/bin/tts.sh',text,$('language').value,$('speech-rate').value,1].map(quote).join(' '));
    state=await command('load',data.file);filesKey='';notify('语音已生成并选中，点击播放即可使用');
  } finally {$('synthesize').textContent='生成语音';}
});
if(typeof window.ksu==='undefined') {
  $('connection').textContent='请从 KernelSU 打开';$('connection').className='badge error';
  $('files').replaceChildren();notify('这是 KernelSU 模块内的控制页面，请在管理器中打开璃麦的 WebUI。',true);
  document.querySelectorAll('button,input,textarea,select').forEach(element=>element.disabled=true);
} else {
  refresh();
  setInterval(()=>paintProgress(performance.now()),100);
  exec('pm list packages -3').then(result=>{
    apps=result.stdout.split('\n').filter(line=>line.startsWith('package:')).map(line=>line.slice(8).trim());renderApps();
  }).catch(()=>renderApps());
  setInterval(()=>{if(!busy)refresh();},1000);
  document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh();});
}

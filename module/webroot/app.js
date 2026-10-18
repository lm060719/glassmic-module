import { exec, getPackagesInfo } from './vendor/index.js';
import { t, say, sayRaw, translateError, applyTranslations, setLanguage, currentLanguage } from './i18n.js';
const MODULE='/data/adb/modules/glassmic';
const CTL=MODULE+'/bin/glassmicctl';
const MEDIA_CARD=MODULE+'/bin/media-card.sh';
const $=id=>document.getElementById(id);
const quote=value=>"'"+String(value).replaceAll("'","'\\''")+"'";
let state=null, busy=false, refreshing=false, connected=false, apps=[], chosen=new Set(), targetsDirty=false, seeking=false;
let filesKey='', targetsKey='', messageTimer;
// App names from the KernelSU manager; packages it does not know keep showing their package name.
const labels=new Map(), labelLookups=new Set();
let positionAnchor=0, lastProgressPaint=0, browser=null, browserLoading=false;
let actionRevision=0;
const importChoices=new Map();
// With args (null for none), text is a translation key and follows language changes.
function notify(text,error=false,args) {
  clearTimeout(messageTimer);if(args!==undefined)say($('message'),text,args);else sayRaw($('message'),text);$('message').hidden=false;$('message').classList.toggle('error',error);
  if(!error)messageTimer=setTimeout(()=>$('message').hidden=true,5000);
}
async function shell(command) {
  const result=await exec(command);
  let data;
  try { data=JSON.parse(result.stdout.trim().split('\n').filter(line=>line.startsWith('{')).at(-1)); }
  catch { throw new Error(result.stderr?.trim()||t('badData')); }
  if(!data.ok)throw new Error(translateError(data.error)||t('failed'));
  if(result.errno!==0)throw new Error(result.stderr?.trim()||translateError(data.error)||t('failed'));
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
  say($('connection'),connected?'connected':'disconnected');$('connection').className=connected?'badge online':'badge error';
  say($('source-mode'),['modeReal','modeFile','modeSilence'][s.mode]||'modeUnknown');
  if(s.selected)sayRaw($('track-name'),s.selected);else say($('track-name'),'pickSound');
  say($('track-description'),s.mode===0?'descReal':s.mode===2?'descSilence':s.paused?'descPaused':'descPlaying');
  const reads=s.clients.reduce((sum,c)=>sum+c.reads,0);
  if(s.clients.length)say($('activity'),'activity',{n:s.clients.length,reads});else say($('activity'),'waitingApps');
  $('level-fill').style.width=Math.min(100,Math.max(0,s.peak*100))+'%';
  if(!seeking){$('seek').max=Math.max(1,s.durationMs);$('seek').value=s.positionMs;}
  $('seek').disabled=!s.selected||busy||!connected;
  $('position').textContent=time(s.positionMs);$('duration').textContent=time(s.durationMs);
  say($('play'),s.mode===1&&!s.paused?'pause':'play');$('play').setAttribute('aria-label',$('play').textContent);
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
  sayRaw($('diagnostics'),t('diagVersion',{version:s.version})+'\n'+t('diagBackends',{n:s.clients.length})+
    '\n'+s.clients.map(c=>t('diagClient',{pkg:c.package,pid:c.pid,hooks:c.hooks,reads:c.reads,missing:c.missing,skipped:c.skipped})+
      (c.stage?t('diagStage',{stage:t(c.stage>=1&&c.stage<=4?'stage'+c.stage:'stageUnknown'),ready:t(c.metadataReady?'ready':'notReady')}):'')+
      (c.hookError?t('diagError',{code:c.hookError}):'')).join('\n'));
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
    const title=document.createElement('h2');title.textContent=t('emptyLibrary');
    const hint=document.createElement('p');hint.className='muted';hint.textContent=t('emptyHint');
    empty.append(symbol,title,hint);container.append(empty);return;
  }
  for(const file of [...files].sort((a,b)=>a.name.localeCompare(b.name))) {
    const row=document.createElement('div');row.className='file-row card'+(file.name===selected?' selected':'');
    const info=document.createElement('div');info.className='file-info';
    const name=document.createElement('div');name.className='file-name';name.textContent=file.name;
    const size=document.createElement('div');size.className='file-size';size.textContent=(file.bytes/1024/1024).toFixed(2)+' MB';
    info.append(name,size);
    const select=document.createElement('button');select.textContent=t(file.name===selected?'selected':'select');
    select.onclick=()=>action(async()=>{state=await command('load',file.name);notify('loadedNotice',false,null);});
    const remove=document.createElement('button');remove.className='delete';remove.textContent='×';remove.setAttribute('aria-label',t('deleteAria',{name:file.name}));
    remove.onclick=()=>action(async()=>{state=await command('delete',file.name);filesKey='';notify('deletedNotice',false,null);});
    row.append(info,select,remove);container.append(row);
  }
}
function lookUpLabels(packages) {
  const missing=packages.filter(p=>!labelLookups.has(p));if(!missing.length)return;
  missing.forEach(p=>labelLookups.add(p));
  // Older managers lack getPackagesInfo; the wrapper then returns [] and package names stay.
  for(const info of getPackagesInfo(missing))if(info?.appLabel&&!info.error)labels.set(info.packageName,String(info.appLabel));
}
const appName=pkg=>labels.get(pkg)||pkg;
function renderApps() {
  const term=$('app-search').value.trim().toLowerCase();const list=$('apps');list.replaceChildren();
  const all=[...new Set([...apps,...chosen])];lookUpLabels(all);
  const collator=new Intl.Collator(document.documentElement.lang);
  // Checked apps first. A box ticked now moves on the next redraw (search, save), not under the finger.
  const packages=all.filter(p=>p.toLowerCase().includes(term)||appName(p).toLowerCase().includes(term))
    .sort((a,b)=>(chosen.has(b)-chosen.has(a))||collator.compare(appName(a),appName(b))||a.localeCompare(b));
  for(const pkg of packages) {
    const label=document.createElement('label');label.className='app-item';
    const check=document.createElement('input');check.type='checkbox';check.checked=chosen.has(pkg);
    check.onchange=()=>{targetsDirty=true;check.checked?chosen.add(pkg):chosen.delete(pkg);$('target-packages').value=[...chosen].join('\n');};
    const text=document.createElement('span');text.className='app-text';
    const name=document.createElement('span');name.className='app-name';name.textContent=appName(pkg);text.append(name);
    if(labels.has(pkg)){const id=document.createElement('small');id.textContent=pkg;text.append(id);}
    label.append(check,text);list.append(label);
  }
  if(!packages.length){const p=document.createElement('p');p.className='muted';p.textContent=t('noMatchingApps');list.append(p);}
}
async function refresh() {
  if(refreshing||document.hidden)return;
  refreshing=true;const revision=actionRevision;
  try {const next=await command('status');if(revision===actionRevision){state=next;render(state);}}
  catch(e){connected=false;say($('connection'),'disconnected');$('connection').className='badge error';$('play').disabled=true;$('seek').disabled=true;
    if(!state){$('files').replaceChildren();notify('serviceDown',true,{detail:e.message});}}
  finally{refreshing=false;}
}
async function upload(files) {
  $('upload-progress').hidden=false;
  try {
    for(const file of files) {
      if(file.size<=0||file.size>512*1024*1024)throw new Error(t('sizeRange'));
      if(!/\.(wav|mp3|flac)$/i.test(file.name))throw new Error(t('formatsOnly'));
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
        say($('upload-label'),'uploading',{name:file.name,progress,speed:(copied/1024/Math.max(.001,seconds)).toFixed(0)});
        $('upload-progress').querySelector('progress').value=progress;
      }
      await command('commit',name);filesKey='';
    }
    notify('uploadedNotice',false,null);
  } finally {$('upload-progress').hidden=true;$('file-input').value='';}
}
function syncBrowserControls() {
  $('browser-up').disabled=busy||browserLoading||!browser?.parent;
  $('browser-import').disabled=busy||browserLoading||!importChoices.size;
  if(busy&&$('file-browser').open)say($('browser-import'),'importing');
  else if(importChoices.size)say($('browser-import'),'importCount',{n:importChoices.size});
  else say($('browser-import'),'importSelected');
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
    if(entry.directory){row.textContent=t('folderEntry',{name:entry.name});row.disabled=busy||browserLoading;row.onclick=()=>openFolder(path);}
    else {
      const check=document.createElement('input');check.type='checkbox';check.checked=importChoices.has(path);check.disabled=busy;
      check.onchange=()=>{check.checked?importChoices.set(path,entry):importChoices.delete(path);syncBrowserControls();};
      const name=document.createElement('span');name.textContent=entry.name;
      const size=document.createElement('small');size.textContent=(entry.bytes/1024/1024).toFixed(2)+' MB';
      name.append(size);row.append(check,name);
    }
    list.append(row);
  }
  if(!entries.length){const hint=document.createElement('p');hint.className='muted';hint.textContent=t(term?'noMatchingFiles':'noAudioHere');list.append(hint);}
}
async function openFolder(path) {
  if(busy||browserLoading)return;
  browserLoading=true;say($('browser-status'),'readingFolder');syncBrowserControls();
  $('browser-entries').replaceChildren();
  try {
    browser=await command('browse',path);$('browser-path').textContent=browser.path;$('browser-search').value='';
    say($('browser-status'),browser.truncated?'truncated':'browseHint');
  } catch(e){sayRaw($('browser-status'),e.message);}
  finally{browserLoading=false;renderBrowser();syncBrowserControls();}
}
async function importLocalFiles() {
  const files=[...importChoices.entries()];$('upload-progress').hidden=false;
  const progress=$('upload-progress').querySelector('progress');progress.removeAttribute('value');
  try {
    for(let i=0;i<files.length;++i) {
      const [path,file]=files[i];const args={name:file.name,i:i+1,n:files.length};say($('upload-label'),'copying',args);
      say($('browser-status'),'copying',args);
      await command('import',path);importChoices.delete(path);filesKey='';
    }
    $('file-browser').close();notify('copiedNotice',false,null);
  } catch(e){sayRaw($('browser-status'),e.message);throw e;}
  finally{$('upload-progress').hidden=true;progress.value=0;renderBrowser();syncBrowserControls();}
}
function renderMediaCard(s) {
  $('media-card').checked=s.enabled;
  say($('media-card-status'),!s.enabled?'mediaCardOff':s.running?'mediaCardOn':s.supervisor?'mediaCardStarting':'mediaCardReboot');
}
// The switch is saved by media-card.sh; the boot supervisor started by service.sh shows or removes the card.
async function mediaCard(command='status') {
  $('media-card').disabled=true;
  try {const s=await shell([MEDIA_CARD,command].map(quote).join(' '));renderMediaCard(s);return s;}
  finally {$('media-card').disabled=false;}
}
async function refreshMediaCard() {
  try {await mediaCard();} catch(e){sayRaw($('media-card-status'),e.message);}
}
document.querySelectorAll('[data-tab]').forEach(button=>button.onclick=()=>{
  document.querySelectorAll('[data-tab]').forEach(b=>{const active=b===button;b.classList.toggle('active',active);b.setAttribute('aria-selected',String(active));});
  document.querySelectorAll('.tab-panel').forEach(panel=>panel.hidden=panel.id!==button.dataset.tab);
  if(button.dataset.tab==='settings'&&typeof window.ksu!=='undefined')refreshMediaCard();
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
  const names=[...chosen];if(names.some(p=>!/^([a-zA-Z0-9_]+\.)+[a-zA-Z0-9_]+$/.test(p)))throw new Error(t('invalidPackage'));
  state=await command('targets',...names);targetsDirty=false;notify('targetsSaved',false,null);
});
$('media-card').onchange=()=>{
  const on=$('media-card').checked;
  if(busy){$('media-card').checked=!on;return;}
  action(async()=>{
    try {await mediaCard(on?'on':'off');notify(on?'mediaCardShown':'mediaCardHidden',false,null);}
    catch(e){await refreshMediaCard();throw e;}
  });
};
$('synthesize').onclick=()=>action(async()=>{
  const text=$('speech-text').value.trim();if(!text)throw new Error(t('enterText'));
  say($('synthesize'),'synthesizing');
  try {
    const data=await shell([MODULE+'/bin/tts.sh',text,$('language').value,$('speech-rate').value,1].map(quote).join(' '));
    state=await command('load',data.file);filesKey='';notify('ttsDone',false,null);
  } finally {say($('synthesize'),'synthesize');}
});
function syncLanguage() {
  const english=currentLanguage()==='en';
  $('language-toggle').textContent=english?'中文':'EN';$('language-toggle').lang=english?'zh-CN':'en';
  document.querySelector('.brand-latin').hidden=english;
}
$('language-toggle').onclick=()=>{
  setLanguage(currentLanguage()==='en'?'zh':'en');syncLanguage();
  filesKey='';if(state)render(state);if(!$('apps').querySelector('[data-i18n]'))renderApps();if(browser)renderBrowser();syncBrowserControls();
};
if(currentLanguage()==='en')$('language').value='en-US';
applyTranslations();syncLanguage();
if(typeof window.ksu==='undefined') {
  say($('connection'),'openFromKsu');$('connection').className='badge error';
  $('files').replaceChildren();notify('ksuOnly',true,null);
  document.querySelectorAll('button,input,textarea,select').forEach(element=>element.disabled=element.id!=='language-toggle');
} else {
  refresh();refreshMediaCard();
  setInterval(()=>paintProgress(performance.now()),100);
  exec('pm list packages -3').then(result=>{
    apps=result.stdout.split('\n').filter(line=>line.startsWith('package:')).map(line=>line.slice(8).trim());renderApps();
  }).catch(()=>renderApps());
  setInterval(()=>{if(!busy)refresh();},1000);
  document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh();});
}

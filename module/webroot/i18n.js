// UI strings. Values are text with {name} placeholders, or functions for plurals.
const plural=(n,one,many)=>n===1?one:many;
const messages={
  zh:{
    title:'璃麦 · GlassMic',brand:'璃麦',tagline:'让声音自由流动',switchLanguage:'切换到英文',
    connecting:'正在连接',connected:'服务已连接',disconnected:'服务未连接',openFromKsu:'请从 KernelSU 打开',
    ksuOnly:'这是 KernelSU 模块内的控制页面，请在管理器中打开璃麦的 WebUI。',
    serviceDown:'音频服务尚未启动。安装后需要重启一次；如已重启，请检查模块是否启用。{detail}',
    badData:'模块命令没有返回有效数据',failed:'操作失败',
    playerRegion:'播放控制',modeReal:'真实麦克风',modeFile:'音频文件',modeSilence:'静音输入',modeUnknown:'未知状态',
    waitingRecord:'等待录音',waitingApps:'等待目标应用启动',activity:'{n} 个后端 · {reads} 次读取',
    pickSound:'选择一段声音',pickSoundHint:'从音频库选择文件，或生成一段语音',
    descReal:'当前使用手机的真实麦克风',descSilence:'目标应用收到静音',
    descPaused:'已暂停 · 点击播放开始供音',descPlaying:'正在播放 · 目标应用录音时接收当前进度的声音',
    outputLevel:'输出电平',seekLabel:'播放进度',real:'麦克风',realAria:'切回真实麦克风',play:'播放',pause:'暂停',
    silence:'静音',silenceAria:'静音录音输入',loop:'循环播放',keepsPlaying:'关闭页面后继续供音',
    tabsAria:'功能',tabLibrary:'音频库',tabSpeech:'文字转语音',tabSettings:'设置',
    myAudio:'我的音频',formats:'WAV、MP3、FLAC · 每个文件最多 512 MB',importAudio:'导入音频',importing:'正在导入…',
    emptyLibrary:'音频库还是空的',emptyHint:'导入一段音频，或试试文字转语音。',
    select:'选择',selected:'已选择',deleteAria:'删除 {name}',
    loadedNotice:'已选择音频，点击播放开始',deletedNotice:'已删除模块中的音频副本',
    sizeRange:'文件大小必须在 1 B 到 512 MB 之间',formatsOnly:'当前支持 WAV、MP3 和 FLAC',
    uploading:'正在导入 {name} · {progress}% · {speed} KB/s',uploadedNotice:'音频已保存到模块音频库',
    speechTitle:'把文字变成声音',speechEngine:'使用手机已配置的系统语音引擎',speechPrompt:'想说些什么？',
    speechPlaceholder:'输入文字，生成后会保存到音频库',language:'语言',langZhCN:'普通话',langEnUS:'English',langZhTW:'中文（台湾）',
    rate:'语速',rateSlow:'慢一些',rateNormal:'标准',rateFast:'快一些',synthesize:'生成语音',synthesizing:'正在生成语音…',
    synthHint:'生成时请保持页面打开。生成完成后，播放由独立服务负责。',
    enterText:'请先输入文字',ttsDone:'语音已生成并选中，点击播放即可使用',
    soundSettings:'声音设置',settingsStored:'设置保存在模块中',gain:'输出增益',speed:'播放速度',
    speedHint:'速度与音调联动；内置软限幅保护输出。',
    mediaCard:'控制中心卡片',mediaCardHint:'在通知栏和控制中心的媒体卡片里控制播放',mediaCardToggle:'显示控制中心卡片',
    mediaCardChecking:'正在读取卡片状态…',mediaCardOff:'已关闭',mediaCardOn:'卡片已显示',mediaCardStarting:'正在启动卡片…',
    mediaCardReboot:'开关已保存，重启设备后生效',mediaCardNote:'卡片可播放、暂停、拖动进度、切换音频，以及切回真实麦克风或静音。开机后按此开关自动显示。',
    mediaCardShown:'已打开控制中心卡片',mediaCardHidden:'已关闭控制中心卡片',errMediaCardSave:'无法保存卡片设置',
    targetApps:'生效应用',targetAppsHint:'仅选中的应用使用虚拟麦克风',save:'保存',findApp:'查找应用',
    appSearchPlaceholder:'搜索应用名或包名',loadingApps:'正在读取应用列表…',noMatchingApps:'没有匹配的应用，可以在下方手动填写包名。',
    editManually:'手动编辑包名',packagesPlaceholder:'每行一个包名',
    targetNote:'首次选择的应用，需要在它下次启动时加载后端。移除应用会立即停止替换其录音。',
    invalidPackage:'请填写有效的应用包名',targetsSaved:'生效应用已保存',
    diagnostics:'运行诊断',noData:'暂无数据',diagVersion:'版本：{version}',diagBackends:'已连接后端：{n}',
    diagClient:'{pkg} · PID {pid}\n  Hook {hooks} · 读取 {reads} · 欠载 {missing} 帧 · 跳帧 {skipped}',
    diagStage:'\n  {stage} · AudioRecord {ready}',diagError:' · 错误码 {code}',
    stage1:'已连接，等待初始化',stage2:'正在初始化',stage3:'后端已初始化',stage4:'初始化失败',stageUnknown:'未知阶段',
    ready:'就绪',notReady:'未就绪',
    footer:'璃麦 · KernelSU 音频模块',
    browserTitle:'从手机导入音频',close:'关闭',closeBrowserAria:'关闭文件选择',
    browserIntro:'直接复制手机文件，支持 WAV、MP3、FLAC。原始文件会保留。',
    folderDownload:'下载',folderMusic:'音乐',folderInternal:'内部存储',up:'上一级',
    filterFiles:'筛选文件',filterPlaceholder:'筛选当前文件夹',systemPicker:'系统文件选择器',
    importSelected:'导入所选音频',importCount:'导入 {n} 个音频',cloudHint:'云盘等位置请使用系统文件选择器，传输速度会较慢。',
    folderEntry:'文件夹 · {name}',noMatchingFiles:'没有匹配的文件',noAudioHere:'这里没有音频，可进入文件夹或使用系统文件选择器。',
    readingFolder:'正在读取文件夹…',truncated:'文件过多，仅显示前 10000 项。',browseHint:'勾选音频后点击导入，可跨文件夹选择。',
    copying:'正在复制 {name} · {i} / {n}',copiedNotice:'已直接复制到音频库，原文件已保留',
    errNoEngine:'请先在系统中配置语音引擎',errTtsTemp:'无法创建合成临时文件',errTtsSave:'无法保存生成的语音',
    errEngine:'语音引擎错误：{code}',errFolder:'无法打开目录，请从手机内部存储选择',
    errExternal:'只能导入手机内部存储中的音频文件',errChanged:'源文件在复制时发生变化，请重试',
    errRoot:'需要 Root 权限',errUnavailable:'音频服务不可用',errRequest:'请求失败',errEmpty:'音频服务没有响应',
    errProtocol:'模块组件版本不一致，请重启设备',errFilename:'文件名无效',errMissing:'文件不存在',
    errUnsupported:'无法识别的音频，请使用 WAV、MP3 或 FLAC',errSelectFirst:'请先选择音频文件',
    errSeek:'跳转失败',errResume:'恢复播放位置失败',errProtected:'包名无效或属于受保护的系统进程',
    errTargets:'无法保存生效应用',errDelete:'无法删除文件',errUpload:'上传数据不连续或写入失败',
    errUploadMissing:'找不到上传的数据',errCommit:'保存导入的文件失败'
  },
  en:{
    title:'GlassMic',brand:'GlassMic',tagline:'Let your sound flow freely',switchLanguage:'Switch to Chinese',
    connecting:'Connecting',connected:'Service connected',disconnected:'Service offline',openFromKsu:'Open in KernelSU',
    ksuOnly:'This control page belongs to a KernelSU module. Open the GlassMic WebUI from the KernelSU manager.',
    serviceDown:'The audio service is not running. Reboot once after installing; if you already did, check that the module is enabled. {detail}',
    badData:'The module command returned no valid data',failed:'Operation failed',
    playerRegion:'Playback controls',modeReal:'Real microphone',modeFile:'Audio file',modeSilence:'Silent input',modeUnknown:'Unknown state',
    waitingRecord:'Waiting for recording',waitingApps:'Waiting for a target app',
    activity:({n,reads})=>n+' '+plural(n,'backend','backends')+' · '+reads+' '+plural(reads,'read','reads'),
    pickSound:'Choose a sound',pickSoundHint:'Pick a file from the library, or generate speech',
    descReal:"Using the phone's real microphone",descSilence:'Target apps receive silence',
    descPaused:'Paused · tap Play to start feeding audio',descPlaying:'Playing · target apps hear the current position when they record',
    outputLevel:'Output level',seekLabel:'Playback position',real:'Mic',realAria:'Switch back to the real microphone',play:'Play',pause:'Pause',
    silence:'Mute',silenceAria:'Mute recording input',loop:'Loop',keepsPlaying:'Keeps playing after you close this page',
    tabsAria:'Sections',tabLibrary:'Library',tabSpeech:'Text to speech',tabSettings:'Settings',
    myAudio:'My audio',formats:'WAV, MP3, FLAC · up to 512 MB each',importAudio:'Import audio',importing:'Importing…',
    emptyLibrary:'Your library is empty',emptyHint:'Import some audio, or try text to speech.',
    select:'Select',selected:'Selected',deleteAria:'Delete {name}',
    loadedNotice:'Audio selected. Tap Play to start.',deletedNotice:"Deleted the module's copy of this audio",
    sizeRange:'Files must be between 1 B and 512 MB',formatsOnly:'Only WAV, MP3 and FLAC are supported',
    uploading:'Importing {name} · {progress}% · {speed} KB/s',uploadedNotice:'Audio saved to the module library',
    speechTitle:'Turn text into sound',speechEngine:"Uses the phone's configured system speech engine",speechPrompt:'What would you like to say?',
    speechPlaceholder:'Type some text; the result is saved to your library',language:'Language',langZhCN:'Mandarin',langEnUS:'English',langZhTW:'Chinese (Taiwan)',
    rate:'Rate',rateSlow:'Slower',rateNormal:'Normal',rateFast:'Faster',synthesize:'Generate speech',synthesizing:'Generating speech…',
    synthHint:'Keep this page open while generating. Once done, the background service handles playback.',
    enterText:'Enter some text first',ttsDone:'Speech generated and selected. Tap Play to use it.',
    soundSettings:'Sound settings',settingsStored:'Settings are stored in the module',gain:'Output gain',speed:'Playback speed',
    speedHint:'Speed also changes pitch. A built-in soft limiter protects the output.',
    mediaCard:'Control center card',mediaCardHint:'Control playback from a media card in notifications and the control center',mediaCardToggle:'Show control center card',
    mediaCardChecking:'Checking card status…',mediaCardOff:'Off',mediaCardOn:'Card is showing',mediaCardStarting:'Starting the card…',
    mediaCardReboot:'Saved. Takes effect after a reboot.',mediaCardNote:'The card can play, pause, seek, switch audio, and go back to the real microphone or mute. It appears after boot while this is on.',
    mediaCardShown:'Control center card turned on',mediaCardHidden:'Control center card turned off',errMediaCardSave:'Could not save the card setting',
    targetApps:'Target apps',targetAppsHint:'Only selected apps use the virtual microphone',save:'Save',findApp:'Find apps',
    appSearchPlaceholder:'Search app or package names',loadingApps:'Loading app list…',noMatchingApps:'No matching apps. You can enter package names below.',
    editManually:'Edit package names manually',packagesPlaceholder:'One package name per line',
    targetNote:'A newly selected app loads the backend the next time it starts. Removing an app stops replacing its recording immediately.',
    invalidPackage:'Enter valid package names',targetsSaved:'Target apps saved',
    diagnostics:'Diagnostics',noData:'No data yet',diagVersion:'Version: {version}',diagBackends:'Connected backends: {n}',
    diagClient:'{pkg} · PID {pid}\n  Hooks {hooks} · reads {reads} · underruns {missing} frames · skipped {skipped}',
    diagStage:'\n  {stage} · AudioRecord {ready}',diagError:' · error {code}',
    stage1:'Connected, waiting to initialize',stage2:'Initializing',stage3:'Backend initialized',stage4:'Initialization failed',stageUnknown:'Unknown stage',
    ready:'ready',notReady:'not ready',
    footer:'GlassMic · KernelSU audio module',
    browserTitle:'Import audio from phone',close:'Close',closeBrowserAria:'Close file picker',
    browserIntro:'Copies files straight from the phone. Supports WAV, MP3 and FLAC. Originals are kept.',
    folderDownload:'Download',folderMusic:'Music',folderInternal:'Internal storage',up:'Up',
    filterFiles:'Filter files',filterPlaceholder:'Filter this folder',systemPicker:'System file picker',
    importSelected:'Import selected',importCount:({n})=>'Import '+n+' '+plural(n,'file','files'),
    cloudHint:'For cloud drives and similar locations, use the system file picker. Transfers are slower.',
    folderEntry:'Folder · {name}',noMatchingFiles:'No matching files',noAudioHere:'No audio here. Open a folder or use the system file picker.',
    readingFolder:'Reading folder…',truncated:'Too many files; showing the first 10,000.',browseHint:'Check audio files, then tap Import. You can pick from several folders.',
    copying:'Copying {name} · {i} / {n}',copiedNotice:'Copied to the library; originals were kept',
    errNoEngine:'Set up a speech engine in system settings first',errTtsTemp:'Could not create a temporary file for synthesis',
    errTtsSave:'Could not save the generated speech',errEngine:'Speech engine error: {code}',
    errFolder:'Cannot open this folder. Choose one in internal storage.',errExternal:'Only audio files in internal storage can be imported',
    errChanged:'The source file changed while copying. Try again.',
    errRoot:'Root access required',errUnavailable:'Audio service unavailable',errRequest:'Request failed',errEmpty:'The audio service did not respond',
    errProtocol:'Module components are out of sync. Reboot the device.',errFilename:'Invalid file name',errMissing:'File not found',
    errUnsupported:'Unrecognized audio. Use WAV, MP3 or FLAC.',errSelectFirst:'Select an audio file first',
    errSeek:'Seek failed',errResume:'Could not restore the playback position',errProtected:'Invalid or protected package name',
    errTargets:'Could not save target apps',errDelete:'Could not delete the file',errUpload:'Upload data was out of order or could not be written',
    errUploadMissing:'Uploaded data not found',errCommit:'Could not save the imported file'
  }
};
// Errors reported by glassmicctl, glassmicd and tts.sh, keyed by their exact text.
const backendErrors={
  '请输入文字':'enterText','请先在系统中配置语音引擎':'errNoEngine','无法创建合成临时文件':'errTtsTemp','无法保存生成的语音':'errTtsSave',
  '无法打开目录，请从手机内部存储选择':'errFolder','只能导入手机内部存储中的音频文件':'errExternal','当前支持 WAV、MP3 和 FLAC':'formatsOnly',
  '文件大小必须在 1 B 到 512 MB 之间':'sizeRange','源文件在复制时发生变化，请重试':'errChanged',
  'Root required':'errRoot','audio service unavailable':'errUnavailable','request failed':'errRequest','empty response':'errEmpty',
  'protocol mismatch':'errProtocol','invalid filename':'errFilename','file missing':'errMissing',
  'unsupported audio; use WAV, MP3 or FLAC':'errUnsupported','select an audio file first':'errSelectFirst',
  'seek failed':'errSeek','resume seek failed':'errResume','invalid or protected package':'errProtected',
  'cannot save targets':'errTargets','cannot delete file':'errDelete','upload offset mismatch or write failed':'errUpload',
  'upload missing':'errUploadMissing','commit failed':'errCommit',
  'cannot save media card setting':'errMediaCardSave'
};
const STORAGE_KEY='glassmic.language';
function initialLanguage() {
  try {const saved=localStorage.getItem(STORAGE_KEY);if(saved in messages)return saved;} catch {}
  return (navigator.languages?.[0]||navigator.language||'zh').toLowerCase().startsWith('zh')?'zh':'en';
}
let language=initialLanguage();
export const currentLanguage=()=>language;
export function t(key,args={}) {
  const value=messages[language][key]??messages.zh[key]??key;
  return typeof value==='function'?value(args):value.replace(/\{(\w+)\}/g,(_,name)=>args[name]??'');
}
export function translateError(text) {
  const message=String(text??'').trim();
  if(backendErrors[message])return t(backendErrors[message]);
  const engine=/^语音引擎错误：(-?\d+)$/.exec(message);
  return engine?t('errEngine',{code:engine[1]}):message;
}
// Text that should follow language changes; raw text (file names, OS errors) clears the key.
export function say(element,key,args) {
  element.dataset.i18n=key;
  if(args)element.dataset.i18nArgs=JSON.stringify(args);else delete element.dataset.i18nArgs;
  element.textContent=t(key,args);
}
export function sayRaw(element,text) {
  delete element.dataset.i18n;delete element.dataset.i18nArgs;element.textContent=text;
}
export function applyTranslations() {
  document.documentElement.lang=language==='zh'?'zh-CN':'en';
  document.title=t('title');
  document.querySelectorAll('[data-i18n]').forEach(element=>
    element.textContent=t(element.dataset.i18n,element.dataset.i18nArgs?JSON.parse(element.dataset.i18nArgs):undefined));
  document.querySelectorAll('[data-i18n-attr]').forEach(element=>{
    for(const pair of element.dataset.i18nAttr.split(';')){const [attribute,key]=pair.split(':');element.setAttribute(attribute,t(key));}
  });
}
export function setLanguage(next) {
  if(!(next in messages))return;
  language=next;
  try {localStorage.setItem(STORAGE_KEY,next);} catch {}
  applyTranslations();
}

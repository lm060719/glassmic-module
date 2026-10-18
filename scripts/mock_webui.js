async (page) => {
  await page.addInitScript(() => {
    window.__gmCommands=[];
    const state={ok:true,version:'0.1.0.8',mode:0,paused:true,loop:false,gainDb:0,speed:1,positionMs:0,durationMs:45000,peak:0.34,selected:'',
      targets:['io.mo.glassmic.probe'],clients:[{package:'io.mo.glassmic.probe',pid:12345,hooks:15,reads:2048,frames:960000,missing:0,skipped:0}],
      files:[{name:'清晨的声音.wav',bytes:4320000},{name:"朋友的'问候.mp3",bytes:640000}]};
    window.__gmState=state;
    let clock=performance.now();
    const folders={
      '/sdcard/Download':[{name:'音乐',directory:true,bytes:0},{name:"朋友的'无损.FLAC",directory:false,bytes:24000000},{name:'长音频.mp3',directory:false,bytes:18000000}],
      '/sdcard/Download/音乐':[{name:'立体声.flac',directory:false,bytes:32000000}],
      '/sdcard/Music':[],
      '/sdcard':[{name:'Download',directory:true,bytes:0},{name:'Music',directory:true,bytes:0}]
    };
    function words(source) {
      const out=[];let word='',quoted=false,started=false;
      for(let i=0;i<source.length;i++){const c=source[i];if(c==="'"){quoted=!quoted;started=true;}
        else if(c==='\\'&&!quoted){word+=source[++i]||'';started=true;}
        else if(/\s/.test(c)&&!quoted){if(started){out.push(word);word='';started=false;}}
        else{word+=c;started=true;}}
      if(started)out.push(word);return out;
    }
    window.ksu={exec(command,options,callback) {
      const now=performance.now();
      if(state.mode===1&&!state.paused){state.positionMs+=(now-clock)*state.speed;
        if(state.loop)state.positionMs%=state.durationMs;
        else if(state.positionMs>=state.durationMs){state.positionMs=state.durationMs;state.paused=true;}}
      clock=now;
      window.__gmCommands.push(command);
      let stdout='',errno=0;
      if(command==='pm list packages -3')stdout='package:io.mo.glassmic.probe\npackage:com.example.recorder\npackage:com.example.chat\n';
      else {
        const args=words(command),cmd=args[1];
        if(args[0].endsWith('media-card.sh')) {
          window.__gmMediaCard||={enabled:false};
          if(cmd==='on'||cmd==='off')window.__gmMediaCard.enabled=cmd==='on';
          const card=window.__gmMediaCard;
          stdout=JSON.stringify({ok:true,enabled:card.enabled,running:card.enabled,supervisor:true});
        } else if(args[0].endsWith('tts.sh')) {
          state.files.push({name:'TTS-测试.wav',bytes:240000});
          stdout=JSON.stringify({ok:true,file:'TTS-测试.wav'});
        } else if(cmd==='browse') {
          const path=args[2];stdout=JSON.stringify(folders[path]?{ok:true,path,parent:path==='/sdcard'?'':path.slice(0,path.lastIndexOf('/')),entries:folders[path]}:{ok:false,error:'目录不可用'});
        } else if(cmd==='import') {
          if(window.__gmImportError){errno=1;stdout=JSON.stringify({ok:false,error:'模拟复制失败'});}
          else {const name=args[2].split('/').at(-1);state.files.push({name,bytes:24000000});stdout=JSON.stringify({ok:true,file:name,bytes:24000000,elapsedMs:20});}
        } else {
          if(cmd==='load'){state.selected=args[2];state.mode=1;state.paused=true;}
          if(cmd==='play'){state.mode=1;state.paused=false;}
          if(cmd==='pause')state.paused=true;
          if(cmd==='real'){state.mode=0;state.paused=true;}
          if(cmd==='silence'){state.mode=2;state.paused=true;}
          if(cmd==='loop')state.loop=Number(args[2])!==0;
          if(cmd==='seek')state.positionMs=Number(args[2]);
          if(cmd==='gain')state.gainDb=Number(args[2]);
          if(cmd==='speed')state.speed=Number(args[2]);
          if(cmd==='targets')state.targets=args.slice(2);
          if(cmd==='commit')state.files.push({name:args[2],bytes:window.__gmUploaded?.[args[2]]||4096});
          if(cmd==='upload'){window.__gmUploaded||={};window.__gmUploaded[args[2]]=Number(args[3])+atob(args[4]).length;}
          if(cmd==='delete')state.files=state.files.filter(f=>f.name!==args[2]);
          stdout=JSON.stringify(state);
        }
      }
      setTimeout(()=>window[callback](errno,stdout,''),5);
    }};
  });
  await page.reload();
  await page.getByText('服务已连接',{exact:true}).waitFor();
  return {title:await page.title(),preview:'mock bridge; no device commands'};
}

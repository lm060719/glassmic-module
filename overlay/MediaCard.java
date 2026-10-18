package io.mo.glassmic.overlay;

import android.app.Notification;
import android.app.NotificationManager;
import android.content.Context;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Control-center media card: a MediaSession plus a MediaStyle notification, both owned by this root
 * app_process. Needs no window and no APK. Card buttons arrive as session callbacks and run glassmicctl
 * on a worker thread. Exits on --seconds, or when <data>/media-card.stop appears, removing the notification.
 */
public final class MediaCard {
    static final String TAG="glassmic-media";static final int ID=0x6d1c;
    static final String REAL="io.mo.glassmic.REAL",SILENCE="io.mo.glassmic.SILENCE";
    final Context context;final NotificationManager notifications;final MediaSession session;
    final Handler main=new Handler(Looper.getMainLooper());
    final ExecutorService worker=Executors.newSingleThreadExecutor();
    final ScheduledExecutorService watchdog=Executors.newSingleThreadScheduledExecutor();
    final String ctl,data,channel;final boolean zh;final File stop;final long deadline;
    JSONObject state;String shown="";boolean busy,polling;

    public static void main(String[] args) {
        Map<String,String> o=new HashMap<>();
        for(int i=0;i<args.length;i++) {
            if(args[i].equals("--cancel"))o.put(args[i],"1");
            else if(args[i].startsWith("--")&&i+1<args.length)o.put(args[i],args[++i]);
            else {Main.fail("unknown argument "+args[i]);return;}
        }
        try {
            Context context=Main.systemContext();
            if(o.containsKey("--cancel")) {
                context.getSystemService(NotificationManager.class).cancel(TAG,ID);
                System.out.println("{\"ok\":true,\"cancelled\":true}");System.exit(0);
            }
            new MediaCard(context,o).start();
            Looper.loop();
        } catch(Throwable e) {e.printStackTrace(System.err);Main.fail(e.toString());}
    }
    MediaCard(Context context,Map<String,String> o) {
        this.context=context;notifications=context.getSystemService(NotificationManager.class);
        ctl=o.getOrDefault("--ctl","/data/adb/modules/glassmic/bin/glassmicctl");data=o.getOrDefault("--data","/data/adb/glassmic");
        // An existing channel of the "android" package; creating one would persist in notification policy.
        channel=o.getOrDefault("--channel","DEVELOPER");
        zh=o.getOrDefault("--lang",java.util.Locale.getDefault().getLanguage()).startsWith("zh");
        stop=new File(data,"media-card.stop");stop.delete();
        deadline=SystemClock.uptimeMillis()+1000L*Integer.parseInt(o.getOrDefault("--seconds","600"));
        session=new MediaSession(context,"GlassMic");
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPlay(){event("play");action("play");}
            @Override public void onPause(){event("pause");action("pause");}
            @Override public void onStop(){event("stop");action("pause");}
            @Override public void onSeekTo(long ms){event("seek "+ms);action("seek",String.valueOf(ms));}
            @Override public void onSkipToNext(){event("next");skip(1);}
            @Override public void onSkipToPrevious(){event("previous");skip(-1);}
            @Override public void onCustomAction(String name,Bundle extras) {
                event("custom "+name);
                if(REAL.equals(name))action("real");else if(SILENCE.equals(name))action("silence");
            }
        },main);
    }
    String t(String zhText,String enText){return zh?zhText:enText;}
    static void event(String name){log("{\"event\":"+JSONObject.quote(name)+",\"uptimeMs\":"+SystemClock.uptimeMillis()+"}");}
    /** stdout goes to a file when launched in the background; flush so the log can be read while running. */
    static void log(String line){System.out.println(line);System.out.flush();}
    void start() {
        session.setActive(true);
        poll.run();
        log("{\"ok\":true,\"pid\":"+android.os.Process.myPid()+",\"channel\":"+JSONObject.quote(channel)+"}");
    }
    void finish() {
        main.removeCallbacksAndMessages(null);
        try{notifications.cancel(TAG,ID);}catch(RuntimeException e){System.err.println("cancel failed: "+e);}
        session.setActive(false);session.release();stop.delete();
        System.out.println("{\"ok\":true,\"finished\":true}");System.exit(0);
    }
    final Runnable poll=new Runnable() {
        @Override public void run() {
            if(stop.exists()||SystemClock.uptimeMillis()>deadline){finish();return;}
            if(!busy&&!polling) {
                polling=true;
                worker.execute(()->{JSONObject r=exec("status");main.post(()->{polling=false;apply(r);});});
            }
            main.postDelayed(this,1000);
        }
    };
    void action(String... args) {
        if(busy)return;
        busy=true;
        worker.execute(()->{
            JSONObject r=exec(args);
            log("{\"command\":"+JSONObject.quote(String.join(" ",args))+",\"ok\":"+r.optBoolean("ok")+(r.optBoolean("ok")?"":",\"error\":"+JSONObject.quote(r.optString("error")))+"}");
            main.post(()->{busy=false;apply(r);});
        });
    }
    /** Next or previous file in the same name order the WebUI lists them. */
    void skip(int step) {
        JSONObject s=state;if(s==null)return;
        JSONArray files=s.optJSONArray("files");List<String> names=new ArrayList<>();
        for(int i=0;files!=null&&i<files.length();i++)names.add(files.optJSONObject(i).optString("name"));
        if(names.isEmpty())return;
        names.sort(String::compareTo);
        int at=names.indexOf(s.optString("selected"));
        action("load",names.get(Math.floorMod((at<0?(step>0?-1:0):at)+step,names.size())));
    }
    void apply(JSONObject r) {
        if(!r.optBoolean("ok")||!r.has("mode"))return;
        state=r;
        int mode=r.optInt("mode");boolean playing=mode==1&&!r.optBoolean("paused");
        String selected=r.optString("selected","");
        String status=mode==0?t("真实麦克风","Real microphone"):mode==2?t("静音输入","Silent input"):playing?t("正在供音","Feeding audio"):t("已暂停","Paused");
        String title=selected.isEmpty()?t("未选择音频","No audio selected"):selected;
        session.setMetadata(new MediaMetadata.Builder()
            .putString(MediaMetadata.METADATA_KEY_TITLE,title)
            .putString(MediaMetadata.METADATA_KEY_ARTIST,t("璃麦 · ","GlassMic · ")+status)
            .putLong(MediaMetadata.METADATA_KEY_DURATION,r.optLong("durationMs")).build());
        long actions=PlaybackState.ACTION_PLAY|PlaybackState.ACTION_PAUSE|PlaybackState.ACTION_PLAY_PAUSE|PlaybackState.ACTION_SEEK_TO
            |PlaybackState.ACTION_SKIP_TO_NEXT|PlaybackState.ACTION_SKIP_TO_PREVIOUS;
        session.setPlaybackState(new PlaybackState.Builder().setActions(actions)
            .setState(playing?PlaybackState.STATE_PLAYING:PlaybackState.STATE_PAUSED,r.optLong("positionMs"),playing?(float)r.optDouble("speed",1):0f,SystemClock.elapsedRealtime())
            .addCustomAction(new PlaybackState.CustomAction.Builder(REAL,t("真实麦克风","Real mic"),android.R.drawable.ic_btn_speak_now).build())
            .addCustomAction(new PlaybackState.CustomAction.Builder(SILENCE,t("静音","Mute"),android.R.drawable.ic_lock_silent_mode).build())
            .build());
        String key=title+"\n"+status;
        if(!key.equals(shown)){shown=key;notifyCard(title,status);}
    }
    void notifyCard(String title,String status) {
        Notification n=new Notification.Builder(context,channel)
            .setSmallIcon(android.R.drawable.ic_btn_speak_now).setContentTitle(title).setContentText(status)
            .setOngoing(true).setOnlyAlertOnce(true).setShowWhen(false).setCategory(Notification.CATEGORY_TRANSPORT)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .setStyle(new Notification.MediaStyle().setMediaSession(session.getSessionToken())).build();
        try{notifications.notify(TAG,ID,n);}
        catch(RuntimeException e){log("{\"ok\":false,\"error\":"+JSONObject.quote("notify: "+e)+"}");}
    }
    JSONObject exec(String... args) {
        List<String> command=new ArrayList<>(Arrays.asList(ctl,"--data",data));command.addAll(Arrays.asList(args));
        try {
            Process process=new ProcessBuilder(command).redirectErrorStream(true).start();
            process.getOutputStream().close();
            ScheduledFuture<?> kill=watchdog.schedule(process::destroyForcibly,5,TimeUnit.SECONDS);
            String last=null;
            try(BufferedReader in=new BufferedReader(new InputStreamReader(process.getInputStream(),StandardCharsets.UTF_8))) {
                for(String line;(line=in.readLine())!=null;)if(line.startsWith("{"))last=line;
            }
            process.waitFor();kill.cancel(false);
            return last==null?new JSONObject().put("ok",false).put("error","no response"):new JSONObject(last);
        } catch(Exception e) {
            try{return new JSONObject().put("ok",false).put("error",e.toString());}catch(Exception never){throw new IllegalStateException(never);}
        }
    }
}

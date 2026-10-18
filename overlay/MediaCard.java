package io.mo.glassmic.overlay;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.media.MediaMetadata;
import android.media.session.MediaSession;
import android.media.session.PlaybackState;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.SystemClock;
import java.io.BufferedReader;
import java.io.File;
import java.io.InputStreamReader;
import java.io.RandomAccessFile;
import java.nio.channels.FileLock;
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
 * on a worker thread. Exits, removing the notification, when <data>/media-card.stop appears, when the
 * module directory gets a disable or remove flag, or after --seconds. --cancel removes a leftover
 * notification; --uninstall also deletes the notification channel and must run as the system uid.
 *
 * Notifications of the "android" package are filed under its uid 1000, and the notification service keys
 * channels by uid. A root caller would create the channel under uid 0, where notify does not look, so the
 * channel is created for uid 1000 explicitly (this process's system context reports uid 0). Deleting acts
 * only on the caller's uid, hence --uninstall as 1000.
 */
public final class MediaCard {
    static final String TAG="glassmic-media",CHANNEL="glassmic.media";static final int ID=0x6d1c;
    static final String REAL="io.mo.glassmic.REAL",SILENCE="io.mo.glassmic.SILENCE";
    // Notification.EXTRA_SUBSTITUTE_APP_NAME (system API); honored for the "android" package, which holds the permission.
    static final String SUBSTITUTE_APP_NAME="android.substName";
    static final long IDLE_POLL_MS=15000;
    static FileLock instance;
    final Context context;final NotificationManager notifications;final PowerManager power;final MediaSession session;
    final Handler main=new Handler(Looper.getMainLooper());
    final ExecutorService worker=Executors.newSingleThreadExecutor();
    final ScheduledExecutorService watchdog=Executors.newSingleThreadScheduledExecutor();
    final String ctl,data;final boolean zh;final File stop,disable,remove;final long deadline;
    JSONObject state;String shown="",metadataKey="",stateKey="";boolean busy,polling;
    long lastPoll,statePosition,stateTime;float stateSpeed;

    public static void main(String[] args) {
        Map<String,String> o=new HashMap<>();
        for(int i=0;i<args.length;i++) {
            if(args[i].equals("--cancel")||args[i].equals("--uninstall"))o.put(args[i],"1");
            else if(args[i].startsWith("--")&&i+1<args.length)o.put(args[i],args[++i]);
            else {Main.fail("unknown argument "+args[i]);return;}
        }
        try {
            Context context=Main.systemContext();
            NotificationManager notifications=context.getSystemService(NotificationManager.class);
            if(o.containsKey("--uninstall")&&android.os.Process.myUid()!=android.os.Process.SYSTEM_UID) {
                Main.fail("--uninstall must run as uid 1000 (su 1000 -c ...)");return;
            }
            if(o.containsKey("--cancel")||o.containsKey("--uninstall")) {
                notifications.cancel(TAG,ID);
                // The system keeps a deleted channel only as a hidden "deleted" record.
                if(o.containsKey("--uninstall"))notifications.deleteNotificationChannel(CHANNEL);
                System.out.println("{\"ok\":true,\"cancelled\":true,\"channelDeleted\":"+o.containsKey("--uninstall")+"}");System.exit(0);
            }
            new MediaCard(context,o).start();
            Looper.loop();
        } catch(Throwable e) {e.printStackTrace(System.err);Main.fail(e.toString());}
    }
    MediaCard(Context context,Map<String,String> o) {
        this.context=context;notifications=context.getSystemService(NotificationManager.class);power=context.getSystemService(PowerManager.class);
        ctl=o.getOrDefault("--ctl","/data/adb/modules/glassmic/bin/glassmicctl");data=o.getOrDefault("--data","/data/adb/glassmic");
        String module=o.get("--module");
        disable=module==null?null:new File(module,"disable");remove=module==null?null:new File(module,"remove");
        zh=o.getOrDefault("--lang",java.util.Locale.getDefault().getLanguage()).startsWith("zh");
        // The stop file is left in place: whoever starts the card clears it first, so a stop request is never lost.
        stop=new File(data,"media-card.stop");
        int seconds=Integer.parseInt(o.getOrDefault("--seconds","0"));
        deadline=seconds>0?SystemClock.uptimeMillis()+1000L*seconds:0;
        session=new MediaSession(context,"GlassMic");
        session.setCallback(new MediaSession.Callback() {
            @Override public void onPlay(){event("play");action("play");}
            @Override public void onPause(){event("pause");action("pause");}
            @Override public void onStop(){event("stop");action("pause");}
            @Override public void onSeekTo(long ms){event("seek "+ms);action("seek",String.valueOf(Math.max(0,ms)));}
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
    void start() throws Exception {
        instance=new RandomAccessFile(new File(data,"media-card.lock"),"rw").getChannel().tryLock();
        if(instance==null){Main.fail("media card already running");return;}
        // Re-creating an existing channel only refreshes its name and description; the user's choices are kept.
        NotificationChannel channel=new NotificationChannel(CHANNEL,t("璃麦媒体控制","GlassMic media controls"),NotificationManager.IMPORTANCE_LOW);
        channel.setDescription(t("控制中心里的播放控制卡片","Playback card in the control center"));
        channel.setShowBadge(false);channel.setSound(null,null);channel.enableVibration(false);
        // INotificationManager.createNotificationChannelsForPackage (system callers, root included).
        Object service=NotificationManager.class.getMethod("getService").invoke(null);
        Class<?> slice=Class.forName("android.content.pm.ParceledListSlice");
        service.getClass().getMethod("createNotificationChannelsForPackage",String.class,int.class,slice)
            .invoke(service,"android",android.os.Process.SYSTEM_UID,slice.getConstructor(List.class).newInstance(List.of(channel)));
        session.setActive(true);
        poll.run();
        log("{\"ok\":true,\"pid\":"+android.os.Process.myPid()+",\"channel\":"+JSONObject.quote(CHANNEL)+"}");
    }
    void finish() {
        main.removeCallbacksAndMessages(null);
        try{notifications.cancel(TAG,ID);}catch(RuntimeException e){System.err.println("cancel failed: "+e);}
        session.setActive(false);session.release();
        log("{\"ok\":true,\"finished\":true}");System.exit(0);
    }
    boolean exitRequested() {
        return stop.exists()||disable!=null&&(disable.exists()||remove.exists())||deadline>0&&SystemClock.uptimeMillis()>deadline;
    }
    final Runnable poll=new Runnable() {
        @Override public void run() {
            if(exitRequested()){finish();return;}
            long now=SystemClock.uptimeMillis();
            // The card shows progress from the playback state, so polling only picks up outside changes; slow it while the screen is off.
            if(!busy&&!polling&&(now-lastPoll>=IDLE_POLL_MS||power.isInteractive())) {
                polling=true;lastPoll=now;
                worker.execute(()->{JSONObject r=exec("status");main.post(()->{polling=false;if(r.optBoolean("ok")&&r.has("mode"))apply(r);else offline();});});
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
            main.post(()->{busy=false;if(r.optBoolean("ok")&&r.has("mode"))apply(r);});
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
        state=r;
        int mode=r.optInt("mode");boolean playing=mode==1&&!r.optBoolean("paused");
        String selected=r.optString("selected","");
        String status=mode==0?t("真实麦克风","Real microphone"):mode==2?t("静音输入","Silent input"):playing?t("正在供音","Feeding audio"):t("已暂停","Paused");
        String title=selected.isEmpty()?t("未选择音频","No audio selected"):selected;
        long duration=r.optLong("durationMs"),position=r.optLong("positionMs");
        float speed=playing?(float)r.optDouble("speed",1):0f;
        long actions=PlaybackState.ACTION_PLAY|PlaybackState.ACTION_PAUSE|PlaybackState.ACTION_PLAY_PAUSE
            |PlaybackState.ACTION_SKIP_TO_NEXT|PlaybackState.ACTION_SKIP_TO_PREVIOUS|(selected.isEmpty()?0:PlaybackState.ACTION_SEEK_TO);
        show(title,t("璃麦 · ","GlassMic · ")+status,duration,playing?PlaybackState.STATE_PLAYING:PlaybackState.STATE_PAUSED,actions,position,speed,status);
    }
    /** Status failed: the daemon is not running (yet). Keep the card, but without controls that would fail. */
    void offline() {
        state=null;
        String status=t("音频服务未连接","Audio service offline");
        show(t("璃麦","GlassMic"),status,0,PlaybackState.STATE_STOPPED,0,0,0f,status);
    }
    /** Pushes metadata and playback state only when they change; between updates the system extrapolates progress. */
    void show(String title,String artist,long duration,int playback,long actions,long position,float speed,String status) {
        String metadata=title+"\n"+artist+"\n"+duration;
        if(!metadata.equals(metadataKey)) {
            metadataKey=metadata;
            session.setMetadata(new MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE,title).putString(MediaMetadata.METADATA_KEY_ARTIST,artist)
                .putLong(MediaMetadata.METADATA_KEY_DURATION,duration).build());
        }
        long now=SystemClock.elapsedRealtime();
        String key=playback+"|"+actions+"|"+speed;
        long expected=statePosition+(long)((now-stateTime)*stateSpeed);
        if(!key.equals(stateKey)||Math.abs(position-expected)>750) {
            stateKey=key;statePosition=position;stateTime=now;stateSpeed=speed;
            PlaybackState.Builder builder=new PlaybackState.Builder().setActions(actions).setState(playback,position,speed,now);
            if(playback!=PlaybackState.STATE_STOPPED)builder
                .addCustomAction(new PlaybackState.CustomAction.Builder(REAL,t("真实麦克风","Real mic"),android.R.drawable.ic_btn_speak_now).build())
                .addCustomAction(new PlaybackState.CustomAction.Builder(SILENCE,t("静音","Mute"),android.R.drawable.ic_lock_silent_mode).build());
            session.setPlaybackState(builder.build());
        }
        String card=title+"\n"+status;
        if(!card.equals(shown)){shown=card;notifyCard(title,status);}
    }
    void notifyCard(String title,String status) {
        Bundle extras=new Bundle();extras.putString(SUBSTITUTE_APP_NAME,t("璃麦","GlassMic"));
        Notification n=new Notification.Builder(context,CHANNEL)
            .setSmallIcon(android.R.drawable.ic_btn_speak_now).setContentTitle(title).setContentText(status)
            .setOngoing(true).setOnlyAlertOnce(true).setShowWhen(false).setCategory(Notification.CATEGORY_TRANSPORT)
            .setVisibility(Notification.VISIBILITY_PUBLIC).addExtras(extras)
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

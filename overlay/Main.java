package io.mo.glassmic.overlay;

import android.animation.ValueAnimator;
import android.content.Context;
import android.content.ContextWrapper;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Outline;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PixelFormat;
import android.graphics.Rect;
import android.graphics.RectF;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.text.TextUtils;
import android.util.DisplayMetrics;
import android.util.TypedValue;
import android.view.Display;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.view.ViewGroup;
import android.view.ViewOutlineProvider;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.RandomAccessFile;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.nio.channels.FileLock;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.Predicate;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Floating quick controls drawn by a root app_process. No APK, Activity or audio hook:
 * every control runs glassmicctl on a worker thread, the UI thread only draws and handles touch.
 * --show adds the window; --render draws offscreen PNGs and never adds a window.
 */
public final class Main {
    static final int SP=TypedValue.COMPLEX_UNIT_SP,TYPE=WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY;
    static final class Palette {
        final int surface,soft,text,muted,accent,onAccent,border,warn;
        Palette(int... c){surface=c[0];soft=c[1];text=c[2];muted=c[3];accent=c[4];onAccent=c[5];border=c[6];warn=c[7];}
    }
    // Same tokens as the WebUI (webroot/style.css).
    static final Palette LIGHT=new Palette(0xffffffff,0xffe9efeb,0xff192720,0xff53665b,0xff126d50,0xffffffff,0xffd7e1da,0xff805213);
    static final Palette DARK=new Palette(0xff1b2820,0xff29382e,0xffebf4ed,0xffb2c7b8,0xffa1dfbc,0xff133a25,0xff3c5042,0xfff5cd88);
    static FileLock instance;

    final Context ui;final WindowManager wm;final Handler main=new Handler(Looper.getMainLooper());
    final ExecutorService worker=Executors.newSingleThreadExecutor();
    final ScheduledExecutorService watchdog=Executors.newSingleThreadScheduledExecutor();
    final String ctl,data;final boolean zh;final float density;final Palette palette;
    final List<Panel> panels=new ArrayList<>();final List<Ball> balls=new ArrayList<>();
    final FrameLayout host;final Ball ball;final Panel panel;
    final WindowManager.LayoutParams params=new WindowManager.LayoutParams();
    volatile JSONObject state;volatile String error;volatile boolean actionBusy;
    boolean expanded,attached,polling;int generation,ballX,ballY;long anchorTime;

    public static void main(String[] args) {
        Map<String,String> o=new HashMap<>();
        for(int i=0;i<args.length;i++) {
            if(args[i].equals("--exercise")||args[i].equals("--show"))o.put(args[i],"1");
            else if(args[i].startsWith("--")&&i+1<args.length)o.put(args[i],args[++i]);
            else {fail("unknown argument "+args[i]);return;}
        }
        try {
            Context ui=uiContext();initFonts();
            Main overlay=new Main(ui,o);
            if(o.containsKey("--render"))overlay.render(new File(o.get("--render")),o.containsKey("--exercise"));
            else if(o.containsKey("--show"))overlay.show();
            else {fail("--show or --render required");return;}
            Looper.loop();
        } catch(Throwable e) {e.printStackTrace(System.err);fail(e.toString());}
    }
    static void fail(String e){System.out.println("{\"ok\":false,\"error\":"+JSONObject.quote(e)+"}");System.exit(1);}
    /** Main looper plus the system context ("android" package) of this bare root process. */
    static Context systemContext() throws Exception {
        Looper.prepareMainLooper();
        // Android 17 WindowManager reads this process-local instance; it is not shared with system processes.
        try {
            Class<?> memory=Class.forName("com.android.internal.os.ApplicationSharedMemory");
            memory.getMethod("setInstance",memory).invoke(null,memory.getMethod("create").invoke(null));
        } catch(ClassNotFoundException ignored) { /* Before Android 17. */ }
        Class<?> at=Class.forName("android.app.ActivityThread");
        Context system=(Context)at.getMethod("getSystemContext").invoke(at.getMethod("systemMain").invoke(null));
        // Registers mainline service wrappers (media session, etc.), as SystemServer and bindApplication do.
        try{at.getMethod("initializeMainlineModules").invoke(null);}catch(NoSuchMethodException ignored){/* Before Android 11. */}
        bindSettingsProvider();
        return system;
    }
    static Context uiContext() throws Exception {
        Context system=systemContext();
        Display display=system.getSystemService(DisplayManager.class).getDisplay(Display.DEFAULT_DISPLAY);
        Context window=system.createWindowContext(display,TYPE,null);
        // This process is unknown to ActivityManager, so content providers reject it. View.performClick
        // creates AutofillManager, which reads DeviceConfig through the settings provider; no view here autofills.
        return new ContextWrapper(window) {
            @Override public Object getSystemService(String name){return "autofill".equals(name)?null:super.getSystemService(name);}
        };
    }
    /**
     * ActivityManager does not know this process, so Settings reads (used by View clicks, ROM view hooks and
     * DeviceConfig) are refused. Like the shell `settings` command, take the provider as an external client.
     */
    static void bindSettingsProvider() {
        try {
            Object am=Class.forName("android.app.ActivityManager").getMethod("getService").invoke(null);
            int user=(Integer)Class.forName("android.app.ActivityManager").getMethod("getCurrentUser").invoke(null);
            Method external=am.getClass().getMethod("getContentProviderExternal",String.class,int.class,android.os.IBinder.class,String.class);
            Object holder=external.invoke(am,"settings",user,new android.os.Binder(),"glassmic-overlay");
            Object provider=holder==null?null:holder.getClass().getField("provider").get(holder);
            if(provider==null)throw new IllegalStateException("no settings provider");
            for(String scope:new String[]{"System","Secure","Global","Config"}) {
                for(Field f:Class.forName("android.provider.Settings$"+scope).getDeclaredFields()) {
                    if(!java.lang.reflect.Modifier.isStatic(f.getModifiers())||!f.getType().getName().equals("android.provider.Settings$ContentProviderHolder"))continue;
                    f.setAccessible(true);Object cache=f.get(null);
                    Field slot=cache.getClass().getDeclaredField("mContentProvider");slot.setAccessible(true);slot.set(cache,provider);
                }
            }
        } catch(Throwable e) {System.err.println("glassmic-overlay: settings provider unavailable: "+e);}
    }
    /**
     * Zygote or bindApplication normally installs the default typeface. A bare app_process with lazy
     * typeface initialization (Android 17) aborts in Minikin on the first text draw unless it is loaded here.
     */
    static void initFonts() throws Exception {
        try {
            Field current=Typeface.class.getDeclaredField("sDefaultTypeface");current.setAccessible(true);
            if(current.get(null)!=null)return;
        } catch(NoSuchFieldException ignored) { /* Fall through to the loader. */ }
        try {
            Method load=Typeface.class.getDeclaredMethod("loadPreinstalledSystemFontMap");load.setAccessible(true);load.invoke(null);
        } catch(NoSuchMethodException ignored) { /* Before Android 12 the static initializer loads fonts. */ }
    }

    Main(Context ui,Map<String,String> o) {
        this.ui=ui;wm=ui.getSystemService(WindowManager.class);
        ctl=o.getOrDefault("--ctl","/data/adb/modules/glassmic/bin/glassmicctl");data=o.getOrDefault("--data","/data/adb/glassmic");
        String lang=o.getOrDefault("--lang",java.util.Locale.getDefault().getLanguage());zh=lang.startsWith("zh");
        density=ui.getResources().getDisplayMetrics().density;
        String theme=o.getOrDefault("--theme","auto");
        boolean night=theme.equals("dark")||theme.equals("auto")&&(ui.getResources().getConfiguration().uiMode&Configuration.UI_MODE_NIGHT_MASK)==Configuration.UI_MODE_NIGHT_YES;
        palette=night?DARK:LIGHT;
        ball=new Ball(palette,false);panel=new Panel(palette);
        host=new FrameLayout(ui) {
            @Override public boolean dispatchTouchEvent(MotionEvent e) {
                if(e.getActionMasked()==MotionEvent.ACTION_OUTSIDE){if(expanded)setExpanded(false);return true;}
                return super.dispatchTouchEvent(e);
            }
            @Override protected void onConfigurationChanged(Configuration c){super.onConfigurationChanged(c);post(Main.this::clamp);}
        };
        host.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);
        int shadow=dp(10);host.setPadding(shadow,shadow,shadow,shadow);host.setClipToPadding(false);
        host.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->clamp());
        bindBall();
    }
    String t(String zhText,String enText){return zh?zhText:enText;}
    int dp(float v){return Math.round(v*density);}

    // ---- Window ----
    void show() throws Exception {
        new File(data).mkdirs();
        instance=new RandomAccessFile(new File(data,"overlay.lock"),"rw").getChannel().tryLock();
        if(instance==null){fail("overlay already running");return;}
        Rect screen=screen();ballX=screen.width()-dp(72);ballY=screen.height()/3;loadPosition();
        params.type=TYPE;params.format=PixelFormat.TRANSLUCENT;params.gravity=Gravity.TOP|Gravity.START;
        params.width=params.height=WindowManager.LayoutParams.WRAP_CONTENT;
        params.flags=WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL|WindowManager.LayoutParams.FLAG_WATCH_OUTSIDE_TOUCH;
        params.setTitle("GlassMic");params.x=ballX;params.y=ballY;
        host.addView(ball);wm.addView(host,params);attached=true;
        poll.run();
        System.out.println("{\"ok\":true,\"pid\":"+android.os.Process.myPid()+"}");
    }
    void close() {
        main.removeCallbacksAndMessages(null);
        if(attached)try{wm.removeViewImmediate(host);}catch(RuntimeException ignored){}
        attached=false;System.exit(0);
    }
    Rect screen() {
        if(Build.VERSION.SDK_INT>=30)return wm.getCurrentWindowMetrics().getBounds();
        DisplayMetrics m=ui.getResources().getDisplayMetrics();return new Rect(0,0,m.widthPixels,m.heightPixels);
    }
    /** Keeps the whole window on screen after layout, rotation or a size change. */
    void clamp() {
        if(!attached||host.getWidth()==0)return;
        Rect s=screen();
        int x=Math.max(0,Math.min(params.x,s.width()-host.getWidth())),y=Math.max(0,Math.min(params.y,s.height()-host.getHeight()));
        if(x!=params.x||y!=params.y){params.x=x;params.y=y;wm.updateViewLayout(host,params);}
    }
    void move(int x,int y){params.x=x;params.y=y;if(attached){wm.updateViewLayout(host,params);clamp();}}
    void setExpanded(boolean value) {
        if(expanded==value)return;
        expanded=value;host.removeAllViews();
        if(value) {
            int width=Math.min(dp(320),screen().width()-2*host.getPaddingLeft());
            host.addView(panel.root,new FrameLayout.LayoutParams(width,ViewGroup.LayoutParams.WRAP_CONTENT));
            refreshViews();
        } else host.addView(ball);
        move(value?params.x:ballX,value?params.y:ballY);
        main.removeCallbacks(poll);poll.run();main.removeCallbacks(tick);if(value)tick.run();
    }
    void bindBall() {
        int slop=ViewConfiguration.get(ui).getScaledTouchSlop();
        float[] down=new float[2];int[] start=new int[2];boolean[] dragging={false};
        ball.setOnClickListener(v->setExpanded(true));
        ball.setOnTouchListener((v,e)->{
            switch(e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    down[0]=e.getRawX();down[1]=e.getRawY();start[0]=params.x;start[1]=params.y;dragging[0]=false;v.setPressed(true);return true;
                case MotionEvent.ACTION_MOVE:
                    float dx=e.getRawX()-down[0],dy=e.getRawY()-down[1];
                    if(!dragging[0]&&Math.hypot(dx,dy)>slop){dragging[0]=true;v.setPressed(false);}
                    if(dragging[0])move(start[0]+Math.round(dx),start[1]+Math.round(dy));
                    return true;
                case MotionEvent.ACTION_UP:
                    v.setPressed(false);
                    if(dragging[0])snap();else v.performClick();
                    return true;
                case MotionEvent.ACTION_CANCEL:
                    v.setPressed(false);if(dragging[0])snap();return true;
                default:return false;
            }
        });
    }
    /** Settles the ball against the nearer side edge and remembers the spot. */
    void snap() {
        int width=screen().width(),from=params.x,to=params.x+host.getWidth()/2<width/2?0:width-host.getWidth();
        ValueAnimator animator=ValueAnimator.ofInt(from,to).setDuration(180);
        animator.addUpdateListener(a->move((Integer)a.getAnimatedValue(),params.y));
        animator.addListener(new android.animation.AnimatorListenerAdapter() {
            @Override public void onAnimationEnd(android.animation.Animator a){ballX=params.x;ballY=params.y;savePosition();}
        });
        animator.start();
    }
    File positionFile(){return new File(data,"overlay.json");}
    void loadPosition() {
        try {
            JSONObject p=new JSONObject(new String(Files.readAllBytes(positionFile().toPath()),StandardCharsets.UTF_8));
            ballX=p.getInt("x");ballY=p.getInt("y");
        } catch(Exception ignored) { /* First run or unreadable: keep the default spot. */ }
    }
    void savePosition() {
        String json="{\"x\":"+ballX+",\"y\":"+ballY+"}";
        worker.execute(()->{
            File target=positionFile(),temporary=new File(data,"overlay.json.tmp");
            try(FileOutputStream out=new FileOutputStream(temporary)){out.write(json.getBytes(StandardCharsets.UTF_8));out.getFD().sync();}
            catch(Exception e){temporary.delete();return;}
            if(!temporary.renameTo(target))temporary.delete();
        });
    }

    // ---- Control ----
    final Runnable poll=new Runnable() {
        @Override public void run() {
            main.removeCallbacks(this);
            if(!actionBusy&&!polling) {
                polling=true;int request=generation;
                worker.execute(()->{JSONObject r=exec("status");main.post(()->{polling=false;if(request==generation)apply(r,true);});});
            }
            main.postDelayed(this,expanded?1000:3000);
        }
    };
    final Runnable tick=new Runnable() {
        @Override public void run(){for(Panel p:panels)p.progress();if(expanded)main.postDelayed(this,250);}
    };
    void action(String... args) {
        if(actionBusy||state==null)return;
        actionBusy=true;++generation;refreshViews();
        worker.execute(()->{
            JSONObject r=exec(args);
            main.post(()->{actionBusy=false;apply(r,false);poll.run();});
        });
    }
    void apply(JSONObject r,boolean status) {
        if(r.optBoolean("ok")&&r.has("mode")){state=r;error=null;anchorTime=SystemClock.uptimeMillis();}
        else if(!r.optBoolean("ok")){error=r.optString("error",t("操作失败","Failed"));if(status)state=null;}
        refreshViews();
    }
    void refreshViews(){for(Ball b:balls)b.invalidate();for(Panel p:panels)p.update();}
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
            if(last==null)return new JSONObject().put("ok",false).put("error",t("音频服务没有响应","Audio service did not respond"));
            return new JSONObject(last);
        } catch(Exception e) {
            try{return new JSONObject().put("ok",false).put("error",e.toString());}catch(Exception never){throw new IllegalStateException(never);}
        }
    }
    static String time(long ms){long n=Math.max(0,ms/1000);return String.format(java.util.Locale.ROOT,"%02d:%02d",n/60,n%60);}
    long position() {
        JSONObject s=state;if(s==null)return 0;
        long position=s.optLong("positionMs"),duration=s.optLong("durationMs");
        if(s.optInt("mode")==1&&!s.optBoolean("paused")) {
            position+=Math.round((SystemClock.uptimeMillis()-anchorTime)*s.optDouble("speed",1));
            if(s.optBoolean("loop")&&duration>0)position%=duration;
        }
        return Math.max(0,Math.min(position,duration));
    }

    // ---- Views ----
    static Drawable rounded(int fill,int stroke,float radius,int strokeWidth) {
        GradientDrawable d=new GradientDrawable();d.setColor(fill);d.setCornerRadius(radius);
        if(strokeWidth>0)d.setStroke(strokeWidth,stroke);
        return d;
    }
    Drawable touchable(int fill,int stroke,float radius,Palette p) {
        int strokeWidth=stroke==0?0:dp(1);
        return new RippleDrawable(ColorStateList.valueOf((p.accent&0x00ffffff)|0x33000000),rounded(fill,stroke,radius,strokeWidth),rounded(0xff000000,0,radius,0));
    }
    TextView label(Palette p,float size,int color,boolean bold) {
        TextView v=new TextView(ui);v.setTextSize(SP,size);v.setTextColor(color);v.setIncludeFontPadding(false);
        if(bold)v.setTypeface(null,Typeface.BOLD);
        return v;
    }
    TextView button(Palette p,String text,String description) {
        TextView v=label(p,14,p.text,true);v.setText(text);v.setContentDescription(description);v.setGravity(Gravity.CENTER);
        v.setMinHeight(dp(44));v.setPadding(dp(8),0,dp(8),0);v.setClickable(true);
        // maxLines, not singleLine: centred single-line text scrolls into view only on pre-draw.
        v.setMaxLines(1);v.setEllipsize(TextUtils.TruncateAt.END);
        return v;
    }
    static LinearLayout.LayoutParams weighted(){return new LinearLayout.LayoutParams(0,ViewGroup.LayoutParams.WRAP_CONTENT,1);}
    static LinearLayout.LayoutParams margins(int left,int top,int right,int bottom) {
        LinearLayout.LayoutParams l=new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,ViewGroup.LayoutParams.WRAP_CONTENT);
        l.setMargins(left,top,right,bottom);return l;
    }

    /** Collapsed state: a draggable round handle whose glyph shows the input source. */
    final class Ball extends View {
        final Palette p;final Paint fill=new Paint(Paint.ANTI_ALIAS_FLAG),ink=new Paint(Paint.ANTI_ALIAS_FLAG);
        JSONObject fixed;final boolean preview;
        Ball(Palette p,boolean preview) {
            super(ui);this.p=p;this.preview=preview;balls.add(this);
            ink.setStrokeCap(Paint.Cap.ROUND);setClickable(true);setImportantForAutofill(IMPORTANT_FOR_AUTOFILL_NO);setElevation(dp(6));
            setOutlineProvider(new ViewOutlineProvider(){@Override public void getOutline(View v,Outline o){o.setOval(0,0,v.getWidth(),v.getHeight());}});
        }
        @Override protected void onMeasure(int w,int h){int s=dp(52);setMeasuredDimension(s,s);}
        @Override protected void onDraw(Canvas c) {
            JSONObject s=preview?fixed:state;
            int mode=s==null?-1:s.optInt("mode",-1);boolean playing=mode==1&&!s.optBoolean("paused");
            setContentDescription(t("璃麦浮窗：","GlassMic: ")+modeText(s)+t("，点按展开",". Tap to expand"));
            float r=getWidth()/2f,cx=r,cy=getHeight()/2f;
            fill.setStyle(Paint.Style.FILL);fill.setColor(isPressed()?p.soft:playing?p.accent:p.surface);c.drawCircle(cx,cy,r,fill);
            if(!playing){fill.setStyle(Paint.Style.STROKE);fill.setStrokeWidth(dp(1.5f));fill.setColor(mode<0?p.warn:p.border);c.drawCircle(cx,cy,r-dp(.75f),fill);}
            ink.setColor(playing?p.onAccent:mode<0?p.warn:mode==1?p.accent:p.text);
            ink.setStyle(Paint.Style.FILL);
            if(playing) {
                float w=dp(4),gap=dp(3);float[] heights={dp(10),dp(18),dp(13)};
                for(int i=0;i<3;i++){float x=cx-1.5f*w-gap+i*(w+gap);c.drawRoundRect(x,cy-heights[i]/2,x+w,cy+heights[i]/2,w/2,w/2,ink);}
            } else if(mode==1) {
                Path play=new Path();play.moveTo(cx-dp(5),cy-dp(9));play.lineTo(cx+dp(9),cy);play.lineTo(cx-dp(5),cy+dp(9));play.close();
                ink.setStrokeJoin(Paint.Join.ROUND);ink.setStyle(Paint.Style.FILL_AND_STROKE);ink.setStrokeWidth(dp(2));c.drawPath(play,ink);
            } else if(mode<0) {
                c.drawRoundRect(cx-dp(2),cy-dp(11),cx+dp(2),cy+dp(4),dp(2),dp(2),ink);c.drawCircle(cx,cy+dp(9),dp(2.2f),ink);
            } else {
                c.drawRoundRect(cx-dp(5),cy-dp(13),cx+dp(5),cy+dp(3),dp(5),dp(5),ink);
                ink.setStyle(Paint.Style.STROKE);ink.setStrokeWidth(dp(2));
                c.drawArc(new RectF(cx-dp(9),cy-dp(7),cx+dp(9),cy+dp(8)),0,180,false,ink);
                c.drawLine(cx,cy+dp(8),cx,cy+dp(12),ink);c.drawLine(cx-dp(5),cy+dp(12),cx+dp(5),cy+dp(12),ink);
                if(mode==2) {
                    ink.setColor(p.surface);ink.setStrokeWidth(dp(5));c.drawLine(cx-dp(11),cy-dp(12),cx+dp(11),cy+dp(10),ink);
                    ink.setColor(p.warn);ink.setStrokeWidth(dp(2.2f));c.drawLine(cx-dp(11),cy-dp(12),cx+dp(11),cy+dp(10),ink);
                }
            }
        }
        @Override protected void drawableStateChanged(){super.drawableStateChanged();invalidate();}
    }
    String modeText(JSONObject s) {
        if(s==null)return t("音频服务未连接","Audio service offline");
        int mode=s.optInt("mode",-1);
        return mode==0?t("真实麦克风","Real microphone"):mode==2?t("静音输入","Silent input")
            :mode==1?(s.optBoolean("paused")?t("音频已暂停","Audio paused"):t("正在播放音频","Playing audio")):t("未知状态","Unknown state");
    }

    /** Expanded state: source, progress, transport and the audio library. */
    final class Panel {
        final Palette p;final LinearLayout root,files;final TextView mode,track,time,play,real,silence,notice;final SeekBar seek;
        final List<TextView> rows=new ArrayList<>();String filesKey;boolean seeking;
        Panel(Palette p) {
            this.p=p;panels.add(this);
            root=new LinearLayout(ui);root.setOrientation(LinearLayout.VERTICAL);root.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO_EXCLUDE_DESCENDANTS);root.setPadding(dp(16),dp(12),dp(12),dp(16));
            root.setBackground(rounded(p.surface,p.border,dp(24),dp(1)));root.setElevation(dp(8));root.setClickable(true);

            LinearLayout header=new LinearLayout(ui);header.setGravity(Gravity.CENTER_VERTICAL);
            TextView title=label(p,16,p.text,true);title.setText(t("璃麦","GlassMic"));
            mode=label(p,12,p.accent,true);mode.setPadding(dp(8),dp(4),dp(8),dp(4));mode.setBackground(rounded(p.soft,0,dp(12),0));
            LinearLayout.LayoutParams chip=new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT,ViewGroup.LayoutParams.WRAP_CONTENT);chip.setMarginStart(dp(8));
            header.addView(title);header.addView(mode,chip);header.addView(new View(ui),weighted());
            TextView collapse=icon("−",t("收起","Collapse")),close=icon("×",t("关闭浮窗","Close floating controls"));
            collapse.setOnClickListener(v->setExpanded(false));close.setOnClickListener(v->close());
            header.addView(collapse);header.addView(close);
            root.addView(header);

            track=label(p,17,p.text,true);track.setSingleLine(true);track.setEllipsize(TextUtils.TruncateAt.MIDDLE);
            root.addView(track,margins(0,dp(10),dp(4),0));
            seek=new SeekBar(ui);seek.setContentDescription(t("播放进度","Playback position"));
            seek.setProgressTintList(ColorStateList.valueOf(p.accent));seek.setThumbTintList(ColorStateList.valueOf(p.accent));
            seek.setProgressBackgroundTintList(ColorStateList.valueOf(p.border));seek.setPadding(dp(8),dp(12),dp(8),dp(12));
            seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
                @Override public void onProgressChanged(SeekBar s,int value,boolean user){if(user)time.setText(time(value)+" / "+time(s.getMax()));}
                @Override public void onStartTrackingTouch(SeekBar s){seeking=true;}
                @Override public void onStopTrackingTouch(SeekBar s){seeking=false;action("seek",String.valueOf(s.getProgress()));}
            });
            root.addView(seek,margins(-dp(8),dp(4),-dp(4),0));
            time=label(p,12,p.muted,false);time.setFontFeatureSettings("tnum");
            root.addView(time,margins(0,0,0,dp(10)));

            LinearLayout transport=new LinearLayout(ui);
            real=button(p,t("麦克风","Mic"),t("切回真实麦克风","Switch back to the real microphone"));
            play=button(p,t("播放","Play"),t("播放","Play"));
            silence=button(p,t("静音","Mute"),t("静音录音输入","Mute recording input"));
            real.setOnClickListener(v->action("real"));silence.setOnClickListener(v->action("silence"));
            play.setOnClickListener(v->{JSONObject s=state;action(s!=null&&s.optInt("mode")==1&&!s.optBoolean("paused")?"pause":"play");});
            LinearLayout.LayoutParams middle=weighted();middle.setMargins(dp(8),0,dp(8),0);
            transport.addView(real,weighted());transport.addView(play,middle);transport.addView(silence,weighted());
            root.addView(transport,margins(0,0,dp(4),0));

            TextView library=label(p,12,p.muted,true);library.setText(t("音频列表","Audio library"));
            root.addView(library,margins(0,dp(16),0,dp(6)));
            files=new LinearLayout(ui);files.setOrientation(LinearLayout.VERTICAL);
            ScrollView scroll=new ScrollView(ui) {
                @Override protected void onMeasure(int w,int h){super.onMeasure(w,View.MeasureSpec.makeMeasureSpec(dp(184),View.MeasureSpec.AT_MOST));}
            };
            scroll.setVerticalFadingEdgeEnabled(true);scroll.addView(files);
            root.addView(scroll,margins(0,0,dp(4),0));
            notice=label(p,12,p.warn,false);notice.setLineSpacing(0,1.3f);
            root.addView(notice,margins(0,dp(10),dp(4),0));
            update();
        }
        TextView icon(String glyph,String description) {
            TextView v=button(p,glyph,description);v.setTextSize(SP,20);v.setPadding(0,0,0,0);v.setTextColor(p.muted);
            v.setBackground(touchable(0,0,dp(22),p));v.setLayoutParams(new LinearLayout.LayoutParams(dp(44),dp(44)));
            return v;
        }
        void style(TextView v,boolean primary,boolean active,boolean enabled) {
            int fill=primary?p.accent:active?p.soft:0,text=primary?p.onAccent:active?p.accent:p.text;
            v.setBackground(touchable(fill,primary||active?0:p.border,dp(14),p));v.setTextColor(text);
            v.setEnabled(enabled);v.setAlpha(enabled?1f:.45f);
        }
        void update() {
            JSONObject s=state;boolean connected=s!=null,busy=actionBusy;
            int m=connected?s.optInt("mode",-1):-1;boolean playing=m==1&&!s.optBoolean("paused");
            String selected=connected?s.optString("selected",""):"";
            mode.setText(modeText(s));mode.setTextColor(connected?p.accent:p.warn);
            track.setText(selected.isEmpty()?t("未选择音频","No audio selected"):selected);
            play.setText(playing?t("暂停","Pause"):t("播放","Play"));play.setContentDescription(play.getText());
            boolean enabled=connected&&!busy;
            style(real,false,m==0,enabled);style(play,true,false,enabled&&!selected.isEmpty());style(silence,false,m==2,enabled);
            seek.setEnabled(enabled&&!selected.isEmpty());
            if(!seeking)seek.setMax((int)Math.max(1,Math.min(Integer.MAX_VALUE,connected?s.optLong("durationMs"):1)));
            progress();
            String key=connected?String.valueOf(s.optJSONArray("files"))+"\n"+selected+busy:"";
            if(!key.equals(filesKey)){filesKey=key;renderFiles(connected?s.optJSONArray("files"):null,selected,enabled);}
            String problem=error;notice.setVisibility(problem==null?View.GONE:View.VISIBLE);
            if(problem!=null)notice.setText(connected?problem:t("音频服务未连接：","Audio service offline: ")+problem);
        }
        void progress() {
            JSONObject s=state;long duration=s==null?0:s.optLong("durationMs"),position=position();
            if(!seeking){seek.setProgress((int)Math.min(Integer.MAX_VALUE,position));time.setText(time(position)+" / "+time(duration));}
        }
        void renderFiles(JSONArray list,String selected,boolean enabled) {
            files.removeAllViews();rows.clear();
            List<String> names=new ArrayList<>();
            for(int i=0;list!=null&&i<list.length();i++){JSONObject f=list.optJSONObject(i);if(f!=null)names.add(f.optString("name"));}
            names.sort(String::compareTo);
            if(names.isEmpty()) {
                TextView empty=label(p,13,p.muted,false);empty.setLineSpacing(0,1.3f);
                empty.setText(t("音频库为空，请在 KernelSU 的 WebUI 中导入。","The library is empty. Import audio in the KernelSU WebUI."));
                files.addView(empty);return;
            }
            for(String name:names) {
                boolean current=name.equals(selected);
                TextView row=label(p,14,current?p.accent:p.text,current);
                row.setText((current?"✓  ":"")+name);row.setSingleLine(true);row.setEllipsize(TextUtils.TruncateAt.MIDDLE);
                row.setGravity(Gravity.CENTER_VERTICAL);row.setMinHeight(dp(44));row.setPadding(dp(12),0,dp(12),0);
                row.setBackground(touchable(current?p.soft:0,0,dp(12),p));row.setClickable(true);
                row.setEnabled(enabled&&!current);row.setContentDescription(t("选择 ","Select ")+name);
                row.setOnClickListener(v->action("load",name));
                files.addView(row,margins(0,0,0,dp(2)));rows.add(row);
            }
        }
    }

    // ---- Offscreen check (no window) ----
    void render(File dir,boolean exercise) {
        Panel dark=palette==DARK?panel:new Panel(DARK),light=palette==LIGHT?panel:new Panel(LIGHT);
        new Thread(()->{
            JSONObject result=new JSONObject();
            try {
                dir.mkdirs();
                main.post(poll);
                waitFor("status",s->s!=null,5000,result);
                onMain(()->{
                    snapshot(strip(),0,new File(dir,"balls.png"));
                    snapshot(light.root,dp(320),new File(dir,"panel-light.png"));snapshot(dark.root,dp(320),new File(dir,"panel-dark.png"));
                });
                if(exercise)exercise(dir,light,result);
                result.put("ok",true);
            } catch(Throwable e) {
                e.printStackTrace(System.err);
                try{result.put("ok",false).put("error",e.toString());}catch(Exception ignored){}
            }
            try{result.put("windowAdded",attached);}catch(Exception ignored){}
            System.out.println(result);System.exit(result.optBoolean("ok")?0:1);
        },"render").start();
    }
    void exercise(File dir,Panel p,JSONObject result) throws Exception {
        JSONArray list=state.optJSONArray("files");
        if(list==null||list.length()==0)throw new IllegalStateException("isolated library has no audio file");
        String first=p.rows.isEmpty()?null:p.rows.get(0).getText().toString().replace("✓  ","");
        if(first!=null&&!first.equals(state.optString("selected"))) {
            onMain(()->p.rows.get(0).performClick());
            String name=first;waitFor("load",s->name.equals(s.optString("selected"))&&s.optInt("mode")==1&&s.optBoolean("paused"),5000,result);
        }
        onMain(()->p.play.performClick());waitFor("play",s->s.optInt("mode")==1&&!s.optBoolean("paused"),5000,result);
        Thread.sleep(1200);
        onMain(()->snapshot(p.root,dp(320),new File(dir,"panel-playing.png")));
        result.put("advancedMs",state.optLong("positionMs"));
        onMain(()->p.play.performClick());waitFor("pause",s->s.optInt("mode")==1&&s.optBoolean("paused"),5000,result);
        onMain(()->p.silence.performClick());waitFor("silence",s->s.optInt("mode")==2,5000,result);
        onMain(()->p.real.performClick());waitFor("real",s->s.optInt("mode")==0,5000,result);
        onMain(()->{setExpanded(true);setExpanded(false);});result.put("toggle",!expanded&&host.getChildAt(0)==ball);
    }
    View strip() {
        LinearLayout rows=new LinearLayout(ui);rows.setOrientation(LinearLayout.VERTICAL);
        for(Palette p:new Palette[]{LIGHT,DARK}) {
            LinearLayout row=new LinearLayout(ui);row.setPadding(dp(8),dp(8),dp(8),dp(8));
            for(int mode=-1;mode<=3;mode++) {
                Ball b=new Ball(p,true);
                try{b.fixed=mode<0?null:new JSONObject().put("mode",mode==3?1:mode).put("paused",mode==3);}catch(Exception ignored){}
                LinearLayout.LayoutParams l=new LinearLayout.LayoutParams(dp(52),dp(52));l.setMargins(dp(6),0,dp(6),0);row.addView(b,l);
            }
            row.setBackground(new ColorDrawable(p==LIGHT?0xffc9d3cd:0xff0b110e));rows.addView(row);
        }
        return rows;
    }
    void snapshot(View v,int width,File file) {
        v.measure(width>0?View.MeasureSpec.makeMeasureSpec(width,View.MeasureSpec.EXACTLY):View.MeasureSpec.makeMeasureSpec(0,View.MeasureSpec.UNSPECIFIED),
            View.MeasureSpec.makeMeasureSpec(0,View.MeasureSpec.UNSPECIFIED));
        v.layout(0,0,v.getMeasuredWidth(),v.getMeasuredHeight());
        int pad=dp(12);Bitmap bitmap=Bitmap.createBitmap(v.getMeasuredWidth()+2*pad,v.getMeasuredHeight()+2*pad,Bitmap.Config.ARGB_8888);
        Canvas canvas=new Canvas(bitmap);canvas.drawColor(0xff8a948e);canvas.translate(pad,pad);v.draw(canvas);
        try(FileOutputStream out=new FileOutputStream(file)){bitmap.compress(Bitmap.CompressFormat.PNG,100,out);}
        catch(Exception e){throw new IllegalStateException(e);}
        file.setReadable(true,false);bitmap.recycle();
    }
    void onMain(Runnable task) throws Exception {
        CountDownLatch done=new CountDownLatch(1);Throwable[] failure={null};
        main.post(()->{try{task.run();}catch(Throwable e){failure[0]=e;}finally{done.countDown();}});
        if(!done.await(10,TimeUnit.SECONDS))throw new IllegalStateException("UI thread timeout");
        if(failure[0]!=null)throw new IllegalStateException(failure[0]);
    }
    void waitFor(String step,Predicate<JSONObject> ready,long timeout,JSONObject result) throws Exception {
        long end=SystemClock.uptimeMillis()+timeout;
        while(SystemClock.uptimeMillis()<end) {
            JSONObject s=state;
            if(!actionBusy&&s!=null&&ready.test(s)){result.put(step,true);return;}
            Thread.sleep(50);
        }
        result.put(step,false);throw new IllegalStateException(step+" timed out; last error: "+error);
    }
}

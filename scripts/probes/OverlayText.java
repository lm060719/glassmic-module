import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Typeface;
import android.os.Looper;
import android.view.View;
import android.view.WindowManager;
import java.io.FileOutputStream;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import org.json.JSONObject;

/**
 * Offscreen text rendering check, one mode per process because a native font
 * assertion aborts the whole process. Never adds a window or launches an Activity.
 * Usage: OverlayText <state|paint|textview|preload-paint|preload-textview> <out.png>
 */
public final class OverlayText {
    public static void main(String[] args) {
        try { run(args[0],args[1]); }
        catch(Throwable error) { error.printStackTrace(System.err);System.exit(1); }
    }
    static Context uiContext() throws Exception {
        Looper.prepareMainLooper();
        try {
            Class<?> memory=Class.forName("com.android.internal.os.ApplicationSharedMemory");
            memory.getMethod("setInstance",memory).invoke(null,memory.getMethod("create").invoke(null));
        } catch(ClassNotFoundException ignored) { /* Before Android 17. */ }
        Class<?> at=Class.forName("android.app.ActivityThread");
        Object thread=at.getMethod("systemMain").invoke(null);
        Context context=(Context)at.getMethod("getSystemContext").invoke(thread);
        android.view.Display display=context.getSystemService(android.hardware.display.DisplayManager.class).getDisplay(android.view.Display.DEFAULT_DISPLAY);
        return context.createWindowContext(display,WindowManager.LayoutParams.TYPE_SYSTEM_DIALOG,null);
    }
    static Object field(Class<?> type,String name) {
        try { Field f=type.getDeclaredField(name);f.setAccessible(true);return f.get(null); }
        catch(Throwable error) { return "unavailable: "+error; }
    }
    static void fontState(JSONObject out) throws Exception {
        Object map=field(Typeface.class,"sSystemFontMap");
        out.put("systemFontMap",map instanceof java.util.Map?((java.util.Map<?,?>)map).size():String.valueOf(map));
        out.put("defaultTypeface",String.valueOf(field(Typeface.class,"sDefaultTypeface")));
        out.put("lazyInit",String.valueOf(field(Typeface.class,"ENABLE_LAZY_TYPEFACE_INITIALIZATION")));
    }
    static void preload(JSONObject out) throws Exception {
        Method load=Typeface.class.getDeclaredMethod("loadPreinstalledSystemFontMap");
        load.setAccessible(true);load.invoke(null);
        out.put("preloaded",true);
    }
    static void save(Bitmap bitmap,String path) throws Exception {
        try(FileOutputStream file=new FileOutputStream(path)) { bitmap.compress(Bitmap.CompressFormat.PNG,100,file); }
        new java.io.File(path).setReadable(true,false);
    }
    /** Fraction of pixels that differ from the background, so the result does not depend on viewing the PNG. */
    static double inked(Bitmap bitmap,int background) {
        int[] pixels=new int[bitmap.getWidth()*bitmap.getHeight()];
        bitmap.getPixels(pixels,0,bitmap.getWidth(),0,0,bitmap.getWidth(),bitmap.getHeight());
        int count=0;for(int p:pixels)if(p!=background)count++;
        return (double)count/pixels.length;
    }
    static void run(String mode,String output) throws Exception {
        JSONObject out=new JSONObject();out.put("mode",mode);
        Context ui=uiContext();
        fontState(out);
        if(mode.startsWith("preload-")) { preload(out);fontState(out);mode=mode.substring(8); }
        System.err.println("probe: drawing "+mode);
        final int background=0xfff4f7f5,width=720,height=200;
        Bitmap bitmap=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);
        Canvas canvas=new Canvas(bitmap);canvas.drawColor(background);
        String text="GlassMic 璃麦 ▶ 01:23 / 04:56";
        if(mode.equals("paint")) {
            Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);paint.setColor(0xff1b2a22);paint.setTextSize(44);
            canvas.drawText(text,24,120,paint);
        } else if(mode.equals("textview")) {
            android.widget.TextView view=new android.widget.TextView(ui);
            view.setText(text);view.setTextSize(android.util.TypedValue.COMPLEX_UNIT_PX,44);view.setTextColor(0xff1b2a22);
            view.measure(View.MeasureSpec.makeMeasureSpec(width,View.MeasureSpec.EXACTLY),View.MeasureSpec.makeMeasureSpec(height,View.MeasureSpec.EXACTLY));
            view.layout(0,0,width,height);view.draw(canvas);
        } else if(!mode.equals("state")) throw new IllegalArgumentException(mode);
        out.put("inkedFraction",inked(bitmap,background));
        save(bitmap,output);bitmap.recycle();
        out.put("windowDisplayed",false);
        System.out.println(out);System.exit(0);
    }
}

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.os.Looper;
import android.view.WindowManager;
import org.json.JSONObject;

/** Read-only capability check. Does not add windows, launch Activities or use audio. */
public final class OverlayEnvironment {
    public static void main(String[] args) {
        try { check(); }
        catch(Throwable error) { error.printStackTrace(System.err);System.exit(1); }
    }
    static void check() throws Exception {
        System.err.println("probe: main");
        Looper.prepareMainLooper();
        System.err.println("probe: looper");
        Class<?> memory=Class.forName("com.android.internal.os.ApplicationSharedMemory");
        Object shared=memory.getMethod("create").invoke(null);
        memory.getMethod("setInstance",memory).invoke(null,shared);
        Class<?> at=Class.forName("android.app.ActivityThread");
        Object thread=at.getMethod("systemMain").invoke(null);
        System.err.println("probe: context");
        Context context=(Context)at.getMethod("getSystemContext").invoke(thread);
        android.view.Display display=context.getSystemService(android.hardware.display.DisplayManager.class).getDisplay(android.view.Display.DEFAULT_DISPLAY);
        Context ui=context.createWindowContext(display,WindowManager.LayoutParams.TYPE_SYSTEM_DIALOG,null);
        System.err.println("probe: ui context");
        JSONObject result=new JSONObject();
        result.put("uid",android.os.Process.myUid());
        result.put("package",context.getPackageName());
        result.put("windowManager",ui.getSystemService(WindowManager.class)!=null);
        result.put("internalWindowPermission",context.checkPermission("android.permission.INTERNAL_SYSTEM_WINDOW",android.os.Process.myPid(),android.os.Process.myUid()));
        android.view.View view=new android.view.View(ui);
        view.setBackgroundColor(0xffdcefe4);
        view.measure(android.view.View.MeasureSpec.makeMeasureSpec(400,android.view.View.MeasureSpec.EXACTLY),android.view.View.MeasureSpec.makeMeasureSpec(200,android.view.View.MeasureSpec.EXACTLY));
        view.layout(0,0,400,200);
        Bitmap bitmap=Bitmap.createBitmap(400,200,Bitmap.Config.ARGB_8888);
        view.draw(new Canvas(bitmap));bitmap.recycle();
        result.put("offscreenDrawing",true);
        result.put("windowDisplayed",false);
        System.out.println(result);System.exit(0);
    }
}

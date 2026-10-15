import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Modifier;

// Metadata only: never constructs AudioRecord or opens a microphone.
public final class AudioRecordFields {
    public static void main(String[] args) throws Exception {
        Class<?> cls=Class.forName("android.media.AudioRecord");
        Field nativeField=null;
        for(String name:new String[]{"mNativeAudioRecordHandle","mNativeRecorderInJavaObj"}) {
            try {nativeField=cls.getDeclaredField(name);break;}
            catch(NoSuchFieldException ignored) {}
        }
        if(nativeField==null||nativeField.getType()!=long.class)throw new IllegalStateException("No supported native record field");
        System.out.println("PASS: native pointer field = "+nativeField.getName()+" (long)");
        for(String name:new String[]{"getSampleRate","getChannelCount","getAudioFormat"}) {
            if(cls.getMethod(name).getReturnType()!=int.class)throw new IllegalStateException(name);
            System.out.println("PASS: public format accessor "+name+"()I");
        }
        Method start=cls.getDeclaredMethod("native_start",int.class,int.class);
        Method release=cls.getDeclaredMethod("native_release");
        if(!Modifier.isNative(start.getModifiers())||start.getReturnType()!=int.class
                ||!Modifier.isNative(release.getModifiers())||release.getReturnType()!=void.class)
            throw new IllegalStateException("Unsupported JNI signatures");
        System.out.println("PASS: native_start(II)I and native_release()V");
    }
}

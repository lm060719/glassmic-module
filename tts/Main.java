package io.mo.glassmic.tts;

import android.os.Binder;
import android.os.Bundle;
import android.os.IBinder;
import android.os.Parcel;
import android.os.ParcelFileDescriptor;
import java.io.File;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.util.Locale;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/** Headless client of Android's system TTS session broker. No Activity or APK. */
public final class Main {
    static final String PREFIX="android.speech.tts.";
    private static String quote(String s) {
        StringBuilder b=new StringBuilder("\"");
        for(char c:s.toCharArray()) {
            if(c=='"'||c=='\\')b.append('\\').append(c);
            else if(c<32)b.append(String.format("\\u%04x",(int)c));
            else b.append(c);
        }
        return b.append('"').toString();
    }
    private static void result(boolean ok,String message) {
        System.out.println("{\"ok\":"+ok+(ok?",\"file\":":",\"error\":")+quote(message)+"}");
    }
    static Object proxy(String name,IBinder binder) throws Exception {
        return Class.forName(PREFIX+name+"$Stub").getMethod("asInterface",IBinder.class).invoke(null,binder);
    }
    static Object call(Object target,String name,Object... args) throws Exception {
        for(Method m:target.getClass().getMethods())if(m.getName().equals(name)&&m.getParameterCount()==args.length) {
            m.setAccessible(true);
            try{return m.invoke(target,args);}
            catch(java.lang.reflect.InvocationTargetException e){
                Throwable cause=e.getCause();throw new Exception(cause==null?e.toString():cause.toString());
            }
        }
        throw new NoSuchMethodException(name);
    }
    static int transaction(String descriptor,String name) throws Exception {
        Field f=Class.forName(descriptor+"$Stub").getDeclaredField("TRANSACTION_"+name);
        f.setAccessible(true);return f.getInt(null);
    }
    static final class Session extends Binder {
        final String descriptor=PREFIX+"ITextToSpeechSessionCallback";
        final int connected,disconnected,error;
        final CountDownLatch ready=new CountDownLatch(1);
        volatile IBinder engine,session;volatile String failure;
        Session() throws Exception {
            attachInterface(null,descriptor);connected=transaction(descriptor,"onConnected");
            disconnected=transaction(descriptor,"onDisconnected");error=transaction(descriptor,"onError");
        }
        protected boolean onTransact(int code,Parcel data,Parcel reply,int flags) {
            if(code==INTERFACE_TRANSACTION){if(reply!=null)reply.writeString(descriptor);return true;}
            if(code==connected){data.enforceInterface(descriptor);session=data.readStrongBinder();engine=data.readStrongBinder();ready.countDown();return true;}
            if(code==error){data.enforceInterface(descriptor);failure=data.readString();ready.countDown();return true;}
            if(code==disconnected){failure="语音引擎已断开";ready.countDown();return true;}
            return false;
        }
    }
    static final class Progress extends Binder {
        final String descriptor=PREFIX+"ITextToSpeechCallback";
        final int success,error,stop;
        final CountDownLatch done=new CountDownLatch(1);
        volatile boolean ok=false;volatile String failure="语音合成失败";
        Progress() throws Exception {
            attachInterface(null,descriptor);success=transaction(descriptor,"onSuccess");
            error=transaction(descriptor,"onError");stop=transaction(descriptor,"onStop");
        }
        protected boolean onTransact(int code,Parcel data,Parcel reply,int flags) {
            if(code==INTERFACE_TRANSACTION){if(reply!=null)reply.writeString(descriptor);return true;}
            data.enforceInterface(descriptor);
            if(code==success){data.readString();ok=true;done.countDown();}
            else if(code==error){data.readString();failure="语音引擎错误："+data.readInt();done.countDown();}
            else if(code==stop){failure="语音合成已停止";done.countDown();}
            return true;
        }
    }
    public static void main(String[] args) {
        if(args.length<3||args[0].isEmpty()||args[0].length()>4000) {
            result(false,"请输入 1 到 4000 字的文字，并配置系统语音引擎");System.exit(1);return;
        }
        File output=new File(args[1]);Session connection=null;Object service=null;Binder identity=new Binder();
        boolean ok=false;String message="";
        try {
            IBinder binder=(IBinder)Class.forName("android.os.ServiceManager").getMethod("getService",String.class).invoke(null,"texttospeech");
            if(binder==null)throw new Exception("系统未提供 TTS 会话服务，当前系统无法无 APK 合成语音");
            Object manager=proxy("ITextToSpeechManager",binder);
            connection=new Session();
            call(manager,"createSession",args[2],proxy("ITextToSpeechSessionCallback",connection));
            if(!connection.ready.await(15,TimeUnit.SECONDS)||connection.engine==null)
                throw new Exception(connection.failure==null?"无法连接系统语音引擎":connection.failure);
            service=proxy("ITextToSpeechService",connection.engine);
            Progress progress=new Progress();
            call(service,"setCallback",identity,proxy("ITextToSpeechCallback",progress));
            Locale locale=Locale.forLanguageTag(args.length>3?args[3]:"zh-CN");
            String language=locale.getISO3Language(),country=locale.getISO3Country(),variant=locale.getVariant();
            int available=(Integer)call(service,"loadLanguage",identity,language,country,variant);
            if(available<0)throw new Exception("语音引擎缺少所选语言数据");
            Bundle params=new Bundle();
            params.putString("language",language);params.putString("country",country);params.putString("variant",variant);
            params.putInt("rate",Math.round(100*(args.length>4?Math.max(.5f,Math.min(2,Float.parseFloat(args[4]))):1)));
            params.putInt("pitch",Math.round(100*(args.length>5?Math.max(.5f,Math.min(2,Float.parseFloat(args[5]))):1)));
            try {
                String voice=(String)call(service,"getDefaultVoiceNameFor",language,country,variant);
                if(voice!=null){call(service,"loadVoice",identity,voice);params.putString("voiceName",voice);}
            } catch(Exception ignored) { /* Engines may implement only language selection. */ }
            try(ParcelFileDescriptor fd=ParcelFileDescriptor.open(output,ParcelFileDescriptor.MODE_CREATE|ParcelFileDescriptor.MODE_TRUNCATE|ParcelFileDescriptor.MODE_WRITE_ONLY)) {
                int code=(Integer)call(service,"synthesizeToFileDescriptor",identity,args[0],fd,params,"glassmic-module-tts");
                if(code!=0||!progress.done.await(60,TimeUnit.SECONDS)||!progress.ok||output.length()<=44)
                    throw new Exception(progress.failure);
            }
            ok=true;message=output.getName();
        } catch(Throwable error) {message=error.getMessage()==null?error.toString():error.getMessage();}
        finally {
            if(service!=null)try{call(service,"setCallback",identity,null);}catch(Exception ignored){}
            if(connection!=null&&connection.session!=null)try{call(proxy("ITextToSpeechSession",connection.session),"disconnect");}catch(Exception ignored){}
            if(!ok)output.delete();
        }
        result(ok,message);System.exit(ok?0:1);
    }
}

package io.mo.glassmic.overlay;

import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.content.Context;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.MediaSessionManager;
import android.media.session.PlaybackState;
import org.json.JSONObject;

/**
 * Test-only controller for the media card: finds the GlassMic session and sends transport commands through
 * the media session service, the same path SystemUI's card uses. Not packaged into the module.
 * Usage: MediaControl <info|play|pause|next|previous|seek MS|custom ACTION|channel>
 * channel reports the card's notification channel of the "android" package (uid 1000), including a deleted one.
 */
public final class MediaControl {
    public static void main(String[] args) {
        try {
            Context context=Main.systemContext();
            if(args.length>0&&args[0].equals("channel")){channel(context);return;}
            MediaController target=null;
            for(MediaController c:context.getSystemService(MediaSessionManager.class).getActiveSessions(null))
                if("android".equals(c.getPackageName())&&"GlassMic".equals(c.getTag()))target=c;
            if(target==null){Main.fail("GlassMic session not found");return;}
            String command=args.length>0?args[0]:"info";
            MediaController.TransportControls controls=target.getTransportControls();
            switch(command) {
                case "info":break;
                case "play":controls.play();break;
                case "pause":controls.pause();break;
                case "next":controls.skipToNext();break;
                case "previous":controls.skipToPrevious();break;
                case "seek":controls.seekTo(Long.parseLong(args[1]));break;
                case "custom":controls.sendCustomAction(args[1],null);break;
                default:Main.fail("unknown command "+command);return;
            }
            PlaybackState state=target.getPlaybackState();MediaMetadata metadata=target.getMetadata();
            JSONObject out=new JSONObject().put("ok",true).put("command",command);
            if(state!=null)out.put("state",state.getState()).put("positionMs",state.getPosition()).put("actions",state.getActions())
                .put("seekable",(state.getActions()&PlaybackState.ACTION_SEEK_TO)!=0).put("customActions",state.getCustomActions().size());
            if(metadata!=null)out.put("title",metadata.getString(MediaMetadata.METADATA_KEY_TITLE))
                .put("artist",metadata.getString(MediaMetadata.METADATA_KEY_ARTIST)).put("durationMs",metadata.getLong(MediaMetadata.METADATA_KEY_DURATION));
            System.out.println(out);System.exit(0);
        } catch(Throwable e) {e.printStackTrace(System.err);Main.fail(e.toString());}
    }
    static void channel(Context context) throws Exception {
        Object service=NotificationManager.class.getMethod("getService").invoke(null);
        NotificationChannel c=(NotificationChannel)service.getClass().getMethod("getNotificationChannelForPackage",String.class,int.class,String.class,String.class,boolean.class)
            .invoke(service,"android",android.os.Process.SYSTEM_UID,MediaCard.CHANNEL,null,true);
        JSONObject out=new JSONObject().put("ok",true).put("uid",android.os.Process.SYSTEM_UID).put("exists",c!=null);
        if(c!=null)out.put("name",String.valueOf(c.getName())).put("importance",c.getImportance()).put("deleted",(boolean)NotificationChannel.class.getMethod("isDeleted").invoke(c));
        System.out.println(out);System.exit(0);
    }
}

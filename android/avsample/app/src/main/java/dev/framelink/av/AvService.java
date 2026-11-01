package dev.framelink.av;
import android.app.Service;
import android.content.Intent;
import android.media.AudioAttributes;
import android.media.AudioDeviceCallback;
import android.media.AudioDeviceInfo;
import android.media.AudioFocusRequest;
import android.media.AudioManager;
import android.os.Binder;
import android.os.IBinder;
import android.os.Handler;
import android.os.Looper;
import android.view.Surface;
import java.util.concurrent.Executors;
/** Same-application bound A/V receiver. No exported endpoint or background playback. */
public final class AvService extends Service {
 static { System.loadLibrary("fl_av_receiver"); }
 private static native void run(String dir,Surface surface);
 private static native void control(boolean stop,boolean paused);
 private static final java.util.concurrent.ExecutorService queue=Executors.newSingleThreadExecutor();
 private AudioManager audio;
 private AudioFocusRequest focus;
 private volatile boolean destroyed;
 private boolean started;
 private String session;
 private volatile boolean focused;
 private final AudioDeviceCallback routes=new AudioDeviceCallback(){
  public void onAudioDevicesRemoved(AudioDeviceInfo[] devices){if(devices.length>0){focused=false;control(true,true);android.util.Log.i("FrameLinkAV","route removed: stop; reopen activity to renegotiate");}}
 };
 public final class LocalBinder extends Binder { public void start(Surface surface){startReceiver(surface);} }
 public IBinder onBind(Intent intent){return new LocalBinder();}
 public void onCreate(){super.onCreate();audio=getSystemService(AudioManager.class);
  focus=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN).setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MOVIE).build()).setOnAudioFocusChangeListener(change->{focused=change==AudioManager.AUDIOFOCUS_GAIN; if(!focused)control(true,true);android.util.Log.i("FrameLinkAV","focus="+change);}).build();
  audio.registerAudioDeviceCallback(routes,new Handler(Looper.getMainLooper()));
 }
 private void startReceiver(Surface surface){
  if(started || destroyed || !surface.isValid())return;
  focused=audio.requestAudioFocus(focus)==AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
  if(!focused){android.util.Log.w("FrameLinkAV","audio focus denied");return;}
  started=true;
  session=getFilesDir()+"/session-"+java.util.UUID.randomUUID();
  queue.execute(()->{if(destroyed || !focused)return;
   java.io.File[] stale=getFilesDir().listFiles();if(stale!=null)for(java.io.File d:stale)if(d.getName().startsWith("session-") && !d.getPath().equals(session)){java.io.File[] fs=d.listFiles();if(fs!=null)for(java.io.File f:fs)f.delete();d.delete();}
   control(false,false);if(destroyed || !focused){control(true,true);return;}run(session,surface);
   new Handler(Looper.getMainLooper()).post(()->{audio.abandonAudioFocusRequest(focus);focused=false;});
   java.io.File dir=new java.io.File(session);java.io.File[] files=dir.listFiles();if(files!=null)for(java.io.File f:files)f.delete();dir.delete();});
  startService(new Intent(this,ProducerService.class).putExtra("session",session));
 }
 public void onDestroy(){destroyed=true;control(true,true);audio.abandonAudioFocusRequest(focus);audio.unregisterAudioDeviceCallback(routes);stopService(new Intent(this,ProducerService.class));super.onDestroy();}
}

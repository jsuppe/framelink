package dev.framelink.av;
import android.app.Service;
import android.content.Intent;
import android.media.*;
import android.os.*;
/** Debug-only competing real AudioManager focus request. */
public class FocusTestService extends Service {
 AudioManager audio; AudioFocusRequest request;
 public IBinder onBind(Intent i){return null;}
 public int onStartCommand(Intent i,int flags,int id){
  audio=getSystemService(AudioManager.class);
  request=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT).setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build()).setOnAudioFocusChangeListener(change->{}).build();
  android.util.Log.i("FrameLinkAV","test competing focus="+audio.requestAudioFocus(request));
  new Handler(Looper.getMainLooper()).postDelayed(()->stopSelf(),1000);
  return START_NOT_STICKY;
 }
 public void onDestroy(){audio.abandonAudioFocusRequest(request);super.onDestroy();}
}

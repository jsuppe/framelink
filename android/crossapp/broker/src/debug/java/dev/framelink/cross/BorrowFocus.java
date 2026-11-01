package dev.framelink.cross;
import android.content.*;
import android.media.*;
import android.os.*;
/** Debug-only real AudioManager focus competition; no sound or global settings. */
public final class BorrowFocus extends BroadcastReceiver {
 public void onReceive(Context context,Intent intent){
  final PendingResult pending=goAsync();
  AudioManager manager=context.getSystemService(AudioManager.class);
  AudioFocusRequest request=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
   .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build())
   .setOnAudioFocusChangeListener(change->{}).build();
  int result=manager.requestAudioFocus(request);
  android.util.Log.i("FLCross","fault=borrow-focus granted="+(result==AudioManager.AUDIOFOCUS_REQUEST_GRANTED));
  new Handler(Looper.getMainLooper()).postDelayed(()->{manager.abandonAudioFocusRequest(request);pending.finish();},800);
 }
}

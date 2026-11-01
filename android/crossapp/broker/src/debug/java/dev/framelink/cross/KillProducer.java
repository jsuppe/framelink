package dev.framelink.cross;
import android.content.*;
/** Debug-only deterministic process-death qualification; absent from release. */
public final class KillProducer extends BroadcastReceiver {
 public void onReceive(Context c,Intent i){android.util.Log.i("FLCross","fault=kill-Producer pid="+android.os.Process.myPid());android.os.Process.killProcess(android.os.Process.myPid());}
}

package dev.framelink.av;
import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
public final class ProducerService extends Service {
 static { System.loadLibrary("fl_av_producer"); }
 private static native void run(String dir);
 private Thread worker;
 public IBinder onBind(Intent i) { return null; }
 // Fixture runs in its own manifest process: stop must also terminate blocking CLI calls.
 public void onDestroy(){super.onDestroy();android.os.Process.killProcess(android.os.Process.myPid());}
 public int onStartCommand(Intent i,int flags,int id) {
  if(worker==null && i!=null && i.getStringExtra("session")!=null) {final String session=i.getStringExtra("session");worker=new Thread(()->{run(session);stopSelf();},"av-producer");worker.start();}
  return START_NOT_STICKY;
 }
}

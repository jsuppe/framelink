package dev.framelink.av;
import android.app.Activity;
import android.os.Bundle;
import android.os.IBinder;
import android.content.*;
import android.view.*;
public final class MainActivity extends Activity implements SurfaceHolder.Callback {
 private boolean bound;
 private SurfaceView view;
 private AvService.LocalBinder receiver;
 private final ServiceConnection connection=new ServiceConnection(){
  public void onServiceConnected(ComponentName name,IBinder service){receiver=(AvService.LocalBinder)service; if(view.getHolder().getSurface().isValid())receiver.start(view.getHolder().getSurface());}
  public void onServiceDisconnected(ComponentName name){receiver=null;}
 };
 public void onCreate(Bundle b){super.onCreate(b);view=new SurfaceView(this);view.getHolder().addCallback(this);setContentView(view);}
 public void onStart(){super.onStart();bound=bindService(new Intent(this,AvService.class),connection,BIND_AUTO_CREATE);}
 protected void onNewIntent(Intent intent){super.onNewIntent(intent);if((getApplicationInfo().flags & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE)!=0 && intent.getBooleanExtra("recreate",false))recreate();}
 public void onStop(){disconnect();super.onStop();}
 private void disconnect(){if(bound){unbindService(connection);bound=false;}receiver=null;}
 public void surfaceCreated(SurfaceHolder h){if(receiver!=null)receiver.start(h.getSurface());}
 public void surfaceChanged(SurfaceHolder h,int f,int w,int height){}
 public void surfaceDestroyed(SurfaceHolder h){disconnect();}
}

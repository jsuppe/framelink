package dev.framelink.cross;
import android.app.Activity;
import android.content.*;
import android.media.*;
import android.os.*;
import android.view.*;
import android.widget.*;
import android.util.Log;
/** Foreground-only cross-UID example. Reopen creates a fresh broker lease. */
public final class Viewer extends Activity implements SurfaceHolder.Callback {
 private final Object lock=new Object();
 private final IBinder owner=new Binder();
 private boolean bound,started,destroyed;
 private IBroker broker;private Lease lease;private TextView status;
 private AudioManager audio;private AudioFocusRequest focus;
 private Surface surface;
 private final AudioDeviceCallback routes=new AudioDeviceCallback(){public void onAudioDevicesRemoved(AudioDeviceInfo[] devices){if(devices.length>0)stop("audio-route");}};
 private final ServiceConnection connection=new ServiceConnection(){
  public void onServiceConnected(ComponentName name,IBinder binder){synchronized(lock){if(destroyed)return;}broker=IBroker.Stub.asInterface(binder);new Thread(Viewer.this::receive,"crossapp-receiver").start();}
  public void onServiceDisconnected(ComponentName name){stop("broker-death");}
  public void onBindingDied(ComponentName name){stop("broker-binding-death");}
 };
 public void onCreate(Bundle saved){super.onCreate(saved);
  LinearLayout layout=new LinearLayout(this);layout.setOrientation(LinearLayout.VERTICAL);
  status=new TextView(this);status.setText("FrameLink cross-app video + audio");layout.addView(status);
  SurfaceView view=new SurfaceView(this);view.getHolder().addCallback(this);layout.addView(view,new LinearLayout.LayoutParams(-1,0,1));setContentView(layout);
  audio=getSystemService(AudioManager.class);
  focus=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN).setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).build()).setOnAudioFocusChangeListener(change->{if(change!=AudioManager.AUDIOFOCUS_GAIN)stop("audio-focus");}).build();
  audio.registerAudioDeviceCallback(routes,new Handler(Looper.getMainLooper()));
 }
 public void surfaceCreated(SurfaceHolder holder){
  if(started||destroyed)return;started=true;surface=holder.getSurface();
  if(!getIntent().getBooleanExtra("silent",false) && audio.requestAudioFocus(focus)!=AudioManager.AUDIOFOCUS_REQUEST_GRANTED){status.setText("Audio focus unavailable");return;}
  try{bound=bindService(new Intent().setComponent(new ComponentName("dev.framelink.cross.broker","dev.framelink.cross.Broker")),connection,BIND_AUTO_CREATE);Log.i("FLCross","bind="+bound);}
  catch(SecurityException e){Log.i("FLCross","permission_rejected=true");status.setText("Broker permission rejected");}
 }
 private void receive(){
  IBroker remote=broker;Lease current=null;IBinder currentOwner=owner;
  try{
   String format=getIntent().getStringExtra("format");if(format==null)format="BGRA";
   Bundle data=remote.acquire(owner,format,getIntent().getIntExtra("frames",90),getIntent().getIntExtra("fps",30));
   current=new Lease(data);synchronized(lock){if(destroyed){current.close();return;}lease=current;}
   final String label=format;runOnUiThread(()->status.setText("Cross-app "+label+" • native shared video + GDP audio"));
   if(getIntent().getBooleanExtra("diagnostics",false))Native.diagnostics(getIntent().getIntExtra("diagnostic-level",5));
   Log.i("FLCross","receiver-start lease="+data.getString("lease")+" generation="+data.getLong("generation")+" uid="+android.os.Process.myUid()+" pid="+android.os.Process.myPid());
   if(getIntent().getBooleanExtra("wrong-key",false)){byte[] key=data.getByteArray("key");key[0]^=1;}
   final boolean replace="replace".equals(getIntent().getStringExtra("mode"));
   final IBinder nextOwner=new Binder();
   final java.util.concurrent.CompletableFuture<Bundle> next=new java.util.concurrent.CompletableFuture<>();
   if(replace){
    boolean rejected=false;try{Bundle invalid=remote.acquire(owner,format,data.getInt("frames"),data.getInt("fps"));new Lease(invalid).close();}catch(IllegalArgumentException expected){rejected=true;}
    if(!rejected)throw new IllegalStateException("owner reuse accepted");
    Log.i("FLCross","active_owner_reuse_rejected=true");
   }
   if(replace || "revoke".equals(getIntent().getStringExtra("mode"))){
    final String selected=format;final int count=data.getInt("frames"),rate=data.getInt("fps");
    new Thread(()->{try{Thread.sleep(700);if(replace)next.complete(remote.acquire(nextOwner,selected,count,rate));else remote.revoke(owner);}catch(Exception e){next.completeExceptionally(e);}},"lease-transition").start();
   }
   String result=Native.run(false,current.detach(),data.getByteArray("key"),format,data.getInt("frames"),data.getInt("fps"),!getIntent().getBooleanExtra("silent",false),surface);
   if(replace){
    Log.i("FLCross","replaced-old-result lease="+data.getString("lease")+" "+result);
    current.close();data=next.get(10,java.util.concurrent.TimeUnit.SECONDS);currentOwner=nextOwner;
    remote.revoke(owner); // stale owner must not revoke the replacement
    current=new Lease(data);synchronized(lock){if(destroyed){current.close();return;}lease=current;}
    Log.i("FLCross","receiver-replacement lease="+data.getString("lease")+" generation="+data.getLong("generation"));
    result=Native.run(false,current.detach(),data.getByteArray("key"),format,data.getInt("frames"),data.getInt("fps"),!getIntent().getBooleanExtra("silent",false),surface);
   }
   Log.i("FLCross","receiver-result lease="+data.getString("lease")+" "+result);
   final String display=result;runOnUiThread(()->status.setText("FrameLink result: "+display));
  }catch(SecurityException e){Log.i("FLCross","permission_rejected=true");}
  catch(Exception e){Log.i("FLCross","receiver-stopped reason="+e.getClass().getSimpleName());}
  finally{if(current!=null)current.close();try{remote.revoke(currentOwner);}catch(Exception ignored){}synchronized(lock){if(lease==current)lease=null;}audio.abandonAudioFocusRequest(focus);}
 }
 private void stop(String reason){synchronized(lock){destroyed=true;if(lease!=null)lease.close();}Log.i("FLCross","stop="+reason);}
 public void surfaceDestroyed(SurfaceHolder holder){stop("surface-destroyed");}
 public void surfaceChanged(SurfaceHolder h,int format,int width,int height){}
 public void onStop(){stop("background");super.onStop();}
 public void onDestroy(){stop("destroy");if(bound)unbindService(connection);audio.abandonAudioFocusRequest(focus);audio.unregisterAudioDeviceCallback(routes);super.onDestroy();}
}

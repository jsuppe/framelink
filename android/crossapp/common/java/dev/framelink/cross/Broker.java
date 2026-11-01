package dev.framelink.cross;
import android.app.Service;
import android.content.*;
import android.os.*;
import android.util.Log;
import java.security.SecureRandom;
import java.util.UUID;
/** One active, signature-authorized A/V lease; producer runs in a separate process. */
public final class Broker extends Service {
 private final Object lock=new Object();
 private final IBinder lifetime=new Binder();
 private IProducer runner;
 private boolean bound,closing;
 private long generation;
 private Grant active;
 private final class Grant implements IBinder.DeathRecipient {
  final int uid; final IBinder owner; final String id=UUID.randomUUID().toString();
  final long version=++generation; final byte[] key=new byte[32];
  final ParcelFileDescriptor[] server=new ParcelFileDescriptor[3],client=new ParcelFileDescriptor[3];
  final java.util.ArrayList<ParcelFileDescriptor> revokers=new java.util.ArrayList<>();
  boolean closed,linked;
  Grant(int uid,IBinder owner){this.uid=uid;this.owner=owner;new SecureRandom().nextBytes(key);}
  void close(String reason){synchronized(lock){
   if(closed)return;closed=true;
   for(ParcelFileDescriptor fd:revokers){Native.shutdown(fd.getFd());try{fd.close();}catch(Exception ignored){}}
   for(ParcelFileDescriptor[] ends:new ParcelFileDescriptor[][]{server,client})for(ParcelFileDescriptor fd:ends)if(fd!=null)try{fd.close();}catch(Exception ignored){}
   if(linked){owner.unlinkToDeath(this,0);linked=false;}
   if(active==this)active=null;
   java.util.Arrays.fill(key,(byte)0);
   Log.i("FLCross","retired lease="+id+" generation="+version+" reason="+reason);
  }}
  public void binderDied(){close("consumer-death");}
  Bundle bundle(boolean producer,String format,int frames,int fps,boolean producerClock){
   Bundle b=new Bundle();b.putString("lease",id);b.putLong("generation",version);
   b.putByteArray("key",key.clone());b.putParcelableArray("fds",producer?server:client);
   b.putBoolean("producer-clock",producerClock);b.putString("format",format);b.putInt("frames",frames);b.putInt("fps",fps);return b;
  }
 }
 private final ServiceConnection producerConnection=new ServiceConnection(){
  public void onServiceConnected(ComponentName name,IBinder binder){synchronized(lock){runner=IProducer.Stub.asInterface(binder);lock.notifyAll();}}
  public void onServiceDisconnected(ComponentName name){synchronized(lock){runner=null;if(active!=null)active.close("producer-death");lock.notifyAll();}}
  public void onBindingDied(ComponentName name){onServiceDisconnected(name);}
 };
 private final IBroker.Stub service=new IBroker.Stub(){
  public Bundle acquire(IBinder owner,String format,int frames,int fps,boolean producerClock)throws RemoteException {
   enforceCallingPermission("dev.framelink.cross.BIND","signature required");
   if(owner==null || !("BGRA".equals(format)||"NV12".equals(format)) || frames<1 || frames>18000 || fps<13 || fps>120 || (long)frames*1000>300000L*fps)throw new IllegalArgumentException("invalid bounded media request");
   if(!producerClock && (long)frames*1000>10000L*fps)throw new IllegalArgumentException("audio-clock sample mode is bounded to ten seconds");
   synchronized(lock){
    long deadline=SystemClock.elapsedRealtime()+10000;
    while(runner==null && !closing){long left=deadline-SystemClock.elapsedRealtime();if(left<=0)throw new RemoteException("producer service deadline");try{lock.wait(left);}catch(InterruptedException e){Thread.currentThread().interrupt();throw new RemoteException("interrupted");}}
    if(closing)throw new RemoteException("broker closing");
    int uid=Binder.getCallingUid();
    if(active!=null){
     if(active.uid!=uid)throw new IllegalStateException("one active consumer supported");
     if(active.owner.equals(owner))throw new IllegalArgumentException("fresh owner token required");
     active.close("replacement");
    }
    Grant grant=new Grant(uid,owner);
    try{
     for(int n=0;n<2;n++){ParcelFileDescriptor[] pair=ParcelFileDescriptor.createSocketPair();grant.server[n]=pair[0];grant.client[n]=pair[1];}
     int[] nativePair=Native.pair();if(nativePair==null)throw new java.io.IOException("native video socketpair");
     grant.server[2]=ParcelFileDescriptor.adoptFd(nativePair[0]);grant.client[2]=ParcelFileDescriptor.adoptFd(nativePair[1]);
     for(ParcelFileDescriptor[] ends:new ParcelFileDescriptor[][]{grant.server,grant.client})for(ParcelFileDescriptor fd:ends)grant.revokers.add(ParcelFileDescriptor.dup(fd.getFileDescriptor()));
     owner.linkToDeath(grant,0);grant.linked=true;active=grant;
     runner.start(grant.bundle(true,format,frames,fps,producerClock),lifetime);
     Log.i("FLCross","grant lease="+grant.id+" generation="+grant.version+" consumer_uid="+uid+" broker_uid="+android.os.Process.myUid());
     return grant.bundle(false,format,frames,fps,producerClock);
    }catch(Exception e){grant.close("setup-failed");throw new RemoteException("lease setup failed: "+e.getClass().getSimpleName());}
   }
  }
  public void revoke(IBinder owner){enforceCallingPermission("dev.framelink.cross.BIND","signature required");synchronized(lock){if(active!=null && active.uid==Binder.getCallingUid() && active.owner.equals(owner))active.close("explicit");}}
 };
 public IBinder onBind(Intent i){return service;}
 public void onCreate(){super.onCreate();bound=bindService(new Intent(this,Producer.class),producerConnection,BIND_AUTO_CREATE);}
 public void onDestroy(){synchronized(lock){closing=true;if(active!=null)active.close("broker-destroy");lock.notifyAll();}if(bound)unbindService(producerConnection);super.onDestroy();}
}

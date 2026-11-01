package dev.framelink.cross;
import android.app.Service;
import android.content.Intent;
import android.os.*;
import android.util.Log;
import java.util.concurrent.Executors;
public final class Producer extends Service {
 private final Object lock=new Object();
 private final java.util.concurrent.ExecutorService queue=Executors.newSingleThreadExecutor();
 private Lease active;
 private final IProducer.Stub service=new IProducer.Stub(){
  public void start(Bundle data,IBinder brokerOwner)throws RemoteException {
   if(Binder.getCallingUid()!=android.os.Process.myUid())throw new SecurityException("same app producer controller only");
   final Lease lease;
   try{lease=new Lease(data);}catch(Exception e){throw new RemoteException("invalid producer lease");}
   final IBinder.DeathRecipient died=()->{lease.close();Log.i("FLCross","producer broker-death lease="+data.getString("lease"));};
   try{brokerOwner.linkToDeath(died,0);}catch(RemoteException e){lease.close();throw e;}
   synchronized(lock){if(active!=null)active.close();active=lease;}
   queue.execute(()->{
    try{
     Log.i("FLCross","producer-start lease="+data.getString("lease")+" uid="+android.os.Process.myUid()+" pid="+android.os.Process.myPid());
     String result=Native.run(true,lease.detach(),data.getByteArray("key"),data.getString("format"),data.getInt("frames"),data.getInt("fps"),false,data.getBoolean("producer-clock"),null);
     Log.i("FLCross","producer-result lease="+data.getString("lease")+" "+result);
    }catch(Exception e){Log.i("FLCross","producer-stopped lease="+data.getString("lease")+" reason="+e.getClass().getSimpleName());}
    finally{lease.close();brokerOwner.unlinkToDeath(died,0);synchronized(lock){if(active==lease)active=null;}}
   });
  }
 };
 public IBinder onBind(Intent i){return service;}
 public void onDestroy(){synchronized(lock){if(active!=null)active.close();}queue.shutdown();super.onDestroy();}
}

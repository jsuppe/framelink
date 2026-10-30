package dev.framelink.spike;
import android.app.Service;
import android.content.Intent;
import android.os.*;
import android.util.Log;
import java.util.HashMap;
public final class Broker extends Service {
 private final HashMap<Integer,Lease> leases=new HashMap<>();
 private final class Lease implements IBinder.DeathRecipient {
  final int uid,fd;final IBinder owner;boolean closed;
  Lease(int uid,int fd,IBinder owner){this.uid=uid;this.fd=fd;this.owner=owner;}
  void close(String reason){synchronized(leases){if(closed)return;closed=true;Bridge.stop(fd);owner.unlinkToDeath(this,0);if(leases.get(uid)==this)leases.remove(uid);Log.i("FLBroker","revoke uid="+uid+" reason="+reason);}}
  public void binderDied(){close("owner-death");}
 }
 private final ILease.Stub binder=new ILease.Stub(){
  public ParcelFileDescriptor acquire(IBinder owner)throws RemoteException{
   enforceCallingPermission("dev.framelink.spike.BIND_LEASE","signature grant required");
   if(owner==null)throw new IllegalArgumentException("owner required");
   int uid=Binder.getCallingUid();int[] pair=Bridge.pair();
   if(pair==null)throw new IllegalStateException("socketpair failed");
   ParcelFileDescriptor peer=ParcelFileDescriptor.adoptFd(pair[1]);Lease lease=new Lease(uid,pair[0],owner);
   synchronized(leases){
   try{owner.linkToDeath(lease,0);}catch(RemoteException e){Bridge.stop(pair[0]);Bridge.destroy(pair[0]);try{peer.close();}catch(Exception ignored){}throw e;}
   Lease old=leases.get(uid);if(old!=null)old.close("replacement");leases.put(uid,lease);}
   Log.i("FLBroker","grant uid="+uid+" broker_uid="+android.os.Process.myUid());
   new Thread(()->{int result=Bridge.serve(lease.fd);Log.i("FLBroker","transfer result="+result);lease.close("transport-end");Bridge.destroy(lease.fd);},"broker-transport").start();
   return peer;
  }
  public void revoke(IBinder owner){enforceCallingPermission("dev.framelink.spike.BIND_LEASE","signature required");synchronized(leases){Lease lease=leases.get(Binder.getCallingUid());if(lease!=null && lease.owner.equals(owner))lease.close("explicit");}}
 };
 public IBinder onBind(Intent i){return binder;}
 public void onDestroy(){synchronized(leases){for(Lease l:leases.values().toArray(new Lease[0]))l.close("service-destroy");}super.onDestroy();}
}

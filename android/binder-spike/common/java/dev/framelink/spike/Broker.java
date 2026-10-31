package dev.framelink.spike;
import android.app.Service;
import android.content.Intent;
import android.os.*;
import android.util.Log;
import java.util.HashMap;
public final class Broker extends Service {
 private final HashMap<Integer,Lease> leases=new HashMap<>();
 private final HashMap<Integer,StreamGrant> streams=new HashMap<>();
 // Separate from the AHB SEQPACKET grant: GDP/control require byte streams.
 private final class StreamGrant implements IBinder.DeathRecipient {
  final int uid; final IBinder owner; final ParcelFileDescriptor[] server; boolean closed,linked;
  StreamGrant(int uid,IBinder owner,ParcelFileDescriptor[] server){this.uid=uid;this.owner=owner;this.server=server;}
  void close(String reason){synchronized(streams){if(closed)return;closed=true;for(ParcelFileDescriptor fd:server)Bridge.stop(fd.getFd());if(linked){owner.unlinkToDeath(this,0);linked=false;}if(streams.get(uid)==this)streams.remove(uid);Log.i("FLBroker","stream_revoke="+reason);}}
  public void binderDied(){close("owner-death");}
 }
 private void serveStream(StreamGrant grant,int index){
  ParcelFileDescriptor fd=grant.server[index];
  try{boolean eof=Bridge.streamServe(fd.getFd(),index==0?'C':'A');Log.i("FLBroker","stream_peer_eof="+eof);}
  finally{grant.close("transport-end");try{fd.close();}catch(Exception ignored){}}
 }
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
  public ParcelFileDescriptor[] acquireStreams(IBinder owner)throws RemoteException{
   enforceCallingPermission("dev.framelink.spike.BIND_LEASE","signature grant required");
   if(owner==null)throw new IllegalArgumentException("owner required");
   ParcelFileDescriptor[] server=new ParcelFileDescriptor[2],peer=new ParcelFileDescriptor[2];
   boolean[] started=new boolean[2];boolean handedOff=false;StreamGrant grant=null;
   try{
    for(int n=0;n<2;n++){ParcelFileDescriptor[] pair=ParcelFileDescriptor.createSocketPair();server[n]=pair[0];peer[n]=pair[1];}
    int uid=Binder.getCallingUid();grant=new StreamGrant(uid,owner,server);
    synchronized(streams){
     StreamGrant old=streams.get(uid);
     // A token identifies this active grant, not an indefinitely reusable UID.
     if(old!=null && old.owner.equals(owner))throw new IllegalArgumentException("fresh owner token required");
     owner.linkToDeath(grant,0);grant.linked=true;
     if(old!=null)old.close("replacement");streams.put(uid,grant);
     final StreamGrant active=grant;
     for(int n=0;n<2;n++){final int index=n;new Thread(()->serveStream(active,index),"stream-lease-"+n).start();started[n]=true;}
    }
    Log.i("FLBroker","stream_grant uid="+uid+" broker_uid="+android.os.Process.myUid());
    handedOff=true;return peer;
   }catch(java.io.IOException failure){throw new RemoteException("stream lease failed: "+failure);}
   finally{if(!handedOff){
    if(grant!=null)grant.close("setup-failed");
    // A started worker is the sole closer of its server FD. Shutdown first,
    // then close only unstarted ends; never race raw descriptor reuse.
    for(int n=0;n<2;n++){
     if(!started[n] && server[n]!=null)try{server[n].close();}catch(Exception ignored){}
     if(peer[n]!=null)try{peer[n].close();}catch(Exception ignored){}
    }
   }}
  }
  public void revoke(IBinder owner){enforceCallingPermission("dev.framelink.spike.BIND_LEASE","signature required");synchronized(leases){Lease lease=leases.get(Binder.getCallingUid());if(lease!=null && lease.owner.equals(owner))lease.close("explicit");}synchronized(streams){StreamGrant grant=streams.get(Binder.getCallingUid());if(grant!=null && grant.owner.equals(owner))grant.close("explicit");}}
 };
 public IBinder onBind(Intent i){return binder;}
 public void onDestroy(){synchronized(streams){for(StreamGrant g:streams.values().toArray(new StreamGrant[0]))g.close("service-destroy");}synchronized(leases){for(Lease l:leases.values().toArray(new Lease[0]))l.close("service-destroy");}super.onDestroy();}
}

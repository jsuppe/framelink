package dev.framelink.spike;
import android.app.Activity;
import android.content.*;
import android.os.*;
import android.util.Log;
public final class Client extends Activity {
 private boolean bound;
 private IBinder owner=new Binder();
 private boolean ownerBound, ownerConnected;
 private final ServiceConnection ownerConn=new ServiceConnection(){public void onServiceConnected(ComponentName n,IBinder token){if(ownerConnected)return;ownerConnected=true;owner=token;bindBroker();}public void onServiceDisconnected(ComponentName n){}};
 private ParcelFileDescriptor peer;
 private final ServiceConnection conn=new ServiceConnection(){
  public void onServiceConnected(ComponentName n,IBinder b){new Thread(()->{
   try{ILease broker=ILease.Stub.asInterface(b);peer=broker.acquire(owner);
    boolean pixel=Bridge.receive(peer.getFd());Log.i("FLClient","pixel_roundtrip="+pixel+" uid="+android.os.Process.myUid());
    if("replace".equals(getIntent().getStringExtra("mode"))){IBinder next=new Binder();ParcelFileDescriptor replacement=broker.acquire(next);boolean retired=Bridge.eof(peer.getFd());broker.revoke(owner);boolean fresh=Bridge.receive(replacement.getFd());broker.revoke(next);Log.i("FLClient","replacement_safe="+(retired && fresh && Bridge.eof(replacement.getFd())));replacement.close();peer.close();return;}
    if("death".equals(getIntent().getStringExtra("mode"))){android.os.Process.killProcess(android.os.Process.myPid());return;}
    if("owner-token-death".equals(getIntent().getStringExtra("mode"))){Parcel data=Parcel.obtain();try{owner.transact(1,data,null,IBinder.FLAG_ONEWAY);}finally{data.recycle();}Log.i("FLClient","owner_death_eof="+Bridge.eof(peer.getFd()));peer.close();return;}
    broker.revoke(owner);Log.i("FLClient","revoked_eof="+Bridge.eof(peer.getFd()));peer.close();
   }catch(Exception e){Log.e("FLClient","failed",e);}
  },"client-transport").start();}
  public void onServiceDisconnected(ComponentName n){Log.i("FLClient","broker disconnected");}
 };
 public void onCreate(Bundle b){super.onCreate(b);if("owner-token-death".equals(getIntent().getStringExtra("mode")))ownerBound=bindService(new Intent(this,OwnerService.class),ownerConn,BIND_AUTO_CREATE);else bindBroker();}
 private void bindBroker(){try{Intent i=new Intent().setComponent(new ComponentName("dev.framelink.spike.broker","dev.framelink.spike.Broker"));bound=bindService(i,conn,BIND_AUTO_CREATE);Log.i("FLClient","bind="+bound);}catch(SecurityException e){Log.i("FLClient","permission_rejected=true");}}
 public void onDestroy(){if(ownerBound)unbindService(ownerConn);if(bound)unbindService(conn);try{if(peer!=null)peer.close();}catch(Exception ignored){}super.onDestroy();}
}

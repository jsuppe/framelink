package dev.framelink.spike;
import android.app.Service;
import android.content.Intent;
import android.os.*;
/** Test-only remote owner: death is independent of the client's socket FD. */
public final class OwnerService extends Service {
 private final Binder token=new Binder(){protected boolean onTransact(int code,Parcel data,Parcel reply,int flags){if(code==1){android.os.Process.killProcess(android.os.Process.myPid());return true;}return false;}};
 public IBinder onBind(Intent intent){return token;}
}

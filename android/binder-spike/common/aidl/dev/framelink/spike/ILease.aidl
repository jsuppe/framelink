package dev.framelink.spike;
import android.os.IBinder;
import android.os.ParcelFileDescriptor;
interface ILease {
 ParcelFileDescriptor acquire(IBinder owner);
 ParcelFileDescriptor[] acquireStreams(IBinder owner);
 void revoke(IBinder owner);
}

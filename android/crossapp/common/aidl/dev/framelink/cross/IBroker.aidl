package dev.framelink.cross;
import android.os.Bundle;
import android.os.IBinder;
interface IBroker {
 Bundle acquire(IBinder owner, String format, int frames, int fps);
 void revoke(IBinder owner);
}

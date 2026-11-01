package dev.framelink.cross;
import android.view.Surface;
final class Native {
 static {System.loadLibrary("fl_crossapp");}
 static native int[] pair();
 static native void shutdown(int fd);
 static native void diagnostics(int level);
 // Owns every descriptor supplied, including native failure paths.
 static native String run(boolean producer,int[] fds,byte[] key,String format,int frames,int fps,boolean audible,Surface surface);
}

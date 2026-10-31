package dev.framelink.spike;
final class Bridge {
 static {System.loadLibrary("fl_binder_spike");}
 static native int[] pair();
 static native boolean streamServe(int fd,int role);
 static native boolean streamReceive(int fd,int role);
 static native int serve(int fd);
 static native boolean receive(int fd);
 static native boolean eof(int fd);
 static native void destroy(int fd);
 static native void stop(int fd);
}

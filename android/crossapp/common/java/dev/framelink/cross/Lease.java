package dev.framelink.cross;
import android.os.*;
/** Stable owning duplicates for cancellation, never shutdown a reused raw FD. */
final class Lease implements AutoCloseable {
 final Bundle data;
 final ParcelFileDescriptor[] media=new ParcelFileDescriptor[3], cancel=new ParcelFileDescriptor[3];
 boolean retired;
 Lease(Bundle data) throws java.io.IOException {
  this.data=data;
  Parcelable[] raw=data.getParcelableArray("fds");
  if(raw==null || raw.length!=3)throw new IllegalArgumentException("three endpoints required");
  try {for(int n=0;n<3;n++){media[n]=(ParcelFileDescriptor)raw[n];cancel[n]=ParcelFileDescriptor.dup(media[n].getFileDescriptor());}}
  catch(Exception error){close();throw new java.io.IOException(error);}
 }
 synchronized int[] detach() {
  if(retired)throw new IllegalStateException("retired lease");
  int[] fds=new int[3];for(int n=0;n<3;n++)fds[n]=media[n].detachFd();return fds;
 }
 synchronized public void close(){
  if(retired)return;retired=true;
  for(ParcelFileDescriptor fd:cancel)if(fd!=null){Native.shutdown(fd.getFd());try{fd.close();}catch(Exception ignored){}}
  for(ParcelFileDescriptor fd:media)if(fd!=null)try{fd.close();}catch(Exception ignored){}
 }
}

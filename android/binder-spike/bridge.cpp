#include <jni.h>
#include <android/hardware_buffer.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdint>
extern "C" JNIEXPORT jintArray JNICALL Java_dev_framelink_spike_Bridge_pair(JNIEnv* env,jclass){
 int fd[2];if(socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,fd))return nullptr;
 timeval limit{5,0};for(int f:fd){setsockopt(f,SOL_SOCKET,SO_RCVTIMEO,&limit,sizeof(limit));setsockopt(f,SOL_SOCKET,SO_SNDTIMEO,&limit,sizeof(limit));}
 jintArray result=env->NewIntArray(2);env->SetIntArrayRegion(result,0,2,fd);return result;
}
extern "C" JNIEXPORT void JNICALL Java_dev_framelink_spike_Bridge_stop(JNIEnv*,jclass,jint fd){shutdown(fd,SHUT_RDWR);}
extern "C" JNIEXPORT void JNICALL Java_dev_framelink_spike_Bridge_destroy(JNIEnv*,jclass,jint fd){close(fd);}
extern "C" JNIEXPORT jint JNICALL Java_dev_framelink_spike_Bridge_serve(JNIEnv*,jclass,jint fd){
 AHardwareBuffer_Desc desc{};desc.width=4;desc.height=4;desc.layers=1;desc.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;desc.usage=AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
 AHardwareBuffer* buffer=nullptr;if(AHardwareBuffer_allocate(&desc,&buffer))return 1;
 struct Owner {AHardwareBuffer* b;~Owner(){AHardwareBuffer_release(b);}} owner{buffer};
 void* data=nullptr;if(AHardwareBuffer_lock(buffer,AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&data))return 2;
 *static_cast<uint32_t*>(data)=0x12345678;AHardwareBuffer_unlock(buffer,nullptr);
 if(AHardwareBuffer_sendHandleToUnixSocket(buffer,fd))return 3;
 char ack=0;if(recv(fd,&ack,1,0)!=1 || ack!='A')return 4;
 if(AHardwareBuffer_lock(buffer,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN,-1,nullptr,&data))return 5;
 bool same=*static_cast<uint32_t*>(data)==0xabcdef01;AHardwareBuffer_unlock(buffer,nullptr);
 ack=same?'Y':'N';if(send(fd,&ack,1,MSG_NOSIGNAL)!=1 || !same)return 6;
 // Retain the lease until explicit revoke, owner death, or bounded timeout.
 return recv(fd,&ack,1,0)==0 ? 0 : 7;
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_framelink_spike_Bridge_receive(JNIEnv*,jclass,jint fd){
 AHardwareBuffer* buffer=nullptr;if(AHardwareBuffer_recvHandleFromUnixSocket(fd,&buffer)||!buffer)return false;
 void* data=nullptr;if(AHardwareBuffer_lock(buffer,AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN|AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,-1,nullptr,&data)){AHardwareBuffer_release(buffer);return false;}
 bool good=*static_cast<uint32_t*>(data)==0x12345678;*static_cast<uint32_t*>(data)=0xabcdef01;
 AHardwareBuffer_unlock(buffer,nullptr);AHardwareBuffer_release(buffer);
 char ack=good?'A':'N';if(send(fd,&ack,1,MSG_NOSIGNAL)!=1)return false;
 return recv(fd,&ack,1,0)==1 && ack=='Y';
}
extern "C" JNIEXPORT jboolean JNICALL Java_dev_framelink_spike_Bridge_eof(JNIEnv*,jclass,jint fd){char b;return recv(fd,&b,1,0)==0;}

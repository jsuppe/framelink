#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <sys/resource.h>
static uint64_t ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void *(*volatile copyfn)(void*,const void*,size_t)=memcpy;
int main(void){size_t n=1920*1080*4;unsigned char*s=malloc(n);if(!s)return 2;for(size_t j=0;j<n;j++)s[j]=(j*13+7)%251;
puts("iteration,mode,setup_ns,copy_ns,minor_faults,valid");
for(int i=0;i<100;i++)for(int k=0;k<2;k++){int mode=(i+k)%2;uint64_t a=ns();int fd=syscall(SYS_memfd_create,"probe",1);if(fd<0||ftruncate(fd,n))return 3;unsigned char*d=mmap(0,n,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);if(d==MAP_FAILED)return 4;if(mode)for(size_t j=0;j<n;j+=4096)((volatile unsigned char*)d)[j]=0;uint64_t b=ns();struct rusage before,after;getrusage(RUSAGE_SELF,&before);uint64_t c=ns();copyfn(d,s,n);uint64_t e=ns();getrusage(RUSAGE_SELF,&after);int valid=memcmp(d,s,n)==0;printf("%d,%d,%llu,%llu,%ld,%d\n",i,mode,(unsigned long long)(b-a),(unsigned long long)(e-c),after.ru_minflt-before.ru_minflt,valid);munmap(d,n);close(fd);if(!valid)return 5;struct timespec wait={0,16666667};nanosleep(&wait,0);}free(s);return 0;}

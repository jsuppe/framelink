#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
static uint64_t ns(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void *(*volatile copyfn)(void*,const void*,size_t)=memcpy;
static void *(*volatile zerofn)(void*,int,size_t)=memset;
int main(void){const size_t row=1920*4,rows=1080,n=row*rows;unsigned char *src=malloc(n),*reuse=malloc(n);if(!src||!reuse)return 2;for(size_t i=0;i<n;i++)src[i]=(i*13+7)%251;memset(reuse,0,n);puts("iteration,mode,ns,valid");
for(int i=0;i<1800;i++)for(int k=0;k<4;k++){int mode=(k+i)%4;uint64_t begin=ns();unsigned char*d=mode==3?reuse:malloc(n);if(!d)return 3;if(mode==0)zerofn(d,0,n);if(mode<2){for(size_t y=0;y<rows;y++)copyfn(d+y*row,src+y*row,row);}else copyfn(d,src,n);uint64_t end=ns();int valid=memcmp(d,src,n)==0;printf("%d,%d,%llu,%d\n",i,mode,(unsigned long long)(end-begin),valid);if(mode!=3)free(d);if(!valid)return 4;uint64_t deadline=begin+16666667;struct timespec t={(time_t)(deadline/1000000000),(long)(deadline%1000000000)};clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&t,0);}free(src);free(reuse);return 0;}

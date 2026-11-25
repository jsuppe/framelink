// Diagnostic LD_PRELOAD shim: logs every malloc/calloc/realloc/posix_memalign/
// aligned_alloc/memalign of 8 MiB or more (and the free of such a block) with
// time, pid, thread name and a backtrace, to $BIGALLOC_LOG.<pid>.
// Build: cc -O1 -g -shared -fPIC -o bigalloc_trace.so bigalloc_trace.c -ldl
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

#define BIG (8u << 20)
#define SLOTS 256
static void *(*real_malloc)(size_t), *(*real_calloc)(size_t, size_t), *(*real_realloc)(void *, size_t);
static void (*real_free)(void *);
static int (*real_posix_memalign)(void **, size_t, size_t);
static void *(*real_aligned_alloc)(size_t, size_t), *(*real_memalign)(size_t, size_t);
static __thread int busy;
static int fd = -1;
static void *live[SLOTS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
// dlsym may call calloc before real_calloc is known: serve it from here.
static char boot[4096];
static size_t bootUsed;
static int initializing;

static void init(void) {
  if (real_malloc || initializing) return;
  initializing = 1;
  real_malloc = dlsym(RTLD_NEXT, "malloc");
  real_calloc = dlsym(RTLD_NEXT, "calloc");
  real_realloc = dlsym(RTLD_NEXT, "realloc");
  real_free = dlsym(RTLD_NEXT, "free");
  real_posix_memalign = dlsym(RTLD_NEXT, "posix_memalign");
  real_aligned_alloc = dlsym(RTLD_NEXT, "aligned_alloc");
  real_memalign = dlsym(RTLD_NEXT, "memalign");
  initializing = 0;
}

static void emit(const char *what, void *p, size_t size) {
  if (busy) return;
  busy = 1;
  pthread_mutex_lock(&lock);
  if (fd < 0) {
    const char *base = getenv("BIGALLOC_LOG");
    char path[512];
    snprintf(path, sizeof path, "%s.%d", base ? base : "/tmp/bigalloc", (int)getpid());
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
  }
  if (fd >= 0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    char name[17] = {0};
    prctl(PR_GET_NAME, name, 0, 0, 0);
    char line[256];
    int n = snprintf(line, sizeof line, "%s %ld.%06ld pid=%d tid=%d thread=%s size=%zu ptr=%p\n", what, (long)ts.tv_sec,
                     ts.tv_nsec / 1000, (int)getpid(), (int)gettid(), name, size, p);
    write(fd, line, n);
    if (what[0] == 'A') {
      void *frames[24];
      int k = backtrace(frames, 24);
      backtrace_symbols_fd(frames, k, fd);
    }
    write(fd, "\n", 1);
  }
  pthread_mutex_unlock(&lock);
  busy = 0;
}

static void track(void *p, size_t size) {
  if (!p || size < BIG) return;
  pthread_mutex_lock(&lock);
  for (int i = 0; i < SLOTS; i++)
    if (!live[i]) { live[i] = p; break; }
  pthread_mutex_unlock(&lock);
  emit("ALLOC", p, size);
}

static void untrack(void *p) {
  if (!p) return;
  int hit = 0;
  pthread_mutex_lock(&lock);
  for (int i = 0; i < SLOTS; i++)
    if (live[i] == p) { live[i] = 0; hit = 1; break; }
  pthread_mutex_unlock(&lock);
  if (hit) emit("FREE", p, 0);
}

void *malloc(size_t n) { init(); void *p = real_malloc(n); track(p, n); return p; }
void *calloc(size_t a, size_t b) {
  if (!real_calloc) init();
  if (!real_calloc) {  // during dlsym
    size_t n = (a * b + 15) & ~(size_t)15;
    if (bootUsed + n > sizeof boot) return 0;
    void *p = boot + bootUsed; bootUsed += n; memset(p, 0, n); return p;
  }
  void *p = real_calloc(a, b); track(p, a * b); return p;
}
void *realloc(void *o, size_t n) { init(); untrack(o); void *p = real_realloc(o, n); track(p, n); return p; }
void free(void *p) {
  if ((char *)p >= boot && (char *)p < boot + sizeof boot) return;
  init(); untrack(p); real_free(p);
}
int posix_memalign(void **r, size_t a, size_t n) { init(); int e = real_posix_memalign(r, a, n); if (!e) track(*r, n); return e; }
void *aligned_alloc(size_t a, size_t n) { init(); void *p = real_aligned_alloc(a, n); track(p, n); return p; }
void *memalign(size_t a, size_t n) { init(); void *p = real_memalign(a, n); track(p, n); return p; }

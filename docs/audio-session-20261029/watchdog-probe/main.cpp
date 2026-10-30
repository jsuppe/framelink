#include "deadline.h"
#include <cstdio>
int main() {
    SampleDeadline deadline;
    std::fprintf(stderr, "Injected blocked media operation; waiting beyond deadline\n");
    std::fflush(stderr);
    g_usleep(30 * G_TIME_SPAN_SECOND);
    std::fprintf(stderr, "ERROR: watchdog failed to terminate blocked operation\n");
    return 0;
}

#define FL_EXPORTS
#include "framelink.h"
#include "framelink_trace.hpp"
FL_API flResult flTraceFlush(void) { return fltrace::flush() ? FL_OK : FL_INTERNAL; }

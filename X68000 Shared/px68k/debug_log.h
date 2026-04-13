#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#ifdef DEBUG_OUTPUT
  #define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
  #define DEBUG_PRINTF(...) ((void)0)
#endif

#ifdef TRACE_OUTPUT
  #define TRACE_PRINTF(...) printf(__VA_ARGS__)
#else
  #define TRACE_PRINTF(...) ((void)0)
#endif

#ifdef ERROR_OUTPUT
  #define ERROR_PRINTF(...) fprintf(stderr, __VA_ARGS__)
#else
  #define ERROR_PRINTF(...) ((void)0)
#endif

#endif
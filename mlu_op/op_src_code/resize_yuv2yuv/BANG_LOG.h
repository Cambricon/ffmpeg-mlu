#ifdef __DEBUG
#define PRINTF_VECTOR(statement, format, arr, len) \
  do {                                             \
    __bang_printf(statement);                      \
    __bang_printf("\n");                           \
    for (int idx = 0; idx < len; ++idx) {          \
      __bang_printf(format, *(arr _ idx));         \
    }                                              \
    __bang_printf("\n\n");                         \
  } while (0)                 

#define PRINTF_SCALE(format, ...) __bang_printf(format, ##__VA_ARGS__)
#else
#define PRINTF_VECTOR(format, ...)
#define PRINTF_SCALAR(format, ...)
#endif


/* Minimal host shim so migcom builds on Linux; sizes match 32-bit ARM Darwin. */
#ifndef MIGSHIM_BOOLEAN_H
#define MIGSHIM_BOOLEAN_H
typedef int boolean_t;
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#endif

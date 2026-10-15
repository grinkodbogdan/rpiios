#ifndef MIGSHIM_STD_TYPES_H
#define MIGSHIM_STD_TYPES_H
#include <stdint.h>
#include <mach/boolean.h>
#include <mach/kern_return.h>
typedef uint32_t natural_t;
typedef int32_t integer_t;
typedef uint32_t vm_offset_t, vm_size_t, vm_address_t;
typedef uint64_t mach_vm_address_t;
typedef natural_t mach_port_t;
typedef natural_t mach_port_seqno_t;
#define MACH_PORT_NULL 0
#endif

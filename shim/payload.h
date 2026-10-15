/* Payload appended to the shim at +64 KiB by tools/pack-kernel.py. */
#ifndef RPIIOS_PAYLOAD_H
#define RPIIOS_PAYLOAD_H
#include <stdint.h>

#define PAYLOAD_MAGIC0       0x33495052u   /* "RPI3" */
#define PAYLOAD_MAGIC1       0x31554e58u   /* "XNU1" */
#define PAYLOAD_VERSION      1u
#define PAYLOAD_MAX_SEGMENTS 16u

struct rpi3_payload_segment {
    uint32_t vmaddr, vmsize, fileoff, filesize;   /* fileoff: within the Mach-O */
};

struct rpi3_payload {
    uint32_t magic[2];
    uint32_t version;
    uint32_t total_bytes;          /* header + kernel + device tree */
    uint32_t entry_va;
    uint32_t kernel_offset, kernel_bytes;      /* from the payload start */
    uint32_t dt_offset, dt_bytes, dt_handoff_offset;
    uint32_t segment_count;
    struct rpi3_payload_segment segments[PAYLOAD_MAX_SEGMENTS];
    char cmdline[256];             /* built-in boot-args */
};
#endif

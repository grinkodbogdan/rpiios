#ifndef MIGSHIM_NDR_H
#define MIGSHIM_NDR_H
typedef struct { unsigned char mig_vers, if_vers, reserved1, mig_encoding,
                 int_rep, char_rep, float_rep, reserved2; } NDR_record_t;
#endif

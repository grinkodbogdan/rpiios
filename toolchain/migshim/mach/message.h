#ifndef MIGSHIM_MESSAGE_H
#define MIGSHIM_MESSAGE_H
/* Values copied from xnu osfmk/mach/message.h (xnu-2423 era). */
#include <mach/std_types.h>
#include <mach/ndr.h>
typedef natural_t mach_msg_bits_t, mach_msg_size_t, mach_msg_type_name_t,
                  mach_msg_type_number_t, mach_msg_timeout_t, mach_msg_option_t,
                  mach_msg_copy_options_t, mach_msg_descriptor_type_t,
                  mach_msg_trailer_type_t, mach_msg_trailer_size_t;
typedef integer_t mach_msg_id_t;
typedef kern_return_t mach_msg_return_t;
typedef natural_t mach_msg_type_t;
typedef struct { mach_msg_bits_t msgh_bits; mach_msg_size_t msgh_size;
  mach_port_t msgh_remote_port, msgh_local_port; mach_msg_size_t msgh_reserved;
  mach_msg_id_t msgh_id; } mach_msg_header_t;
typedef struct { mach_msg_size_t msgh_descriptor_count; } mach_msg_body_t;
typedef struct { natural_t pad1; mach_msg_size_t pad2; unsigned int pad3:24, type:8; } mach_msg_type_descriptor_t;
typedef struct { mach_port_t name; mach_msg_size_t pad1; unsigned int pad2:16, disposition:8, type:8; } mach_msg_port_descriptor_t;
typedef struct { uint32_t address; mach_msg_size_t size; unsigned int deallocate:8, copy:8, pad1:8, type:8; } mach_msg_ool_descriptor_t;
typedef struct { uint32_t address; mach_msg_size_t count; unsigned int deallocate:8, copy:8, disposition:8, type:8; } mach_msg_ool_ports_descriptor_t;
typedef union { mach_msg_port_descriptor_t port; mach_msg_ool_descriptor_t out_of_line;
  mach_msg_ool_ports_descriptor_t ool_ports; mach_msg_type_descriptor_t type; } mach_msg_descriptor_t;
typedef struct { unsigned int val[2]; } security_token_t;
typedef struct { unsigned int val[8]; } audit_token_t;
typedef struct { mach_msg_trailer_type_t msgh_trailer_type; mach_msg_trailer_size_t msgh_trailer_size;
  mach_port_seqno_t msgh_seqno; security_token_t msgh_sender; audit_token_t msgh_audit;
  uint64_t msgh_context; int msgh_ad; mach_port_t msgh_labels; } mach_msg_max_trailer_t;

#define MACH_MSGH_BITS_COMPLEX		0x80000000U
#define MACH_MSGH_BITS_CIRCULAR		0x40000000
#define MACH_MSGH_BITS(remote, local)	((remote) | ((local) << 8))
#define MACH_MSG_TYPE_MOVE_RECEIVE	16
#define MACH_MSG_TYPE_MOVE_SEND		17
#define MACH_MSG_TYPE_MOVE_SEND_ONCE	18
#define MACH_MSG_TYPE_COPY_SEND		19
#define MACH_MSG_TYPE_MAKE_SEND		20
#define MACH_MSG_TYPE_MAKE_SEND_ONCE	21
#define MACH_MSG_TYPE_COPY_RECEIVE	22
#define MACH_MSG_TYPE_PORT_NAME		15
#define MACH_MSG_TYPE_PORT_RECEIVE	MACH_MSG_TYPE_MOVE_RECEIVE
#define MACH_MSG_TYPE_PORT_SEND		MACH_MSG_TYPE_MOVE_SEND
#define MACH_MSG_TYPE_PORT_SEND_ONCE	MACH_MSG_TYPE_MOVE_SEND_ONCE
#define MACH_MSG_TYPE_POLYMORPHIC	((mach_msg_type_name_t) -1)
#define MACH_MSG_TYPE_PORT_ANY(x) \
	(((x) >= MACH_MSG_TYPE_MOVE_RECEIVE) && ((x) <= MACH_MSG_TYPE_MAKE_SEND_ONCE))
#define MACH_MSG_TYPE_PORT_ANY_SEND(x) \
	(((x) >= MACH_MSG_TYPE_MOVE_SEND) && ((x) <= MACH_MSG_TYPE_MAKE_SEND_ONCE))
#define MACH_MSG_TYPE_PORT_ANY_RIGHT(x) \
	(((x) >= MACH_MSG_TYPE_MOVE_RECEIVE) && ((x) <= MACH_MSG_TYPE_MOVE_SEND_ONCE))
#define MACH_MSG_PHYSICAL_COPY		0
#define MACH_MSG_VIRTUAL_COPY		1
#define MACH_MSG_ALLOCATE		2
#define MACH_MSG_OVERWRITE		3
#define MACH_MSG_PORT_DESCRIPTOR	0
#define MACH_MSG_OOL_DESCRIPTOR		1
#define MACH_MSG_OOL_PORTS_DESCRIPTOR	2
#define MACH_MSG_OOL_VOLATILE_DESCRIPTOR 3
#define MACH_MSG_OPTION_NONE		0x00000000
#define MACH_SEND_MSG			0x00000001
#define MACH_RCV_MSG			0x00000002
#define MACH_SEND_TIMEOUT		0x00000010
#define MACH_RCV_TIMEOUT		0x00000100
#define MACH_RCV_OVERWRITE		0x00001000
#define MACH_MSG_TIMEOUT_NONE		((mach_msg_timeout_t) 0)
#define MACH_MSG_TRAILER_FORMAT_0	0
#define MACH_RCV_TRAILER_TYPE(x)	(((x) & 0xf) << 28)
#define MACH_RCV_TRAILER_ELEMENTS(x)	(((x) & 0xf) << 24)
#define MACH_RCV_TRAILER_SENDER		2
#define MACH_RCV_TRAILER_AUDIT		3
#define MACH_RCV_TRAILER_CTX		4
#define MACH_MSG_SUCCESS		0x00000000
#define MACH_SEND_INVALID_DATA		0x10000002
#define MACH_SEND_INVALID_DEST		0x10000003
#define MACH_SEND_TIMED_OUT		0x10000004
#define MACH_SEND_INVALID_HEADER	0x10000010
#define MACH_RCV_TIMED_OUT		0x10004003
#endif

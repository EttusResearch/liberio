#ifndef USRP_STUFF_H
#define USRP_STUFF_H

#include <stdint.h>
#include <sys/ioctl.h>
#include <stdlib.h>

#define __u32 uint32_t
#define __s32 int32_t
#define __u8 uint8_t

struct usrp_timecode {
	__u32	type;
	__u32	flags;
	__u8	frames;
	__u8	seconds;
	__u8	minutes;
	__u8	hours;
	__u8	userbits[4];
};


enum usrp_buf_type {
	USRP_BUF_TYPE_INPUT          = 1,
	USRP_BUF_TYPE_OUTPUT         = 2,
	USRP_BUF_TYPE_VIDEO_OVERLAY        = 3,
	USRP_BUF_TYPE_VBI_CAPTURE          = 4,
	USRP_BUF_TYPE_VBI_OUTPUT           = 5,
	USRP_BUF_TYPE_SLICED_VBI_CAPTURE   = 6,
	USRP_BUF_TYPE_SLICED_VBI_OUTPUT    = 7,
	USRP_BUF_TYPE_VIDEO_OUTPUT_OVERLAY = 8,
	USRP_BUF_TYPE_VIDEO_CAPTURE_MPLANE = 9,
	USRP_BUF_TYPE_VIDEO_OUTPUT_MPLANE  = 10,
	USRP_BUF_TYPE_SDR_CAPTURE          = 11,
	USRP_BUF_TYPE_SDR_OUTPUT           = 12,
	/* Deprecated, do not use */
	USRP_BUF_TYPE_PRIVATE              = 0x80,
};

struct usrp_requestbuffers {
	__u32			count;
	__u32			type;		/* enum usrp_buf_type */
	__u32			memory;		/* enum usrp_memory */
	__u32			reserved[2];
};

struct usrp_plane {
	__u32			bytesused;
	__u32			length;
	union {
		__u32		mem_offset;
		unsigned long	userptr;
		__s32		fd;
	} m;
	__u32			data_offset;
	__u32			reserved[11];
};

struct usrp_buffer {
	__u32			index;
	__u32			type;
	__u32			bytesused;
	__u32			flags;
	__u32			field;
	struct timeval		timestamp;
	struct usrp_timecode	timecode;
	__u32			sequence;

	/* memory location */
	__u32			memory;
	union {
		__u32           offset;
		unsigned long   userptr;
		struct usrp_plane *planes;
		__s32		fd;
	} m;
	__u32			length;
	__u32			reserved2;
	__u32			reserved;
};

struct usrp_exportbuffer {
	__u32	type; /* enum usrp_buf_type */
	__u32	index;
	__u32	plane;
	__u32	flags;
	__s32	fd;
	__u32	reserved[11];
};

#define USRPIOC_REQBUFS		_IOWR('V',  8, struct usrp_requestbuffers)
#define USRPIOC_QUERYBUF	_IOWR('V',  9, struct usrp_buffer)
#define USRPIOC_QBUF		_IOWR('V', 15, struct usrp_buffer)
#define USRPIOC_EXPBUF		_IOWR('V', 16, struct usrp_exportbuffer)
#define USRPIOC_DQBUF		_IOWR('V', 17, struct usrp_buffer)
#define USRPIOC_STREAMON	_IOW('V', 18, int)
#define USRPIOC_STREAMOFF	_IOW('V', 19, int)

#endif /* USRP_STUFF_H */

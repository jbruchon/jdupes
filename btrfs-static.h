/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#ifndef JDUPES_BTRFS_H
#define JDUPES_BTRFS_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define BTRFS_IOCTL_MAGIC 0x94

#define BTRFS_DEVICE_PATH_NAME_MAX 1024

#define FILE_DEDUPE_RANGE_DIFFERS	1
/* For extent-same ioctl */
struct file_dedupe_range_info {
	__s64 fd;		/* in - destination file */
	__u64 logical_offset;	/* in - start of extent in destination */
	__u64 bytes_deduped;	/* out - total # of bytes we were able
				 * to dedupe from this file */
	/* status of this dedupe operation:
	 * 0 if dedup succeeds
	 * < 0 for error
	 * == FILE_DEDUPE_RANGE_DIFFERS if data differs
	 */
	__s32 status;		/* out - see above description */
	__u32 reserved;
};

struct file_dedupe_range {
	__u64 logical_offset;	/* in - start of extent in source */
	__u64 length;		/* in - length of extent */
	__u16 dest_count;	/* in - total elements in info array */
	__u16 reserved1;
	__u32 reserved2;
	struct file_dedupe_range_info info[0];
};

#define FIDEDUPERANGE _IOWR(BTRFS_IOCTL_MAGIC, 54, \
					 struct file_dedupe_range)

#endif /* JDUPES_BTRFS_H */

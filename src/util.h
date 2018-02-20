/*
 * Copyright (c) 2017, National Instruments Corp.
 *
 * "That dude over there just punched Merica into that guy,
 *  he must be a Liberio!"
 * 	- Urban Dictionary
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 */

#ifndef LIBERIO_UTIL_H
#define LIBERIO_UTIL_H

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

struct udev_device;
struct udev;

struct udev_device *liberio_udev_device_from_fd(struct udev *udev,
						int fd);

int liberio_ioctl(int fd, unsigned long req, void *arg);

const char *liberio_chan_get_sysattr(struct liberio_chan *chan,
				     const char *sysattr);

int liberio_chan_set_sysattr(struct liberio_chan *chan, const char *sysattr,
			     char *value);

#endif /* LIBERIO_UTIL_H */

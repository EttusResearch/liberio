/*
 * Copyright (c) 2017, National Instruments Corp.
 *
 * "That dude over there just punched Merica into that guy,
 *  he must be a Liberio!"
 * 	- Urban Dictionary
 *
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <libudev.h>

#include "priv.h"

struct udev_device *liberio_udev_device_from_fd(struct udev *udev,
						int fd)
{
	struct udev_device *dev;
	struct stat statbuf;
	int err;

	err = fstat(fd, &statbuf);
	if (err < 0)
		return NULL;

	if (!S_ISCHR(statbuf.st_mode))
		return NULL;

	return udev_device_new_from_devnum(udev, 'c', statbuf.st_rdev);
}

const char *liberio_chan_get_sysattr(struct liberio_chan *chan,
				     const char *sysattr)
{
	if (!chan || !sysattr)
		return NULL;

	return udev_device_get_sysattr_value(chan->dev, sysattr);
}

int liberio_chan_set_sysattr(struct liberio_chan *chan, const char *sysattr,
			     char *value)
{
	if (!chan || !sysattr || !value)
		return -EINVAL;

	return udev_device_set_sysattr_value(chan->dev, sysattr, value);
}

int liberio_ioctl(int fd, unsigned long req, void *arg)
{
	int r;

	do {
		r = ioctl(fd, req, arg);
	} while (-1 == r && EINTR == errno);

	return r;
}


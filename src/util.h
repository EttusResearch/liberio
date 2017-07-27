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

#ifndef LIBERIO_UTIL_H
#define LIBERIO_UTIL_H

#define likely(x)       __builtin_expect((x),1)
#define unlikely(x)     __builtin_expect((x),0)

struct udev_device;
struct udev;

struct udev_device *liberio_udev_device_from_fd(struct udev *udev,
						int fd);

#endif /* LIBERIO_UTIL_H */

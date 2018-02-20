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

#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <sys/socket.h>
#include <string.h>
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

int liberio_send_fd(int sockfd, int fd)
{
	struct msghdr message;
	struct iovec iov[1];
	struct cmsghdr *control_message = NULL;
	char ctrl_buf[CMSG_SPACE(sizeof(int))];
	char data[1];

	memset(&message, 0, sizeof(struct msghdr));
	memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

	data[0] = '!';
	iov[0].iov_base = data;
	iov[0].iov_len = sizeof(data);

	message.msg_name = NULL;
	message.msg_namelen = 0;
	message.msg_iov = iov;
	message.msg_iovlen = 1;
	message.msg_controllen =  CMSG_SPACE(sizeof(int));
	message.msg_control = ctrl_buf;

	control_message = CMSG_FIRSTHDR(&message);
	control_message->cmsg_level = SOL_SOCKET;
	control_message->cmsg_type = SCM_RIGHTS;
	control_message->cmsg_len = CMSG_LEN(sizeof(int));

	*((int *) CMSG_DATA(control_message)) = fd;

	return sendmsg(sockfd, &message, 0);
}

int liberio_recv_fd(int sockfd)
{
	int sent_fd;
	struct msghdr message;
	struct iovec iov[1];
	struct cmsghdr *cmsg = NULL;
	char ctrl_buf[CMSG_SPACE(sizeof(int))];
	char data[1];
	int res;

	memset(&message, 0, sizeof(struct msghdr));
	memset(ctrl_buf, 0, CMSG_SPACE(sizeof(int)));

	/* For the dummy data */
	iov[0].iov_base = data;
	iov[0].iov_len = sizeof(data);

	message.msg_name = NULL;
	message.msg_namelen = 0;
	message.msg_control = ctrl_buf;
	message.msg_controllen = CMSG_SPACE(sizeof(int));
	message.msg_iov = iov;
	message.msg_iovlen = 1;

	res = recvmsg(sockfd, &message, 0);
	if (res <= 0)
		return res;

	/* Iterate through header to find if there is a file
	 * descriptor
	 */
	for (cmsg = CMSG_FIRSTHDR(&message); cmsg != NULL;
	     cmsg = CMSG_NXTHDR(&message, cmsg))
		if ((cmsg->cmsg_level == SOL_SOCKET) &&
		    (cmsg->cmsg_type == SCM_RIGHTS))
			return *((int *)CMSG_DATA(cmsg));

	return -EINVAL;
}


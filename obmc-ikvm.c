/*
 * OpenBMC IKVM daemon
 *
 * Copyright (C) 2018 IBM
 *
 * Eddie James <eajames@us.ibm.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/videodev2.h>
#include <rfb/rfb.h>
#include <rfh/rfbproto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BITS_PER_SAMPLE		8
#define BYTES_PER_PIXEL		4
#define SAMPLES_PER_PIXEL	3

#define PROCESS_EVENTS_TIME_US	100000

#define FRAME_SIZE_BYTE_LIMIT	127
#define FRAME_SIZE_WORD_LIMIT	16383

struct resolution {
	size_t height;
	size_t width;
	size_t size;
};

struct obmc_ikvm {
	int videodev_fd;
	int frame_size;
	struct resolution resolution;
	char *frame;
	rfbScreenInfoPtr server;
};

int init_videodev(struct obmc_ikvm *ikvm, const char *videodev_name)
{
	int rc;
	size_t buf_size;
	struct v4l2_capability cap;
        struct v4l2_format fmt;

	ikvm->videodev_fd = open(videodev_name, O_RDWR | O_NONBLOCK);
	if (ikvm_videodev_fd < 0)
		return -errno;

	rc = ioctl(ikvm->videodev_fd, VIDIOC_QUERYCAP, &cap);
	if (rc < 0)
		return -errno;

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
	    !(cap.capabilities & V4L2_CAP_READWRITE))
		return -EOPNOTSUPP;

	rc = ioctl(ikvm->videodev_fd, VIDIOC_G_FMT, &fmt);
	if (rc < 0)
		return -errno;

	ikvm->resolution.height = fmt.fmt.pix.height;
	ikvm->resolution.width = fmt.fmt.pix.width;
	ikvm->resolution.size = fmt.fmt.pix.sizeimage;

	buf_size = max(ikvm->resolution.size, ikvm->resolution.height *
		ikvm->resolution.width * BYTES_PER_PIXEL);

	ikvm->frame = (char *)malloc(buf_size);
	if (!ikvm->frame)
		return -ENOMEM;

//	ikvm->frame[0] = rfbTightJpeg;

	return 0;
}

int init_server(struct obmc_ikvm *ikvm, int argc, char **argv)
{
	ikvm->server = rfbGetScreen(argc, argv, ikvm->resolution.width,
				    ikvm->resolution.height, BITS_PER_SAMPLE,
				    SAMPLES_PER_PIXEL, BYTES_PER_PIXEL);
	if (!ikvm->server)
		return -ENODEV;

	ikvm->server->desktopName = "AST2XXX Video Engine";
	ikvm->server->framebuffer = ikvm->frame;
	ikvm->server->alwaysShared = true;

	rfbInitServer(ikvm->server);

	return 0;
}

void send_frame_to_clients(struct obmc_ikvm *ikvm)
{
	rfbClientIteratorPtr iterator = rfbGetClientIterator(ikvm->server);
	rfbClientPtr cl;

	while (cl = rfbClientIteratorNext(iterator))
		rfbSendCompressedDataTight(cl, ikvm->frame, ikvm->frame_size);

	rfbReleaseClientIterator(iterator);
}

int get_frame(struct obmc_ikvm *ikvm)
{
	int rc;
/*
	int offs = 2;

	if (ikvm->frame_size > FRAME_SIZE_BYTE_LIMIT) {
		if (ikvm->frame_size > FRAME_SIZE_WORD_LIMIT)
			offs = 4;
		else
			offs = 3;
	}
*/
	rc = read(ikvm->videodev_fd, ikvm->frame, ikvm->resolution.size);
	if (rc < 0)
		return -errno;

	ikvm->frame_size = rc;
/*
	ikvm->frame[1] = rc & 0x7F;

	if (rc > FRAME_SIZE_BYTE_LIMIT) {
		if (offs == 2) {
			if (rc > FRAME_SIZE_WORD_LIMIT)
				memmove(&ikvm->frame[4], &ikvm->frame[2], rc);
			else
				memmove(&ikvm->frame[3], &ikvm->frame[2], rc);
		}
		ikvm->frame[1] |= 0x80;
		ikvm->frame[2] = (rc & 0x3F80) >> 7;
	}

	if (rc > FRAME_SIZE_WORD_LIMIT) {
		if (offs == 3)
			memmove(&ikvm->frame[4], &ikvm->frame[3], rc);

		ikvm->frame[2] |= 0x80;
		ikvm->frame[3] = (rc & 0x3FC000) >> 14;
	}
*/
	send_frame_to_clients(ikvm);
}

int main(int argc, char **argv)
{
	bool ok = true;
	int len;
	int option;
	int rc;
	char *videodev_name = NULL;
	const char *opts = "v:h";
	struct option lopts[] = {
		{ "help", 0, 0, 'h' },
		{ "videodev", 1, 0, 'v' },
		{ 0, 0, 0, 0 }
	};
	struct obmc_ikvm ikvm;

	ikvm.videodev_fd = -1;
	ikvm.frame_size = 15104;

	while ((option = getopt_long(argc, argv, opts, lopts, NULL)) != -1) {
		switch (option) {
		case 'h':

			break;
		case 'v':
			videodev_name = malloc(strlen(optarg) + 1);
			if (!videodev_name)
				return -ENOMEM;

			strcpy(videodev_name, optarg);
			break;
		}
	}

	rc = init_videodev(&ikvm, videodev_name);
	if (rc)
		goto done;

	rc = init_server(&ikvm, argc, argv);
	if (rc)
		goto done;

	while (ok) {
		while (ikvm.server->clientHead == NULL)
			rfbProcessEvents(ikvm.server, PROCESS_EVENTS_TIME_US);

		rfbProcessEvents(ikvm.server, PROCESS_EVENTS_TIME_US);
		rc = get_frame(ikvm);
		if (rc)
			break;
	}

done:
	if (ikvm.server)
		rfbScreenCleanup(ikvm->server);

	if (ikvm.videodev_fd >= 0)
		close(ikvm.videodev_fd);

	if (videodev_name)
		free(videodev_name);

	return rc;
}

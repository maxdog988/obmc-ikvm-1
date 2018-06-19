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

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <linux/videodev2.h>
#include <rfb/rfb.h>
#include <rfb/rfbproto.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BITS_PER_SAMPLE		8
#define BYTES_PER_PIXEL		4
#define SAMPLES_PER_PIXEL	3

#define PROCESS_EVENTS_TIME_US	10000

#define FRAME_SIZE_BYTE_LIMIT	127
#define FRAME_SIZE_WORD_LIMIT	16383

static volatile bool ok = true;

struct resolution {
	size_t height;
	size_t width;
	size_t size;
};

struct obmc_ikvm {
	bool do_delay;
	int num_clients;
	int videodev_fd;
	int frame_size;
	int frame_buf_size;
	struct resolution resolution;
	char *frame;
	char *videodev_name;
	rfbScreenInfoPtr server;
};

void int_handler(int sig)
{
	ok = false;
}

int init_videodev(struct obmc_ikvm *ikvm)
{
	int rc;
	struct v4l2_capability cap;
        struct v4l2_format fmt;

	ikvm->videodev_fd = open(ikvm->videodev_name, O_RDWR);
	if (ikvm->videodev_fd < 0) {
		printf("failed to open %s: %d %s\n", ikvm->videodev_name,
		       errno, strerror(errno));
		return -ENODEV;
	}

	rc = ioctl(ikvm->videodev_fd, VIDIOC_QUERYCAP, &cap);
	if (rc < 0) {
		printf("failed to query capabilities: %d %s\n", errno,
		       strerror(errno));
		return -EINVAL;
	}

	if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
	    !(cap.capabilities & V4L2_CAP_READWRITE)) {
		printf("device doesn't support this application\n");
		return -EOPNOTSUPP;
	}

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rc = ioctl(ikvm->videodev_fd, VIDIOC_G_FMT, &fmt);
	if (rc < 0) {
		printf("failed to query format: %d %s\n", errno,
		       strerror(errno));
		return -EINVAL;
	}

	ikvm->resolution.height = fmt.fmt.pix.height;
	ikvm->resolution.width = fmt.fmt.pix.width;
	ikvm->resolution.size = fmt.fmt.pix.sizeimage;

	ikvm->frame_buf_size = ikvm->resolution.height *
		ikvm->resolution.width * BYTES_PER_PIXEL;
	if (ikvm->resolution.size > ikvm->frame_buf_size)
		ikvm->frame_buf_size = ikvm->resolution.size;

	ikvm->frame = (char *)malloc(ikvm->frame_buf_size);
	if (!ikvm->frame) {
		printf("failed to allocate buffer\n");
		return -ENOMEM;
	}

	memset(ikvm->frame, 0, ikvm->frame_buf_size);

	return 0;
}


static void client_gone(rfbClientPtr cl)
{
	struct obmc_ikvm *ikvm = cl->clientData;

	if (ikvm->num_clients-- > 1)
		return;

	if (ikvm->videodev_fd >= 0) {
		close(ikvm->videodev_fd);
		ikvm->videodev_fd = open(ikvm->videodev_name, O_RDWR);
		if (ikvm->videodev_fd < 0) {
			printf("failed to re-open %s: %d %s\n",
			       ikvm->videodev_name, errno, strerror(errno));
			ok = false;
		} else {
			memset(ikvm->frame, 0, ikvm->frame_buf_size);
			rfbMarkRectAsModified(ikvm->server, 0, 0,
					      ikvm->resolution.width,
					      ikvm->resolution.height);
		}
	}
}

static enum rfbNewClientAction new_client(rfbClientPtr cl)
{
	struct obmc_ikvm *ikvm = cl->screen->screenData;

	cl->clientData = ikvm;
	cl->clientGoneHook = client_gone;

	ikvm->num_clients++;
	ikvm->do_delay = true;

	return RFB_CLIENT_ACCEPT;
}

int init_server(struct obmc_ikvm *ikvm, int *argc, char **argv)
{
	ikvm->server = rfbGetScreen(argc, argv, ikvm->resolution.width,
				    ikvm->resolution.height, BITS_PER_SAMPLE,
				    SAMPLES_PER_PIXEL, BYTES_PER_PIXEL);
	if (!ikvm->server) {
		printf("failed to get vnc screen\n");
		return -ENODEV;
	}

	ikvm->server->screenData = ikvm;
	ikvm->server->desktopName = "AST2XXX Video Engine";
	ikvm->server->frameBuffer = ikvm->frame;
	ikvm->server->alwaysShared = true;
	ikvm->server->newClientHook = new_client;

	rfbInitServer(ikvm->server);

	rfbMarkRectAsModified(ikvm->server, 0, 0, ikvm->resolution.width,
			      ikvm->resolution.height);

	return 0;
}

void send_frame_to_clients(struct obmc_ikvm *ikvm)
{
	rfbClientIteratorPtr iterator = rfbGetClientIterator(ikvm->server);
	rfbClientPtr cl;

	while (cl = rfbClientIteratorNext(iterator)) {
		rfbFramebufferUpdateMsg *fu =
			(rfbFramebufferUpdateMsg *)cl->updateBuf;

		fu->type = rfbFramebufferUpdate;

		if (cl->enableLastRectEncoding)
			fu->nRects = 0xFFFF;
		else
			fu->nRects = Swap16IfLE(1);

		cl->ublen = sz_rfbFramebufferUpdateMsg;

		rfbSendUpdateBuf(cl);

		cl->tightEncoding = rfbEncodingTight;

		rfbSendTightHeader(cl, 0, 0, ikvm->resolution.width,
				   ikvm->resolution.height);

		cl->updateBuf[cl->ublen++] = (char)(rfbTightJpeg << 4);

		rfbSendCompressedDataTight(cl, ikvm->frame, ikvm->frame_size);

		if (cl->enableLastRectEncoding)
			rfbSendLastRectMarker(cl);

		rfbSendUpdateBuf(cl);
	}

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
	if (rc < 0) {
		printf("failed to read frame: %d %s\n", errno,
		       strerror(errno));
		return -EFAULT;
	}

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

	return 0;
}

int timespec_subtract(struct timespec *result, struct timespec *x,
		      struct timespec *y)
{
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_nsec < y->tv_nsec) {
		long long int nsec =
			((y->tv_nsec - x->tv_nsec) / 1000000000) + 1;

		y->tv_nsec -= 1000000000 * nsec;
		y->tv_sec += nsec;
	}

	if (x->tv_nsec - y->tv_nsec > 1000000000) {
		int nsec = (x->tv_nsec - y->tv_nsec) / 1000000000;

		y->tv_nsec += 1000000000 * nsec;
		y->tv_sec -= nsec;
	}

	/*
	 * Compute the time remaining to wait.
	 * tv_nsec is certainly positive.
	 */
	result->tv_sec = x->tv_sec - y->tv_sec;
	result->tv_nsec = x->tv_nsec - y->tv_nsec;

	/* Return 1 if result is negative. */
	return x->tv_sec < y->tv_sec;
}

int main(int argc, char **argv)
{
	int len;
	int option;
	int rc;
	const char *opts = "v:h";
	struct option lopts[] = {
		{ "help", 0, 0, 'h' },
		{ "videodev", 1, 0, 'v' },
		{ 0, 0, 0, 0 }
	};
	struct obmc_ikvm ikvm;

	memset(&ikvm, 0, sizeof(struct obmc_ikvm));
	ikvm.videodev_fd = -1;

	while ((option = getopt_long(argc, argv, opts, lopts, NULL)) != -1) {
		switch (option) {
		case 'h':

			break;
		case 'v':
			ikvm.videodev_name = malloc(strlen(optarg) + 1);
			if (!ikvm.videodev_name)
				return -ENOMEM;

			strcpy(ikvm.videodev_name, optarg);
			break;
		}
	}

	rc = init_videodev(&ikvm);
	if (rc)
		goto done;

	rc = init_server(&ikvm, &argc, argv);
	if (rc)
		goto done;

	signal(SIGINT, int_handler);

	while (ok) {
		while (ikvm.server->clientHead == NULL && ok)
			rfbProcessEvents(ikvm.server, PROCESS_EVENTS_TIME_US);

		if (ikvm.do_delay) {
			struct timespec diff;
			struct timespec end;
			struct timespec now;

			ikvm.do_delay = false;

			clock_gettime(CLOCK_MONOTONIC, &end);
			now = end;
			end.tv_sec++;
			end.tv_nsec += 500000000;

			while (!timespec_subtract(&diff, &end, &now)) {
				rfbProcessEvents(ikvm.server,
						 PROCESS_EVENTS_TIME_US);
				clock_gettime(CLOCK_MONOTONIC, &now);
			}
		}

		rfbProcessEvents(ikvm.server, PROCESS_EVENTS_TIME_US);

		if (!ok)
			break;

		rc = get_frame(&ikvm);
		if (rc)
			break;
	}

done:
	if (ikvm.server)
		rfbScreenCleanup(ikvm.server);

	if (ikvm.frame)
		free(ikvm.frame);

	if (ikvm.videodev_fd >= 0)
		close(ikvm.videodev_fd);

	if (ikvm.videodev_name)
		free(ikvm.videodev_name);

	return rc;
}

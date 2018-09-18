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
#include <pthread.h>
#include <rfb/keysym.h>
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

//#define _DEBUG_
//#define _PROFILE_

#ifdef _DEBUG_
#define DBG(args...)	printf(args)
#else
#define DBG(args...)
#endif /* _DEBUG_ */

#ifdef _PROFILE_
#define PROFILE_SAMPLES		512ULL

struct profile {
	bool rolled_over;
	unsigned int idx;
	unsigned long long times[PROFILE_SAMPLES];
};

unsigned long long _avg(struct profile *p)
{
	unsigned int i;
	unsigned int limit = p->rolled_over ? PROFILE_SAMPLES : p->idx + 1;
	unsigned long long rc = 0;

	for (i = 0; i < limit; ++i)
		rc += (p->times[i] / limit);

	return rc;
}

void _prof(struct timespec *diff, struct profile *p)
{
	unsigned long long usec = diff->tv_nsec / 1000ULL;

	if (diff->tv_sec)
		usec += 1000000ULL;

	p->times[p->idx++] = usec;
	if (p->idx >= PROFILE_SAMPLES) {
		p->rolled_over = true;
		p->idx = 0;
	}
}

struct profile _frame;
struct profile _input;
struct profile _wait;
#endif /* _PROFILE_ */

#define DUMP_FRAME_DIR		"/tmp/obmc-ikvm_frames"

#define BITS_PER_SAMPLE		5
#define BYTES_PER_PIXEL		2
#define SAMPLES_PER_PIXEL	1
#define PTR_SIZE		5
#define REPORT_SIZE		8

#define PROCESS_EVENTS_DELTA	100

#define FRAME_SIZE_BYTE_LIMIT	127
#define FRAME_SIZE_WORD_LIMIT	16383

#define USBHID_KEY_A		0x04
#define USBHID_KEY_B		0x05
#define USBHID_KEY_C		0x06
#define USBHID_KEY_D		0x07
#define USBHID_KEY_E		0x08
#define USBHID_KEY_F		0x09
#define USBHID_KEY_G		0x0a
#define USBHID_KEY_H		0x0b
#define USBHID_KEY_I		0x0c
#define USBHID_KEY_J		0x0d
#define USBHID_KEY_K		0x0e
#define USBHID_KEY_L		0x0f
#define USBHID_KEY_M		0x10
#define USBHID_KEY_N		0x11
#define USBHID_KEY_O		0x12
#define USBHID_KEY_P		0x13
#define USBHID_KEY_Q		0x14
#define USBHID_KEY_R		0x15
#define USBHID_KEY_S		0x16
#define USBHID_KEY_T		0x17
#define USBHID_KEY_U		0x18
#define USBHID_KEY_V		0x19
#define USBHID_KEY_W		0x1a
#define USBHID_KEY_X		0x1b
#define USBHID_KEY_Y		0x1c
#define USBHID_KEY_Z		0x1d
#define USBHID_KEY_1		0x1e
#define USBHID_KEY_2		0x1f
#define USBHID_KEY_3		0x20
#define USBHID_KEY_4		0x21
#define USBHID_KEY_5		0x22
#define USBHID_KEY_6		0x23
#define USBHID_KEY_7		0x24
#define USBHID_KEY_8		0x25
#define USBHID_KEY_9		0x26
#define USBHID_KEY_0		0x27
#define USBHID_KEY_RETURN	0x28
#define USBHID_KEY_ESC		0x29
#define USBHID_KEY_BACKSPACE	0x2a
#define USBHID_KEY_TAB		0x2b
#define USBHID_KEY_SPACE	0x2c
#define USBHID_KEY_MINUS	0x2d
#define USBHID_KEY_EQUAL	0x2e
#define USBHID_KEY_LEFTBRACE	0x2f
#define USBHID_KEY_RIGHTBRACE	0x30
#define USBHID_KEY_BACKSLASH	0x31
#define USBHID_KEY_HASH		0x32
#define USBHID_KEY_SEMICOLON	0x33
#define USBHID_KEY_APOSTROPHE	0x34
#define USBHID_KEY_GRAVE	0x35
#define USBHID_KEY_COMMA	0x36
#define USBHID_KEY_DOT		0x37
#define USBHID_KEY_SLASH	0x38
#define USBHID_KEY_CAPSLOCK	0x39
#define USBHID_KEY_F1		0x3a
#define USBHID_KEY_F2		0x3b
#define USBHID_KEY_F3		0x3c
#define USBHID_KEY_F4		0x3d
#define USBHID_KEY_F5		0x3e
#define USBHID_KEY_F6		0x3f
#define USBHID_KEY_F7		0x40
#define USBHID_KEY_F8		0x41
#define USBHID_KEY_F9		0x42
#define USBHID_KEY_F10		0x43
#define USBHID_KEY_F11		0x44
#define USBHID_KEY_F12		0x45
#define USBHID_KEY_PRINT	0x46
#define USBHID_KEY_SCROLLLOCK	0x47
#define USBHID_KEY_PAUSE	0x48
#define USBHID_KEY_INSERT	0x49
#define USBHID_KEY_HOME		0x4a
#define USBHID_KEY_PAGEUP	0x4b
#define USBHID_KEY_DELETE	0x4c
#define USBHID_KEY_END		0x4d
#define USBHID_KEY_PAGEDOWN	0x4e
#define USBHID_KEY_RIGHT	0x4f
#define USBHID_KEY_LEFT		0x50
#define USBHID_KEY_DOWN		0x51
#define USBHID_KEY_UP		0x52
#define USBHID_KEY_NUMLOCK	0x53

static volatile bool ok = true;
pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

struct resolution {
	size_t height;
	size_t width;
};

struct obmc_ikvm {
	bool dont_wait;
	bool dump_frames;
	bool send_ptr;
	bool send_report;
	bool wait_next;
	int delay_count;
	int num_clients;
	int videodev_fd;
	int frame_size;
	int frame_buf_size;
	int input_fd;
	int keyboard_fd;
	int ptr_fd;
	int dump_frame_idx;
	int frame_rate;
	int frame_time_us;
	int process_events_time_us;
	size_t report_size;
	int nRects;
	struct resolution resolution;
	char *frame;
	char *input_name;
	char *keyboard_name;
	char *ptr_name;
	char *videodev_name;
	char ptr[PTR_SIZE];
	unsigned char report[REPORT_SIZE];
	unsigned short report_map[REPORT_SIZE - 2];
	rfbScreenInfoPtr server;
};

static void int_handler(int sig)
{
	ok = false;
}

static int alloc_frame(struct obmc_ikvm *ikvm, struct v4l2_format *fmt)
{
	ikvm->resolution.height = fmt->fmt.pix.height;
	ikvm->resolution.width = fmt->fmt.pix.width;

	ikvm->frame_buf_size = ikvm->resolution.height *
		ikvm->resolution.width * BYTES_PER_PIXEL;

	if (!ikvm->frame_buf_size) {
		printf("resolution invalid\n");
		return -ENOMEM;
	}

	ikvm->frame = (char *)malloc(ikvm->frame_buf_size);
	if (!ikvm->frame) {
		printf("failed to allocate buffer\n");
		return -ENOMEM;
	}

	DBG("frame buffer size: %d\n", ikvm->frame_buf_size);
	memset(ikvm->frame, 0, ikvm->frame_buf_size);

	return 0;
}

static void set_frame_rate(struct obmc_ikvm *ikvm)
{
	int rc;
	struct v4l2_streamparm sparm;

	sparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	sparm.parm.capture.timeperframe.numerator = 1;
	sparm.parm.capture.timeperframe.denominator = ikvm->frame_rate;

	rc = ioctl(ikvm->videodev_fd, VIDIOC_S_PARM, &sparm);
	if (rc < 0)
		printf("failed to set framerate; ignoring: %d %s\n", errno,
		       strerror(errno));
}

static int init_videodev(struct obmc_ikvm *ikvm)
{
	int rc;
	struct v4l2_capability cap;
        struct v4l2_format fmt;
	struct v4l2_streamparm sparm;

	ikvm->videodev_fd = open(ikvm->videodev_name, O_RDWR);
	if (ikvm->videodev_fd < 0) {
		/* VGA may have gone to sleep? Try and wake it up */
		if (ikvm->input_name) {
			int tmp_fd = open(ikvm->input_name, O_RDWR);

			if (tmp_fd >= 0) {
				struct timespec dur;
				short xx = 0x3FFF;
				char rpt[PTR_SIZE + 1];

				dur.tv_sec = 0;
				dur.tv_nsec =
					ikvm->process_events_time_us * 1000;

				memset(rpt, 0, PTR_SIZE + 1);
				rpt[0] = 2;
				memcpy(&rpt[2], &xx, 2);

				rc = write(tmp_fd, rpt, PTR_SIZE + 1);
				if (rc == PTR_SIZE + 1) {
					nanosleep(&dur, NULL);

					memset(&rpt[1], 0, PTR_SIZE);
					write(tmp_fd, rpt, PTR_SIZE + 1);
				}

				close(tmp_fd);
			}
		}

		ikvm->videodev_fd = open(ikvm->videodev_name, O_RDWR);
		if (ikvm->videodev_fd < 0) {
			printf("failed to open %s: %d %s\n",
			       ikvm->videodev_name, errno, strerror(errno));
			return -ENODEV;
		}
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

	set_frame_rate(ikvm);

	return alloc_frame(ikvm, &fmt);
}

static unsigned char key_to_mod(rfbKeySym key)
{
	unsigned char mod = 0;

	if (key >= XK_Shift_L && key <= XK_Control_R) {
		static const unsigned char map[] = {
			0x02,	// left shift
			0x20,	// right shift
			0x01,	// left control
			0x10	// right control
		};

		mod = map[key - XK_Shift_L];
	} else if (key >= XK_Meta_L && key <= XK_Alt_R) {
		static const unsigned char map[] = {
			0x08,	// left meta
			0x80,	// right meta
			0x04,	// left alt
			0x40	// right alt
		};

		mod = map[key - XK_Meta_L];
	}

	return mod;
}

static char key_to_scancode(rfbKeySym key)
{
	char scancode = 0;

	if ((key >= 'A' && key <= 'Z') || (key >= 'a' && key <= 'z')) {
		scancode = USBHID_KEY_A + ((key & 0x5F) - 'A');
	} else if (key >= '1' && key <= '9') {
		scancode = USBHID_KEY_1 + (key - '1');
	} else if (key >= XK_F1 && key <= XK_F12) {
		scancode = USBHID_KEY_F1 + (key - XK_F1);
	} else {
		switch (key) {
		case XK_exclam:
			scancode = USBHID_KEY_1;
			break;
		case XK_at:
			scancode = USBHID_KEY_2;
			break;
		case XK_numbersign:
			scancode = USBHID_KEY_3;
			break;
		case XK_dollar:
			scancode = USBHID_KEY_4;
			break;
		case XK_percent:
			scancode = USBHID_KEY_5;
			break;
		case XK_asciicircum:
			scancode = USBHID_KEY_6;
			break;
		case XK_ampersand:
			scancode = USBHID_KEY_7;
			break;
		case XK_asterisk:
			scancode = USBHID_KEY_8;
			break;
		case XK_parenleft:
			scancode = USBHID_KEY_9;
			break;
		case XK_0:
		case XK_parenright:
			scancode = USBHID_KEY_0;
			break;
		case XK_Return:
			scancode = USBHID_KEY_RETURN;
			break;
		case XK_Escape:
			scancode = USBHID_KEY_ESC;
			break;
		case XK_BackSpace:
			scancode = USBHID_KEY_BACKSPACE;
			break;
		case XK_Tab:
			scancode = USBHID_KEY_TAB;
			break;
		case XK_space:
			scancode = USBHID_KEY_SPACE;
			break;
		case XK_minus:
		case XK_underscore:
			scancode = USBHID_KEY_MINUS;
			break;
		case XK_plus:
		case XK_equal:
			scancode = USBHID_KEY_EQUAL;
			break;
		case XK_bracketleft:
		case XK_braceleft:
			scancode = USBHID_KEY_LEFTBRACE;
			break;
		case XK_bracketright:
		case XK_braceright:
			scancode = USBHID_KEY_RIGHTBRACE;
			break;
		case XK_backslash:
		case XK_bar:
			scancode = USBHID_KEY_BACKSLASH;
			break;
		case XK_colon:
		case XK_semicolon:
			scancode = USBHID_KEY_SEMICOLON;
			break;
		case XK_quotedbl:
		case XK_apostrophe:
			scancode = USBHID_KEY_APOSTROPHE;
			break;
		case XK_grave:
		case XK_asciitilde:
			scancode = USBHID_KEY_GRAVE;
			break;
		case XK_comma:
		case XK_less:
			scancode = USBHID_KEY_COMMA;
			break;
		case XK_period:
		case XK_greater:
			scancode = USBHID_KEY_DOT;
			break;
		case XK_slash:
		case XK_question:
			scancode = USBHID_KEY_SLASH;
			break;
		case XK_Caps_Lock:
			scancode = USBHID_KEY_CAPSLOCK;
			break;
		case XK_Print:
			scancode = USBHID_KEY_PRINT;
			break;
		case XK_Scroll_Lock:
			scancode = USBHID_KEY_SCROLLLOCK;
			break;
		case XK_Pause:
			scancode = USBHID_KEY_PAUSE;
			break;
		case XK_Insert:
			scancode = USBHID_KEY_INSERT;
			break;
		case XK_Home:
			scancode = USBHID_KEY_HOME;
			break;
		case XK_Page_Up:
			scancode = USBHID_KEY_PAGEUP;
			break;
		case XK_Delete:
			scancode = USBHID_KEY_DELETE;
			break;
		case XK_End:
			scancode = USBHID_KEY_END;
			break;
		case XK_Page_Down:
			scancode = USBHID_KEY_PAGEDOWN;
			break;
		case XK_Right:
			scancode = USBHID_KEY_RIGHT;
			break;
		case XK_Left:
			scancode = USBHID_KEY_LEFT;
			break;
		case XK_Down:
			scancode = USBHID_KEY_DOWN;
			break;
		case XK_Up:
			scancode = USBHID_KEY_UP;
			break;
		case XK_Num_Lock:
			scancode = USBHID_KEY_NUMLOCK;
			break;
		}
	}

	return scancode;
}

static void key_event(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
	struct obmc_ikvm *ikvm = cl->screen->screenData;

	DBG("key event %s %x\n", down ? "down" : "up", key);

	if (down) {
		char sc = key_to_scancode(key);

		if (sc) {
			unsigned int i;

			for (i = 2; i < ikvm->report_size; ++i) {
				if (!ikvm->report[i]) {
					ikvm->report[i] = sc;
					ikvm->report_map[i - 2] = key;
					goto update_send_report;
				}
			}

			DBG("no space in report for additional key press!\n");
			return;
		} else {
			unsigned char mod = key_to_mod(key);

			if (mod) {
				ikvm->report[0] |= mod;
				goto update_send_report;
			}

			return;
		}
	} else {
		unsigned char mod;
		unsigned int i;

		for (i = 0; i < ikvm->report_size - 2; ++i) {
			if (ikvm->report_map[i] == key) {
				ikvm->report_map[i] = 0;
				ikvm->report[i + 2] = 0;
				goto update_send_report;
			}
		}

		mod = key_to_mod(key);
		if (mod) {
			ikvm->report[0] &= ~mod;
			goto update_send_report;
		}

		return;
	}

update_send_report:
	ikvm->send_report = true;
}

static void init_keyboard(struct obmc_ikvm *ikvm)
{
	ikvm->keyboard_fd = open(ikvm->keyboard_name, O_RDWR);
	if (ikvm->keyboard_fd < 0) {
		printf("failed to open %s: %d %s\n", ikvm->keyboard_name,
		       errno, strerror(errno));
		return;
	}

	ikvm->server->kbdAddEvent = key_event;
}

static void keyboard_send_report(struct obmc_ikvm *ikvm)
{
	if (ikvm->send_report) {
		int fd;
		char rpt[REPORT_SIZE];
		unsigned char *data;

		if (ikvm->input_fd >= 0) {
			fd = ikvm->input_fd;

			rpt[0] = 1;
			memcpy(&rpt[1], ikvm->report, ikvm->report_size);

			data = rpt;
		} else {
			fd = ikvm->keyboard_fd;

			data = ikvm->report;
		}

		DBG("sending kbd report[%02x%02x%02x%02x%02x%02x%02x%02x]\n",
		    data[0], data[1], data[2], data[3], data[4], data[5],
		    data[6], data[7]);
		if (write(fd, data, REPORT_SIZE) != REPORT_SIZE)
			printf("failed to write keyboard report: %d %s\n",
			       errno, strerror(errno));

		ikvm->send_report = false;
	}
}

static void ptr_event(int button_mask, int x, int y, rfbClientPtr cl)
{
	struct obmc_ikvm *ikvm = cl->screen->screenData;

	DBG("ptr event btn[%x] x[%d] y[%d]\n", button_mask, x, y);

	ikvm->ptr[0] = button_mask & 0xFF;

	if (x >= 0 && x < ikvm->resolution.width) {
		short xx = x * (0x8000 / ikvm->resolution.width);

		memcpy(&ikvm->ptr[1], &xx, 2);
	}

	if (y >= 0 && y < ikvm->resolution.height) {
		short yy = y * (0x8000 / ikvm->resolution.height);

		memcpy(&ikvm->ptr[3], &yy, 2);
	}

	ikvm->send_ptr = true;

	rfbDefaultPtrAddEvent(button_mask, x, y, cl);
}

static void init_ptr(struct obmc_ikvm *ikvm)
{
	ikvm->ptr_fd = open(ikvm->ptr_name, O_RDWR);
	if (ikvm->ptr_fd < 0) {
		printf("failed to open %s: %d %s\n", ikvm->ptr_name, errno,
		       strerror(errno));
		return;
	}

	ikvm->server->ptrAddEvent = ptr_event;
}

static void ptr_send_report(struct obmc_ikvm *ikvm)
{
	if (ikvm->send_ptr) {
		int fd;
		char rpt[PTR_SIZE + 1];
		size_t rs;
		unsigned char *data;

		if (ikvm->input_fd >= 0) {
			fd = ikvm->input_fd;
			rs = PTR_SIZE + 1;

			rpt[0] = 2;
			memcpy(&rpt[1], ikvm->ptr, PTR_SIZE);

			data = rpt;

			DBG("sending ptr report[%02x%02x%02x%02x%02x%02x]\n",
			    data[0], data[1], data[2], data[3], data[4],
			    data[5]);
		} else {
			fd = ikvm->ptr_fd;
			rs = PTR_SIZE;

			data = ikvm->ptr;

			DBG("sending ptr report[%02x%02x%02x%02x%02x]\n",
			    data[0], data[1], data[2], data[3], data[4]);
		}

		if (write(fd, data, rs) != rs)
			printf("failed to write ptr report: %d %s\n", errno,
			       strerror(errno));

		ikvm->send_ptr = false;
	}
}

static void init_input(struct obmc_ikvm *ikvm)
{
	ikvm->input_fd = open(ikvm->input_name, O_RDWR);
	if (ikvm->input_fd < 0) {
		printf("failed to open %s: %d %s\n", ikvm->input_name, errno,
		       strerror(errno));
		return;
	}

	ikvm->server->kbdAddEvent = key_event;
	ikvm->server->ptrAddEvent = ptr_event;
	ikvm->report_size = REPORT_SIZE - 1;
}

static void client_gone(rfbClientPtr cl)
{
	struct obmc_ikvm *ikvm = cl->clientData;

	if (ikvm->num_clients-- > 1)
		return;

	if (ikvm->videodev_fd >= 0) {
		printf("close(ikvm->videodev_fd)\n");
		close(ikvm->videodev_fd);
		ikvm->videodev_fd = open(ikvm->videodev_name, O_RDWR);
		printf("open(ikvm->videodev_fd)\n");
		if (ikvm->videodev_fd < 0) {
			printf("failed to re-open %s: %d %s\n",
			       ikvm->videodev_name, errno, strerror(errno));
			ok = false;
		} else {
			set_frame_rate(ikvm);

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
	ikvm->delay_count = ikvm->frame_rate;

	return RFB_CLIENT_ACCEPT;
}

static int init_server(struct obmc_ikvm *ikvm, int *argc, char **argv)
{
	rfbPixelFormat *format;

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

	format = &ikvm->server->serverFormat;
	format->redMax = 31;
	format->greenMax = 63;
	format->blueMax = 31;
	format->redShift = 11;
	format->greenShift = 5;
	format->blueShift = 0;

	rfbMarkRectAsModified(ikvm->server, 0, 0, ikvm->resolution.width,
			      ikvm->resolution.height);

	return 0;
}

static rfbBool
rfbHextile16(rfbClientPtr cl, struct obmc_ikvm *ikvm) {
	struct v4l2_format fmt;
	int err = 0;
	uint32_t padding_len = 0;
	uint32_t copy_len = 0;
	char *copy_addr = ikvm->frame;
	rfbFramebufferUpdateRectHeader rect;
	rfbFramebufferUpdateMsg *fu =
		(rfbFramebufferUpdateMsg *)cl->updateBuf;

	if (ikvm->frame_size == 0)
		return TRUE;

	fu->type = rfbFramebufferUpdate;

	if (cl->enableLastRectEncoding)
		fu->nRects = 0xFFFF;
	else
		fu->nRects = Swap16IfLE(ikvm->nRects);

	cl->ublen = sz_rfbFramebufferUpdateMsg;

	rfbSendUpdateBuf(cl);

	if (ikvm->frame_size >= (UPDATE_BUF_SIZE - cl->ublen)) {
		padding_len = ikvm->frame_size - (UPDATE_BUF_SIZE - cl->ublen);
		memcpy(&cl->updateBuf[cl->ublen], copy_addr, (UPDATE_BUF_SIZE - cl->ublen));

		copy_addr += (UPDATE_BUF_SIZE - cl->ublen);
		cl->ublen += (UPDATE_BUF_SIZE - cl->ublen);
		do {
			if (!rfbSendUpdateBuf(cl)) {
				rfbLog("rfbSendUpdateBuf FAIL\n");
				return FALSE;
			}

			copy_len = padding_len;
			if (padding_len > (UPDATE_BUF_SIZE - cl->ublen)) {
				padding_len -= (UPDATE_BUF_SIZE - cl->ublen);
				copy_len = (UPDATE_BUF_SIZE - cl->ublen);
			} else
				padding_len = 0;

			memcpy(&cl->updateBuf[cl->ublen], copy_addr, copy_len);
			cl->ublen += copy_len;
			copy_addr += copy_len;
		} while (padding_len != 0);
	} else {
		memcpy(&cl->updateBuf[cl->ublen], copy_addr, ikvm->frame_size);
		cl->ublen += ikvm->frame_size;
		padding_len = 0;
    }

	if (cl->enableLastRectEncoding)
		rfbSendLastRectMarker(cl);

	rfbSendUpdateBuf(cl);

    return TRUE;
}

static void send_frame_to_clients(struct obmc_ikvm *ikvm)
{
	if (ikvm->wait_next) {
		ikvm->wait_next = false;

		/* Wait for the rfb processing thread to finish it's work */
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&cond, &mutex);
		ikvm->dont_wait = true;
	}

	rfbClientIteratorPtr iterator = rfbGetClientIterator(ikvm->server);
	rfbClientPtr cl;

	while (cl = rfbClientIteratorNext(iterator)) {
#if 1
		rfbHextile16(cl, ikvm);

#else
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
#endif
	}

	rfbReleaseClientIterator(iterator);
}

static int get_frame(struct obmc_ikvm *ikvm)
{
	int rc;
	struct v4l2_format fmt;

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rc = ioctl(ikvm->videodev_fd, VIDIOC_G_FMT, &fmt);
	if (rc < 0) {
		printf("failed to query format: %d %s\n", errno,
		       strerror(errno));
		return -EFAULT;
	}

	if (fmt.fmt.pix.width != ikvm->resolution.width ||
	    fmt.fmt.pix.height != ikvm->resolution.height) {
		char *old_frame = ikvm->frame;

		rc = alloc_frame(ikvm, &fmt);
		if (rc)
			return rc;

		/* Wait for the rfb processing thread to finish it's work */
		pthread_mutex_lock(&mutex);
		pthread_cond_wait(&cond, &mutex);
		ikvm->dont_wait = true;

		rfbNewFramebuffer(ikvm->server, ikvm->frame,
				  ikvm->resolution.width,
				  ikvm->resolution.height,
				  BITS_PER_SAMPLE, SAMPLES_PER_PIXEL,
				  BYTES_PER_PIXEL);
		rfbMarkRectAsModified(ikvm->server, 0, 0,
				      ikvm->resolution.width,
				      ikvm->resolution.height);

		free(old_frame);

		/* Get the image on the next iteration */
		ikvm->wait_next = true;
		return 0;
	}


	fmt.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
	rc = ioctl(ikvm->videodev_fd, VIDIOC_G_FMT, &fmt);
	if (rc < 0) {
		printf("failed to query format: %d %s\n", errno,
			strerror(errno));
		return -EFAULT;
	}

	ikvm->nRects = fmt.fmt.win.clipcount;

	rc = read(ikvm->videodev_fd, ikvm->frame, ikvm->frame_buf_size);
	if (rc < 0) {
		printf("failed to read frame: %d %s\n", errno,
		       strerror(errno));
		return -EFAULT;
	}

	if (rc != ikvm->frame_size)
		DBG("new frame size: %d\n", rc);

	ikvm->frame_size = rc;
	send_frame_to_clients(ikvm);

	return 0;
}

static int timespec_subtract(struct timespec *result, struct timespec *x,
			     struct timespec *y)
{
	/* Perform the carry for the later subtraction by updating y. */
	if (x->tv_nsec < y->tv_nsec) {
		long long int nsec =
			((y->tv_nsec - x->tv_nsec) / 1000000000ULL) + 1;

		y->tv_nsec -= 1000000000ULL * nsec;
		y->tv_sec += nsec;
	}

	if (x->tv_nsec - y->tv_nsec > 1000000000ULL) {
		long long int nsec = (x->tv_nsec - y->tv_nsec) / 1000000000ULL;

		y->tv_nsec += 1000000000ULL * nsec;
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

static void dump_frame(struct obmc_ikvm *ikvm)
{
	int fd;
	int rc;
	char path[256];

	snprintf(path, 256, "%s/frame%03d.bin", DUMP_FRAME_DIR,
		 ikvm->dump_frame_idx++);

	fd = open(path, O_WRONLY | O_CREAT, 0666);
	if (fd < 0) {
		printf("failed to open %s: %d %s\n", path, errno,
		       strerror(errno));
		return;
	}

	rc = write(fd, ikvm->frame, ikvm->frame_size);
	if (rc < ikvm->frame_size)
		printf("failed to write frame: %d %s\n", errno,
		       strerror(errno));

	close(fd);
}

void *threaded_process_rfb(void *ptr)
{
	struct timespec diff;
	struct timespec end;
	struct timespec start;
	struct obmc_ikvm *ikvm = (struct obmc_ikvm *)ptr;

	while (ok) {
		rfbProcessEvents(ikvm->server, ikvm->process_events_time_us);
#ifdef _PROFILE_
		clock_gettime(CLOCK_MONOTONIC, &start);
#endif /* _PROFILE_ */
		if (ikvm->server->clientHead != NULL && ok) {
			keyboard_send_report(ikvm);
			ptr_send_report(ikvm);
		}
#ifdef _PROFILE_
		clock_gettime(CLOCK_MONOTONIC, &end);
		timespec_subtract(&diff, &end, &start);
		_prof(&diff, &_input);
		DBG("input: %lld.%.9ld\n", (long long)diff.tv_sec,
		    diff.tv_nsec);
#endif /* _PROFILE_ */

		pthread_mutex_lock(&mutex);
		pthread_cond_broadcast(&cond);
		pthread_mutex_unlock(&mutex);
	}

	return NULL;
}

void usage()
{
	fprintf(stderr, "OpenBMC IKVM daemon\n");
	fprintf(stderr, "Usage: obmc-ikvm [options]\n");
	fprintf(stderr, "-f frame rate          use this frame rate\n");
	fprintf(stderr, "-i device              HID gadget combined device\n");
	fprintf(stderr, "-k keyboard            HID gadget keyboard device\n");
	fprintf(stderr, "-p mouse               HID gadget mouse device\n");
	fprintf(stderr, "-v device              V4L2 device\n");
	rfbUsage();
}

int main(int argc, char **argv)
{
	int len;
	int option;
	int rc;
	const char *opts = "dhi:k:p:v:";
	struct option lopts[] = {
		{ "dump_frames", 0, 0, 'd' },
		{ "frame_rate", 1, 0, 'f' },
		{ "help", 0, 0, 'h' },
		{ "input", 1, 0, 'i' },
		{ "keyboard", 1, 0, 'k' },
		{ "pointer", 1, 0, 'p' },
		{ "videodev", 1, 0, 'v' },
		{ 0, 0, 0, 0 }
	};
	struct obmc_ikvm ikvm;
	struct timespec diff;
	struct timespec end;
	struct timespec start;
	pthread_t rfb;

	memset(&ikvm, 0, sizeof(struct obmc_ikvm));
	ikvm.frame_rate = 30;
	ikvm.videodev_fd = -1;
	ikvm.input_fd = -1;
	ikvm.keyboard_fd = -1;
	ikvm.ptr_fd = -1;
	ikvm.report_size = REPORT_SIZE;

	while ((option = getopt_long(argc, argv, opts, lopts, NULL)) != -1) {
		switch (option) {
		case 'd':
			ikvm.dump_frames = true;
			rc = mkdir(DUMP_FRAME_DIR, 0777);
			if (rc) {
				printf("failed to create dir %s: %d %s\n",
				       DUMP_FRAME_DIR, errno, strerror(errno));
				ikvm.dump_frames = false;
			}
			break;
		case 'f':
			ikvm.frame_rate = (int)strtol(optarg, NULL, 0);
			if (ikvm.frame_rate < 0 || ikvm.frame_rate >= 60)
				ikvm.frame_rate = 30;
		case 'i':
			if (ikvm.keyboard_fd >= 0 || ikvm.ptr_fd >= 0)
				break;

			ikvm.input_name = malloc(strlen(optarg) + 1);
			if (!ikvm.input_name)
				printf("failed to allocate input name\n");
			else
				strcpy(ikvm.input_name, optarg);
			break;
		case 'k':
			if (ikvm.input_fd >= 0)
				break;

			ikvm.keyboard_name = malloc(strlen(optarg) + 1);
			if (!ikvm.keyboard_name)
				printf("failed to allocate keyboard name\n");
			else
				strcpy(ikvm.keyboard_name, optarg);
			break;
		case 'p':
			if (ikvm.input_fd >= 0)
				break;

			ikvm.ptr_name = malloc(strlen(optarg) + 1);
			if (!ikvm.ptr_name)
				printf("failed to allocate ptr name\n");
			else
				strcpy(ikvm.ptr_name, optarg);
			break;
		case 'v':
			ikvm.videodev_name = malloc(strlen(optarg) + 1);
			if (!ikvm.videodev_name) {
				rc = -ENOMEM;
				goto done;
			}

			strcpy(ikvm.videodev_name, optarg);
			break;
		case 'h':
			usage();
			goto done;
			break;
		}
	}

	ikvm.frame_time_us = 1000000 / ikvm.frame_rate;
	ikvm.process_events_time_us = ikvm.frame_time_us -
		PROCESS_EVENTS_DELTA;

	rc = init_videodev(&ikvm);
	if (rc)
		goto done;

	rc = init_server(&ikvm, &argc, argv);
	if (rc)
		goto done;

	if (ikvm.input_name) {
		init_input(&ikvm);
	} else {
		if (ikvm.keyboard_name)
			init_keyboard(&ikvm);

		if (ikvm.ptr_name)
			init_ptr(&ikvm);
	}

	signal(SIGINT, int_handler);

	pthread_create(&rfb, NULL, threaded_process_rfb, &ikvm);

	while (ok) {
		if (ikvm.delay_count)
			ikvm.delay_count--;
		else if (ikvm.server->clientHead != NULL || ikvm.dump_frames) {
#ifdef _PROFILE_
			clock_gettime(CLOCK_MONOTONIC, &start);
#endif /* _PROFILE_ */
			rc = get_frame(&ikvm);
			if (rc) {
				ok = false;
				break;
			}

			if (ikvm.dump_frames)
				dump_frame(&ikvm);
#ifdef _PROFILE_
			clock_gettime(CLOCK_MONOTONIC, &end);
			timespec_subtract(&diff, &end, &start);
			_prof(&diff, &_frame);
			DBG("frame: %lld.%.9ld\n", (long long)diff.tv_sec,
			    diff.tv_nsec);
#endif /* _PROFILE */
		}

#ifdef _PROFILE_
		clock_gettime(CLOCK_MONOTONIC, &start);
#endif /* _PROFILE_ */
		if (!ikvm.dont_wait) {
			pthread_mutex_lock(&mutex);
			pthread_cond_wait(&cond, &mutex);
			pthread_mutex_unlock(&mutex);
		} else {
			pthread_mutex_unlock(&mutex);
			ikvm.dont_wait = false;
		}
#ifdef _PROFILE_
		clock_gettime(CLOCK_MONOTONIC, &end);
		timespec_subtract(&diff, &end, &start);
		_prof(&diff, &_wait);
		DBG("waited: %lld.%.9ld\n", (long long)diff.tv_sec,
		    diff.tv_nsec);
#endif /* _PROFILE_ */
	}

	pthread_join(rfb, NULL);

#ifdef _PROFILE_
	printf("avg frame time (us): %lld\n", _avg(&_frame));
	printf("avg input time (us): %lld\n", _avg(&_input));
	printf("avg wait time (us): %lld\n", _avg(&_wait));
#endif /* _PROFILE_ */

done:
	if (ikvm.server)
		rfbScreenCleanup(ikvm.server);

	if (ikvm.frame)
		free(ikvm.frame);

	if (ikvm.videodev_fd >= 0)
		close(ikvm.videodev_fd);

	if (ikvm.input_fd >= 0)
		close(ikvm.input_fd);

	if (ikvm.keyboard_fd >= 0)
		close(ikvm.keyboard_fd);

	if (ikvm.ptr_fd >= 0)
		close(ikvm.ptr_fd);

	if (ikvm.input_name)
		free(ikvm.input_name);

	if (ikvm.keyboard_name)
		free(ikvm.keyboard_name);

	if (ikvm.ptr_name)
		free(ikvm.ptr_name);

	if (ikvm.videodev_name)
		free(ikvm.videodev_name);

	return rc;
}

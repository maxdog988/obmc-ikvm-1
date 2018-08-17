#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/log.hpp>
#include <sys/types.h>
#include <sys/stat.h>

#include "ikvm_video.hpp"

namespace ikvm
{

using namespace phosphor::logging;

Video::Video(const std::string &p, int fr) :
    frameRate(fr),
    path(p)
{
    Open();
}

void Video::reset()
{
    if (fd >= 0)
    {
        close(fd);
        fd = open(path.c_str(), O_RDWR);
        if (fd < 0)
        {
            log<level::ERR>("Failed to re-open video device",
                            entry("PATH=%s", path.c_str()),
                            entry("ERROR=%s", strerror(errno)));
            elog<OpenFailure>();
        }

        data.assign(data.size(), 0);
    }
}

void Video::open()
{
    int rc;
    struct v4l2_capability cap;
    struct v4l2_format fmt;

    fd = open(path.c_str(), O_RDWR);
    if (fd < 0)
    {
        log<level::ERR>("Failed to open video device",
                        entry("PATH=%s", path.c_str()),
                        entry("ERROR=%s", strerror(errno)));
        elog<OpenFailure>();
    }

    rc = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (rc < 0)
    {
        log<level::ERR>("Failed to query video device capabilities",
                        entry("ERROR=%s", strerror(errno)));
        elog<OpenFailure>();
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(cap.capabilities & V4L2_CAP_READWRITE))
    {
        log<level::ERR>("Video device doesn't support this application");
        elog<OpenFailure>();
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rc = ioctl(fd, VIDIOC_G_FMT, &fmt);
    if (rc < 0)
    {
        log<level::ERR>("Failed to query video device format",
                        entry("ERROR=%s", strerror(errno)));
        elog<OpenFailure>();
    }

    setFrameRate();

    resize(fmt.pix.width, fmt.pix.height);
}

void Video::resize(size_t w, size_t h)
{
    width = w;
    height = h;

    data.resize(width * height * 4, 0); // four bytes per pixel
}

void Video::setFrameRate()
{
    int rc;
    struct v4l2_streamparm sparm;

    sparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    sparm.parm.capture.timeperframe.numerator = 1;
    sparm.parm.capture.timeperframe.denominator = frameRate;

    rc = ioctl(fd, VIDIOC_S_PARM, &sparm);
    if (rc < 0)
    {
        log<level::WARN>("Failed to set video device frame rate",
                         entry("ERROR=%s", strerror(errno)));
    }
}

} // namespace ikvm

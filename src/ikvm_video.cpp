#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
//#include <phosphor-logging/elog.hpp>
//#include <phosphor-logging/log.hpp>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "ikvm_video.hpp"

namespace ikvm
{

const int Video::bitsPerSample(8);
const int Video::bytesPerPixel(4);
const int Video::samplesPerPixel(3);

//using namespace phosphor::logging;

Video::Video(const std::string &p, Input& input, int fr) :
    frameRate(fr),
    path(p)
{
    int rc;
    struct v4l2_capability cap;
    struct v4l2_format fmt;
    struct v4l2_streamparm sparm;

    fd = open(path.c_str(), O_RDWR);
    if (fd < 0)
    {
        unsigned short xx = SHRT_MAX;
        char wakeupReport[6] = { 0 };

        wakeupReport[0] = 2;
        memcpy(&wakeupReport[2], &xx, 2);

        input.sendRaw(wakeupReport, 6);

        fd = open(path.c_str(), O_RDWR);
        if (fd < 0)
        {
            //log<level::ERR>("Failed to open video device",
                            //entry("PATH=%s", path.c_str()),
                            //entry("ERROR=%s", strerror(errno)));
            //elog<OpenFailure>();
            throw std::runtime_error("video");
        }
    }

    rc = ioctl(fd, VIDIOC_QUERYCAP, &cap);
    if (rc < 0)
    {
        //log<level::ERR>("Failed to query video device capabilities",
                        //entry("ERROR=%s", strerror(errno)));
        //elog<OpenFailure>();
        throw std::runtime_error("query");
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ||
        !(cap.capabilities & V4L2_CAP_READWRITE))
    {
        //log<level::ERR>("Video device doesn't support this application");
        //elog<OpenFailure>();
        throw std::runtime_error("cap");
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rc = ioctl(fd, VIDIOC_G_FMT, &fmt);
    if (rc < 0)
    {
        //log<level::ERR>("Failed to query video device format",
                        //entry("ERROR=%s", strerror(errno)));
        //elog<OpenFailure>();
        throw std::runtime_error("fmt");
    }

    sparm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    sparm.parm.capture.timeperframe.numerator = 1;
    sparm.parm.capture.timeperframe.denominator = frameRate;
    rc = ioctl(fd, VIDIOC_S_PARM, &sparm);
    if (rc < 0)
    {
        //log<level::WARN>("Failed to set video device frame rate",
                         //entry("ERROR=%s", strerror(errno)));
    }

    height = fmt.fmt.pix.height;
    width = fmt.fmt.pix.width;

    resize();
}

void Video::getFrame(bool& needsResize)
{
    int rc;
    struct v4l2_format fmt;

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rc = ioctl(fd, VIDIOC_G_FMT, &fmt);
    if (rc < 0)
    {
        //log<level::ERR>("Failed to query format",
                        //entry("ERROR=%s", strerror(errno)));
        //elog<ReadFailure>();
        throw std::runtime_error("fmt");
    }

    if (fmt.fmt.pix.height != height || fmt.fmt.pix.width != width)
    {
        height = fmt.fmt.pix.height;
        width = fmt.fmt.pix.width;
        needsResize = true;
        return;
    }

    rc = read(fd, data.data(), data.size());
    if (rc < 0)
    {
        //log<level::ERR>("Failed to read frame",
                        //entry("ERROR=%s", strerror(errno)));
        //elog<ReadFailure>();
        throw std::runtime_error("read");
    }

    frameSize = rc;
}

void Video::reset()
{
    if (fd >= 0)
    {
        close(fd);
        fd = open(path.c_str(), O_RDWR);
        if (fd < 0)
        {
            //log<level::ERR>("Failed to re-open video device",
                            //entry("PATH=%s", path.c_str()),
                            //entry("ERROR=%s", strerror(errno)));
            //elog<OpenFailure>();
            throw std::runtime_error("reopen");
        }

        data.assign(data.size(), 0);
    }
}

void Video::resize()
{
    data.resize(width * height * bytesPerPixel, 0);
}

} // namespace ikvm

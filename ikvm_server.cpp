#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/log.hpp>
#include <rfb/rfbproto.h>
#include <xyz/openbmc_project/Common/error.hpp>

#include "ikvm_server.hpp"

namespace ikvm {

using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

Server::Server(const Args& args, Input& i, Video& v) :
    pendingResize(false),
    frameCounter(0),
    numClients(0),
    input(i),
    video(v)
{
    const Args::CommandLine& commandLine = args.getCommandLine();
    int argc = commandLine.argc;

    server = rfbGetScreen(&argc, commandLine.argv, video.getWidth(),
                          video.getHeight(), Video::bitsPerSample,
                          Video::samplesPerPixel, Video::bytesPerPixel);

    if (!server)
    {
        log<level::ERR>("Failed to get VNC screen due to invalid arguments");
        elog<InvalidArgument>(
            xyz::openbmc_project::Common::InvalidArgument::ARGUMENT_NAME(""),
            xyz::openbmc_project::Common::InvalidArgument::ARGUMENT_VALUE(""));
    }

    framebuffer.resize(
        video.getHeight() * video.getWidth() * Video::bytesPerPixel, 0);

    format = &server->serverFormat;

    format->redMax = 31;
    format->greenMax = 63;
    format->blueMax = 31;
    format->redShift = 11;
    format->greenShift = 5;
    format->blueShift = 0;

    server->screenData = this;
    server->desktopName = "OpenBMC IKVM";
    server->frameBuffer = framebuffer.data();
    server->newClientHook = newClient;

    rfbInitServer(server);

    rfbMarkRectAsModified(server, 0, 0, video.getWidth(), video.getHeight());

    server->kbdAddEvent = Input::keyEvent;
    server->ptrAddEvent = Input::pointerEvent;

    processTime = (1000000 / video.getFrameRate()) - 100;
}

Server::~Server()
{
    rfbScreenCleanup(server);
}

void Server::resize()
{
    if (frameCounter > video.getFrameRate() / 2)
    {
        doResize();
    }
    else
    {
        pendingResize = true;
    }
}

void Server::run()
{
    rfbProcessEvents(server, processTime);

    if (server->clientHead)
    {
        input.sendReport();

        frameCounter++;
        if (pendingResize && frameCounter > video.getFrameRate() / 2)
        {
            doResize();
            pendingResize = false;
        }
    }
}

void Server::SendCompressedDataHextile16(rfbClientPtr cl, char *data, int frameSize)
{
    int padding_len = 0, copy_len = 0;

    if (frameSize >= (UPDATE_BUF_SIZE - cl->ublen)) {
        padding_len = frameSize - (UPDATE_BUF_SIZE - cl->ublen);
        memcpy(&cl->updateBuf[cl->ublen], data, (UPDATE_BUF_SIZE - cl->ublen));
        data += (UPDATE_BUF_SIZE - cl->ublen);
        cl->ublen += (UPDATE_BUF_SIZE - cl->ublen);
        do {
            if (!rfbSendUpdateBuf(cl))
                return;

            copy_len = padding_len;
            if (padding_len > (UPDATE_BUF_SIZE - cl->ublen)) {
                padding_len -= (UPDATE_BUF_SIZE - cl->ublen);
                copy_len = (UPDATE_BUF_SIZE - cl->ublen);
            } else
                padding_len = 0;

            memcpy(&cl->updateBuf[cl->ublen], data, copy_len);
            cl->ublen += copy_len;
            data += copy_len;
        } while (padding_len != 0 );
    } else {
        memcpy(&cl->updateBuf[cl->ublen], data, frameSize);
        cl->ublen += frameSize;
        padding_len = 0;
    }
}

void Server::sendFrame()
{
    char *data = video.getData();
    rfbClientIteratorPtr it;
    rfbClientPtr cl;

    if (!data || pendingResize)
    {
        return;
    }

    it = rfbGetClientIterator(server);

    while ((cl = rfbClientIteratorNext(it)))
    {
        ClientData *cd = (ClientData *)cl->clientData;
        rfbFramebufferUpdateMsg *fu = (rfbFramebufferUpdateMsg *)cl->updateBuf;

        if (!cd)
        {
            continue;
        }

        if (cd->skipFrame)
        {
            cd->skipFrame--;
            continue;
        }

        if (!video.getFrameSize())
             continue;

        if (cl->enableLastRectEncoding)
        {
            fu->nRects = 0xFFFF;
        }
        else
        {
            fu->nRects = Swap16IfLE(video.getClipCount());
        }

        fu->type = rfbFramebufferUpdate;
        cl->ublen = sz_rfbFramebufferUpdateMsg;
        rfbSendUpdateBuf(cl);

        SendCompressedDataHextile16(cl, data, video.getFrameSize());
#if 0
        cl->tightEncoding = rfbEncodingTight;
        rfbSendTightHeader(cl, 0, 0, video.getWidth(), video.getHeight());

        cl->updateBuf[cl->ublen++] = (char)(rfbTightJpeg << 4);
        rfbSendCompressedDataTight(cl, data, video.getFrameSize());
#endif
        if (cl->enableLastRectEncoding)
        {
            rfbSendLastRectMarker(cl);
        }

        rfbSendUpdateBuf(cl);
    }

    rfbReleaseClientIterator(it);
}

void Server::clientGone(rfbClientPtr cl)
{
    Server *server = (Server *)cl->screen->screenData;

    delete (ClientData *)cl->clientData;

    if (server->numClients-- == 1)
    {
        rfbMarkRectAsModified(server->server, 0, 0, server->video.getWidth(),
                              server->video.getHeight());
    }
}

enum rfbNewClientAction Server::newClient(rfbClientPtr cl)
{
    Server *server = (Server *)cl->screen->screenData;

    cl->clientData = new ClientData(server->video.getFrameRate() / 2,
                                    &server->input);
    cl->clientGoneHook = clientGone;
    if (!server->numClients++)
    {
        server->pendingResize = false;
        server->frameCounter = 0;
        server->video.start();
    }

    return RFB_CLIENT_ACCEPT;
}

void Server::doResize()
{
    rfbClientIteratorPtr it;
    rfbClientPtr cl;

    framebuffer.resize(
        video.getHeight() * video.getWidth() * Video::bytesPerPixel, 0);

    rfbNewFramebuffer(server, framebuffer.data(), video.getWidth(),
                      video.getHeight(), Video::bitsPerSample,
                      Video::samplesPerPixel, Video::bytesPerPixel);
    rfbMarkRectAsModified(server, 0, 0, video.getWidth(), video.getHeight());

    it = rfbGetClientIterator(server);

    while ((cl = rfbClientIteratorNext(it)))
    {
        ClientData *cd = (ClientData *)cl->clientData;

        if (!cd)
        {
            continue;
        }

        // delay video updates to give the client time to resize
        cd->skipFrame = video.getFrameRate() / 2;
    }

    rfbReleaseClientIterator(it);
}

} // namespace ikvm

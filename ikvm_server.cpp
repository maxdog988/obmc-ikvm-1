#include "ikvm_server.hpp"

#include <rfb/rfbproto.h>

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/log.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

namespace ikvm
{

using namespace phosphor::logging;
using namespace sdbusplus::xyz::openbmc_project::Common::Error;

Server::Server(const Args& args, Input& i, Video& v) :
    pendingResize(false), frameCounter(0), numClients(0), input(i), video(v), encoding(args.getEncoding())
{
    //std::string ip("localhost");
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
    format->redMax = Video::redMax;
    format->greenMax = Video::greenMax;
    format->blueMax = Video::blueMax;
    format->redShift = Video::redShift;
    format->greenShift = Video::greenShift;
    format->blueShift = Video::blueShift;

    server->screenData = this;
    server->desktopName = "OpenBMC IKVM";
    server->frameBuffer = framebuffer.data();
    server->newClientHook = newClient;

    //rfbStringToAddr(&ip[0], &server->listenInterface);

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
    if (frameCounter > video.getFrameRate())
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
        if (pendingResize && frameCounter > video.getFrameRate())
        {
            doResize();
            pendingResize = false;
        }
    }
}

void Server::rfbSendframe(rfbClientPtr cl, char *data, int len)
{
    int i, portionLen;

    portionLen = UPDATE_BUF_SIZE;

    for (i = 0; i < len; i += portionLen) {
        if (i + portionLen > len) {
            portionLen = len - i;
        }
        if (cl->ublen + portionLen > UPDATE_BUF_SIZE) {
            if (!rfbSendUpdateBuf(cl))
                return;
        }
        memcpy(&cl->updateBuf[cl->ublen], &data[i], portionLen);
        cl->ublen += portionLen;
    }
}

void Server::sendFrame()
{
    char* data = video.getData();
    rfbClientIteratorPtr it;
    rfbClientPtr cl;

    if (!data || pendingResize)
    {
        return;
    }

    it = rfbGetClientIterator(server);

    while ((cl = rfbClientIteratorNext(it)))
    {
        ClientData* cd = (ClientData*)cl->clientData;
        rfbFramebufferUpdateMsg* fu = (rfbFramebufferUpdateMsg*)cl->updateBuf;
        rfbStatList *ptr = rfbStatLookupMessage(cl, rfbFramebufferUpdateRequest);

        if (!cd)
        {
            continue;
        }

        if (!ptr->rcvdCount || cl->onHold || cl->state != 4)
            continue;

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
            if (!encoding.compare("rgb565"))
                fu->nRects = Swap16IfLE(1);
            else
                fu->nRects = Swap16IfLE(video.getClipCount());
        }

        fu->type = rfbFramebufferUpdate;
        cl->ublen = sz_rfbFramebufferUpdateMsg;
        rfbSendUpdateBuf(cl);

        if (!encoding.compare("rgb565")) {
            cl->scaledScreen->frameBuffer = data;
            rfbSendRectEncodingHextile(cl, 0, 0, video.getWidth(), video.getHeight());
        } else {
            rfbSendframe(cl, data, video.getFrameSize());
        }

        rfbSendUpdateBuf(cl);
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
    Server* server = (Server*)cl->screen->screenData;

    delete (ClientData*)cl->clientData;

    if (server->numClients-- == 1)
    {
        rfbMarkRectAsModified(server->server, 0, 0, server->video.getWidth(),
                              server->video.getHeight());
    }
}

enum rfbNewClientAction Server::newClient(rfbClientPtr cl)
{
    Server* server = (Server*)cl->screen->screenData;

    cl->clientData =
        new ClientData(server->video.getFrameRate(), &server->input);
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
    
    format = &server->serverFormat;
    format->redMax = Video::redMax;
    format->greenMax = Video::greenMax;
    format->blueMax = Video::blueMax;
    format->redShift = Video::redShift;
    format->greenShift = Video::greenShift;
    format->blueShift = Video::blueShift;

    rfbMarkRectAsModified(server, 0, 0, video.getWidth(), video.getHeight());

    it = rfbGetClientIterator(server);

    while ((cl = rfbClientIteratorNext(it)))
    {
        ClientData* cd = (ClientData*)cl->clientData;

        if (!cd)
        {
            continue;
        }

        // delay video updates to give the client time to resize
        cd->skipFrame = video.getFrameRate();
    }

    rfbReleaseClientIterator(it);
}

} // namespace ikvm

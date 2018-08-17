#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/log.hpp>

#include "ikvm_server.hpp"

namespace ikvm {

Server::Server(const Args& args, Video& video) :
    numClients(0),
    input(args.getInputPath()),
    video(v)
{
    const Args::CommandLine& commandLine = args.getCommandLine();
    int argc = commandLine.argc;

    server = rfbGetScreen(&argc, commandLine.argv, video.getWidth(),
                          video.getHeight(), 8, 3, 4);

    if (!server)
    {
        log<level::ERR>("Failed to get VNC screen");
        elog<OpenFailure>();
    }

    server->screenData = this;
    server->desktopName = "OpenBMC IKVM";
    server->frameBuffer = video.getData();
    server->alwaysShared = true;
    server->newClientHook = newClient;

    rfbInitServer(server);

    rfbMarkRectAsModified(server, 0, 0, video.getWidth(), video.getHeight());

    server->kbdAddEvent = Input::keyEvent;
    server->ptrAddEvent = Input::pointerEvent;
}

void Server::clientGone(rfbClientPtr cl)
{
    Server *server = cl->screen->screenData;

    if (server->numClients-- == 1)
    {
        server->video.reset();
        rfbMarkRectAsModified(server->server, server->video.getWidth(),
                              server->video.getHeight());
    }
}

enum rfbNewClientAction Server::newClient(rfbClientPtr cl)
{
    Server *server = cl->screen->screenData;

    cl->clientData = &input;
    cl->clientGoneHook = clientGone;

    server->numClients++;

    return RFB_CLIENT_ACCEPT;
}

} // namespace ikvm
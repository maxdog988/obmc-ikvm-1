#pragma once

#include <rfb/rfb.h>
#include "ikvm_args.hpp"
#include "ikvm_input.hpp"
#include "ikvm_video.hpp"

namespace ikvm
{

class Server {
    public:
        Server(const Args& args, Video& v);

        inline const Video& getVideo() const
        {
            return video;
        }

    private:
        static void clientGone(rfbClientPtr cl);
        static enum rfbNewClientAction newClient(rfbClientPtr cl);

        int numClients;
        rfbScreenInfoPtr server;
        Input input;
        Video& video;
};

} // namespace ikvm

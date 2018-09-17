#pragma once

#include <rfb/rfb.h>
#include "ikvm_args.hpp"
#include "ikvm_input.hpp"
#include "ikvm_video.hpp"

namespace ikvm
{

class Server {
    public:
        struct ClientData
        {
            ClientData(int s, Input* i) :
                skipFrame(s),
                input(i)
            {}

            int skipFrame;
            Input* input;
        };

        Server(const Args& args, Input& i, Video& v);

        void resize();
        void run();
        void sendFrame();
        void sendHextile16();

        inline bool wantsFrame() const
        {
            return server->clientHead;
        }

        inline const Video& getVideo() const
        {
            return video;
        }

    private:
        static void clientGone(rfbClientPtr cl);
        static enum rfbNewClientAction newClient(rfbClientPtr cl);

        int numClients;
        long int processTime;
        rfbScreenInfoPtr server;
        Input& input;
        Video& video;
};

} // namespace ikvm

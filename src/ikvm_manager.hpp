#pragma once

#include <condition_variable>
#include <mutex>
#include "ikvm_args.hpp"
#include "ikvm_input.hpp"
#include "ikvm_server.hpp"
#include "ikvm_video.hpp"

namespace ikvm
{

class Manager {
    public:
        Manager(const Args& args);

        void run();

    private:
        static void serverThread(Manager* manager);

        void setServerDone();
        void setVideoDone();
        void waitServer();
        void waitVideo();

        bool continueExecuting;
        bool serverDone;
        bool videoDone;
        Input input;
        Server server;
        Video video;
        std::condition_variable sync;
        std::mutex lock;
};

} // namespace ikvm

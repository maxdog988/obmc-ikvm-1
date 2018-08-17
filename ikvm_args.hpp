#pragma once

#include <string>

namespace ikvm
{

class Args {
    public:
        Args(int argc, char *argv[]);

        inline int getFrameRate() const
        {
            return frameRate;
        }

        inline const std::string& getInputPath() const
        {
            return inputPath;
        }

        inline const std::string& getVideoPath() const
        {
            return videoPath;
        }

    private:
        void printUsage();

        int frameRate;
        std::string inputPath;
        std::string videoPath;
};

} // namespace ikvm

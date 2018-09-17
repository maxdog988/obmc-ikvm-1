#pragma once

#include <string>
#include <vector>
#include "ikvm_input.hpp"

namespace ikvm
{

class Video {
    public:
        Video(const std::string& p, Input& input, int fr = 30);

        void getFrame(bool& needsResize);
        void reset();
        void resize();
        int getClipCount();

        inline char* getData()
        {
            return data.data();
        }

        inline int getFrameRate() const
        {
            return frameRate;
        }

        inline size_t getFrameSize() const
        {
            return frameSize;
        }

        inline size_t getHeight() const
        {
            return height;
        }

        inline size_t getWidth() const
        {
            return width;
        }

        static const int bitsPerSample;
        static const int bytesPerPixel;
        static const int samplesPerPixel;

    private:
        int fd;
        int frameRate;
        size_t frameSize;
        size_t height;
        size_t width;
        std::string path;
        std::vector<char> data;
};

} // namespace ikvm

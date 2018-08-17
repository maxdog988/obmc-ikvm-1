#pragma once

#include <string>
#include <vector>

namespace ikvm
{

class Video {
    public:
        Video(const std::string& p, int fr = 30);

        void reset();

        inline const unsigned char* getData() const
        {
            return data.data();
        }

        inline size_t getHeight() const
        {
            return height;
        }

        inline size_t getWidth() const
        {
            return width;
        }

    private:
        void open();
        void resize(size_t w, size_t h);
        void setFrameRate();

        int frameRate;
        int fd;
        size_t width;
        size_t height;
        std::string path;
        std::vector<unsigned char> data;
};

} // namespace ikvm

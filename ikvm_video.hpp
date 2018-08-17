#pragma once

#include <string>
#include <vector>

namespace ikvm
{

class Video {
    public:
        Video(const std::string& p, int fr = 30);

        inline size_t get_width() const {
            return width;
        }

        inline size_t get_height() const {
            return height;
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

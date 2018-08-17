#pragma once

#include <map>
#include <rfb/rfb.h>
#include <string>

namespace ikvm
{

class Input
{
    public:
        Input(const std::string& p);

        static void keyEvent(rfbBool down, rfbKeySym key, rfbClientPtr cl);
        static void pointerEvent(int buttonMask, int x, int y, rfbClientPtr cl);

        void sendReport();

    private:
        enum
        {
            REPORT_LENGTH = 8
        }

        static const char shiftCtrlMap[];
        static const char metaAltMap[];

        static char keyToMod(rfbKeySym key);
        static char keyToScancode(rfbKeySym key);

        bool sendKeyboard;
        bool sendPointer;
        int fd;
        char keyboardReport[REPORT_LENGTH];
        char pointerReport[REPORT_LENGTH];
        std::string path;
        std::map<int, int> keysDown;
};

} // namespace ikvm
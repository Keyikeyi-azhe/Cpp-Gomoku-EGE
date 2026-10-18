// Dev-C++: enable C++11, and link the EGE library as in your existing project.
// Put the assets folder next to the built executable. Press Esc to exit.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <graphics.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace ege;

int main() {
    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(NULL, exePath, MAX_PATH)) {
        char* slash = std::strrchr(exePath, '\\');
        if (slash) { *slash = '\0'; SetCurrentDirectoryA(exePath); }
    }

    const int frameCount = 90;
    std::vector<PIMAGE> frames(frameCount, NULL);
    setinitmode(INIT_ANIMATION);
    initgraph(1800, 1350);
    setrendermode(RENDER_MANUAL);

    for (int i = 0; i < frameCount; ++i) {
        char name[80];
        std::sprintf(name, "assets/menu/frames/frame_%03d.png", i);
        frames[i] = newimage();
        if (!frames[i] || getimage(frames[i], name) != 0) {
            closegraph();
            for (int j = 0; j <= i; ++j) if (frames[j]) delimage(frames[j]);
            MessageBoxA(NULL, "Cannot load frames. Put the assets folder next to this EXE.", "Gomoku background", MB_OK | MB_ICONERROR);
            return 1;
        }
        if (i == 0) { putimage(0, 0, frames[0]); delay_ms(1); }
    }

    typedef std::chrono::steady_clock Clock;
    const Clock::time_point start = Clock::now();
    while (is_run()) {
        while (kbhit()) {
            if (getkey().key == key_esc) {
                closegraph();
                for (int i = 0; i < frameCount; ++i) delimage(frames[i]);
                return 0;
            }
        }

        const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
        const int index = static_cast<int>((elapsedMs % 3000) * 30 / 1000);

        // A full-size opaque frame overwrites the entire offscreen drawing surface.
        // Add menu labels, hover effects and game UI here, before the presentation.
        putimage(0, 0, frames[index]);
        // present the completed image; do not clear the visible window first.
        delay_fps(60);
    }

    closegraph();
    for (int i = 0; i < frameCount; ++i) delimage(frames[i]);
    return 0;
}

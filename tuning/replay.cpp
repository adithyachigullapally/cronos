// replay <stereo.f32> [sideWindowMs...]: runs the app's detector over a recording and
// prints each tap's time and lag per window setting.
#include "tapdetector.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 1;
    std::vector<float> l, r;
    float fr[2];
    while (fread(fr, 4, 2, f) == 2) {
        l.push_back(fr[0]);
        r.push_back(fr[1]);
    }
    std::vector<float> windows;
    for (int i = 2; i < argc; ++i)
        windows.push_back(float(atof(argv[i])));
    if (windows.empty())
        windows = {5.f};
    for (float w : windows) {
        TapDetector *d = new TapDetector;
        d->cfg.sideWindowMs = w;
        if (getenv("RATIO"))
            d->cfg.onsetRatio = float(atof(getenv("RATIO")));
        size_t at = 0;
        printf("window %.2f ms\n", w);
        d->onTap = [&](const Tap &t) { printf("  %6.2f s  peak %.3f  %3.0f ms  lag %+.2f\n", at / 48000.0, t.peak, t.durationMs, t.lag); };
        for (; at + 120 <= l.size(); at += 120)
            d->process(&l[at], 120, false, &r[at]);
        delete d;
    }
    return 0;
}

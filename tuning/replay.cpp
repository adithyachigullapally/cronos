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
        d->onTap = [&](const Tap &t) {
            printf("  %6.2f s  peak %.3f  %3.0f ms  ~%5.0f Hz  lag %+.2f  thump %+5.1f dB%s\n", at / 48000.0, t.peak, t.durationMs,
                   t.freqHz, t.lag, t.thumpDb, t.counted ? "" : "  not counted");
        };
        if (getenv("THUMP")) // least thump in dB for a sound to count
            d->cfg.minThumpDb = float(atof(getenv("THUMP")));
        if (getenv("RING")) // least zero-crossing frequency in Hz for a sound to count
            d->cfg.minRingHz = float(atof(getenv("RING")));
        if (getenv("GAP")) // pattern gap in ms
            d->cfg.patternGapMs = float(atof(getenv("GAP")));
        if (getenv("MAXMS")) // longest sound that still counts as a tap
            d->cfg.maxTapMs = float(atof(getenv("MAXMS")));
        if (getenv("LAGL") && getenv("LAGR")) { // usual lag of a left and a right tap: others are marked "not counted"
            d->cfg.sideLag[0] = float(atof(getenv("LAGL")));
            d->cfg.sideLag[1] = float(atof(getenv("LAGR")));
        }
        d->onPattern = [&](int n, float lag) { printf("  %6.2f s  pattern of %d  lag %+.2f\n", at / 48000.0, n, lag); };
        for (; at + 120 <= l.size(); at += 120)
            d->process(&l[at], 120, false, &r[at]);
        delete d;
    }
    return 0;
}

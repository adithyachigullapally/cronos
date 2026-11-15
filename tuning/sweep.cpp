// sweep <labelled.f32> <leftFrom> <leftTo> <rightFrom> <rightTo> [other.f32...]
// Runs the app's detector over a recording for a grid of left/right settings. Taps inside
// the two time ranges (seconds) are labelled; everything else, and every tap in the other
// files, is unlabelled noise where only "how often is the lag absurd" can be counted.
#include "../tapdetector.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

struct Rec { std::vector<float> l, r; };

static Rec load(const char *path)
{
    Rec rec;
    FILE *f = fopen(path, "rb");
    float fr[2];
    while (f && fread(fr, 4, 2, f) == 2) {
        rec.l.push_back(fr[0]);
        rec.r.push_back(fr[1]);
    }
    return rec;
}

struct Hit { double t; float lag, peak; };

static std::vector<Hit> run(const Rec &rec, const TapConfig &cfg)
{
    std::vector<Hit> hits;
    TapDetector *d = new TapDetector;
    d->cfg = cfg;
    size_t at = 0;
    d->onTap = [&](const Tap &t) { hits.push_back({at / 48000.0, t.lag, t.peak}); };
    for (; at + 480 <= rec.l.size(); at += 480)
        d->process(&rec.l[at], 480, false, &rec.r[at]);
    delete d;
    return hits;
}

int main(int argc, char **argv)
{
    if (argc < 6)
        return 9;
    const Rec main = load(argv[1]);
    const double lf = atof(argv[2]), lt = atof(argv[3]), rf = atof(argv[4]), rt = atof(argv[5]);
    std::vector<Rec> others;
    for (int i = 6; i < argc; ++i)
        others.push_back(load(argv[i]));

    struct Band { float lo, hi; };
    printf("ratio  band Hz      maxLag win |  left lags        right lags       correct  worst |  unlabelled: n  |lag|>4\n");
    for (float ratio : {4.f, 14.5f})
        for (Band b : {Band{4000, 23000}, Band{1000, 6000}, Band{1000, 4000}, Band{2000, 6000}, Band{1500, 5000}})
            for (float maxLag : {0.5f, 0.15f, 0.105f})
                for (float win : {3.f, 5.f, 10.f}) {
                    TapConfig cfg;
                    cfg.onsetRatio = ratio;
                    cfg.sideLowHz = b.lo;
                    cfg.sideHighHz = b.hi;
                    cfg.sideMaxLagMs = maxLag;
                    cfg.sideWindowMs = win;
                    float lmin = 99, lmax = -99, rmin = 99, rmax = -99;
                    int nl = 0, nr = 0, ok = 0, un = 0, wild = 0;
                    for (const Hit &h : run(main, cfg)) {
                        if (h.t >= lf && h.t <= lt) {
                            ++nl;
                            ok += h.lag > 0;
                            lmin = std::min(lmin, h.lag);
                            lmax = std::max(lmax, h.lag);
                        } else if (h.t >= rf && h.t <= rt) {
                            ++nr;
                            ok += h.lag <= 0;
                            rmin = std::min(rmin, h.lag);
                            rmax = std::max(rmax, h.lag);
                        } else {
                            ++un;
                            wild += std::fabs(h.lag) > 4;
                        }
                    }
                    for (const Rec &o : others)
                        for (const Hit &h : run(o, cfg)) {
                            ++un;
                            wild += std::fabs(h.lag) > 4;
                        }
                    // worst = how close the nearest-to-wrong labelled tap came to the dividing line at 0
                    printf("%5.1f  %5.0f-%-5.0f  %5.2f  %3.0f | %+6.2f..%+6.2f  %+6.2f..%+6.2f   %2d/%-2d  %+5.2f | %14d  %5d\n", ratio, b.lo, b.hi, maxLag,
                           win, lmin, lmax, rmin, rmax, ok, nl + nr, std::min(lmin, -rmax), un, wild);
                }
    return 0;
}

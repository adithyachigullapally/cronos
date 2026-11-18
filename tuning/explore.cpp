// explore <stereo.f32>: per tap, candidate left/right cues. Taps before the longest
// pause are labelled L, the rest R; each cue is scored by how well one threshold splits them.
#include "tapdetector.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static std::vector<float> highpass(const std::vector<float> &x, float hz)
{
    const float w = 2.f * 3.14159265f * hz / 48000.f;
    const float cosw = std::cos(w), alpha = std::sin(w) / 1.41421356f, norm = 1.f / (1.f + alpha);
    const float b0 = (1.f + cosw) / 2.f * norm, b1 = -(1.f + cosw) * norm, a1 = -2.f * cosw * norm, a2 = (1.f - alpha) * norm;
    std::vector<float> y(x.size());
    float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (size_t i = 0; i < x.size(); ++i) {
        y[i] = b0 * (x[i] + x2) + b1 * x1 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x[i]; y2 = y1; y1 = y[i];
    }
    return y;
}

static double rms(const std::vector<float> &x, long from, long n)
{
    double s = 0;
    for (long i = from; i < from + n; ++i)
        s += double(x[i]) * x[i];
    return std::sqrt(s / n);
}

// lag of b relative to a (positive: b later), sub-sample, over [from, from+n)
static float xcorr(const std::vector<float> &a, const std::vector<float> &b, long from, long n, int maxLag)
{
    std::vector<double> c(2 * maxLag + 1, 0.0);
    int best = 0;
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        for (long t = from; t < from + n; ++t)
            c[lag + maxLag] += double(a[t]) * b[t + lag];
        if (c[lag + maxLag] > c[best])
            best = lag + maxLag;
    }
    float lag = float(best - maxLag);
    if (best > 0 && best < 2 * maxLag) {
        const double bend = c[best - 1] - 2 * c[best] + c[best + 1];
        if (bend < 0)
            lag += float(0.5 * (c[best - 1] - c[best + 1]) / bend);
    }
    return lag;
}

static long firstAbove(const std::vector<float> &x, long from, long n, float frac)
{
    float peak = 0;
    for (long i = from; i < from + n; ++i)
        peak = std::max(peak, std::fabs(x[i]));
    for (long i = from; i < from + n; ++i)
        if (std::fabs(x[i]) >= peak * frac)
            return i;
    return from;
}

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
    const std::vector<float> l4 = highpass(l, 4000), r4 = highpass(r, 4000);
    const std::vector<float> l1 = highpass(l, 1000), r1 = highpass(r, 1000);
    const std::vector<float> lb = highpass(l, 300), rb = highpass(r, 300);
    std::vector<float> avg(l4.size());
    for (size_t i = 0; i < avg.size(); ++i)
        avg[i] = 0.5f * (l4[i] + r4[i]);

    // tap positions from the app's detector (it reports at the tap's end)
    std::vector<long> ends;
    std::vector<float> durs, appLag;
    TapDetector *d = new TapDetector;
    size_t at = 0;
    d->onTap = [&](const Tap &t) { ends.push_back(long(at)); durs.push_back(t.durationMs); appLag.push_back(t.lag); };
    for (; at + 120 <= l.size(); at += 120)
        d->process(&l[at], 120, false, &r[at]);

    struct Cue { std::string name; std::vector<float> v; };
    std::vector<Cue> cues = {{"appLag"}, {"x4k_1ms"}, {"x4k_2ms"}, {"x4k_5ms"}, {"x1k_2ms"}, {"x1k_5ms"}, {"xbb_5ms"}, {"xbb_20ms"},
                             {"first25"}, {"first50"}, {"ild4k_3"}, {"ild4k_20"}, {"ild1k_20"}, {"ildbb_20"}, {"ildbb_60"}};
    std::vector<long> onsets;
    for (size_t k = 0; k < ends.size(); ++k) {
        const long rough = ends[k] - long(std::max(durs[k], 20.f) * 48) - 240; // a tap is reported 20 ms after it began, or when it ends if later
        if (rough < 480 || rough + 4800 > long(l.size()))
            continue;
        const long on = firstAbove(avg, rough, 480 + long(durs[k] * 48), 0.2f);
        onsets.push_back(on);
        const long s = on - 24;
        size_t c = 0;
        cues[c++].v.push_back(appLag[k]);
        cues[c++].v.push_back(xcorr(l4, r4, s, 48, 24));
        cues[c++].v.push_back(xcorr(l4, r4, s, 96, 24));
        cues[c++].v.push_back(xcorr(l4, r4, s, 240, 24));
        cues[c++].v.push_back(xcorr(l1, r1, s, 96, 24));
        cues[c++].v.push_back(xcorr(l1, r1, s, 240, 24));
        cues[c++].v.push_back(xcorr(lb, rb, s, 240, 48));
        cues[c++].v.push_back(xcorr(lb, rb, s, 960, 48));
        cues[c++].v.push_back(float(firstAbove(r4, s - 24, 480, 0.25f) - firstAbove(l4, s - 24, 480, 0.25f)));
        cues[c++].v.push_back(float(firstAbove(r4, s - 24, 480, 0.5f) - firstAbove(l4, s - 24, 480, 0.5f)));
        cues[c++].v.push_back(float(20 * std::log10(rms(l4, s, 144) / rms(r4, s, 144))));
        cues[c++].v.push_back(float(20 * std::log10(rms(l4, s, 960) / rms(r4, s, 960))));
        cues[c++].v.push_back(float(20 * std::log10(rms(l1, s, 960) / rms(r1, s, 960))));
        cues[c++].v.push_back(float(20 * std::log10(rms(lb, s, 960) / rms(rb, s, 960))));
        cues[c++].v.push_back(float(20 * std::log10(rms(lb, s, 2880) / rms(rb, s, 2880))));
    }
    const size_t n = onsets.size();
    if (n < 4)
        return printf("only %zu taps found\n", n), 2;
    size_t split = 1;
    for (size_t k = 2; k < n; ++k)
        if (onsets[k] - onsets[k - 1] > onsets[split] - onsets[split - 1])
            split = k;
    printf("%zu taps: %zu left, %zu right (pause %.1f s)\n\n   time side", n, split, n - split,
           (onsets[split] - onsets[split - 1]) / 48000.0);
    for (const Cue &c : cues)
        printf(" %8s", c.name.c_str());
    for (size_t k = 0; k < n; ++k) {
        printf("\n%7.2f    %c", onsets[k] / 48000.0, k < split ? 'L' : 'R');
        for (const Cue &c : cues)
            printf(" %+8.2f", c.v[k]);
    }
    printf("\n\nbest single-threshold split (correct of %zu):\n", n);
    for (const Cue &c : cues) {
        int best = 0;
        float bestThr = 0;
        std::vector<float> sorted = c.v;
        std::sort(sorted.begin(), sorted.end());
        for (size_t i = 0; i + 1 < n; ++i) {
            const float thr = 0.5f * (sorted[i] + sorted[i + 1]);
            int ok = 0;
            for (size_t k = 0; k < n; ++k)
                ok += (c.v[k] > thr) == (k < split);
            ok = std::max(ok, int(n) - ok);
            if (ok > best) { best = ok; bestThr = thr; }
        }
        std::vector<float> a(c.v.begin(), c.v.begin() + split), b(c.v.begin() + split, c.v.end());
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        printf("  %8s  %2d   threshold %+7.2f   L median %+7.2f [%+.2f..%+.2f]   R median %+7.2f [%+.2f..%+.2f]\n", c.name.c_str(), best,
               bestThr, a[a.size() / 2], a.front(), a.back(), b[b.size() / 2], b.front(), b.back());
    }
    return 0;
}

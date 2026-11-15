// Offline check of TapDetector on synthetic audio. Run: ctest, or detector_test.exe
#include "tapdetector.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

static const int SR = 48000;
static unsigned seed = 1;
static float noise() // deterministic white noise in [-1, 1]
{
    seed = seed * 1664525u + 1013904223u;
    return (seed >> 8) / 8388608.f - 1.f;
}

static std::vector<float> room(float seconds)
{
    std::vector<float> s(size_t(seconds * SR));
    for (float &v : s)
        v = 0.001f * noise();
    return s;
}

static void addTap(std::vector<float> &s, float atSec, float amp = 0.3f) // noise burst, 5 ms decay
{
    for (int i = 0; i < SR / 20; ++i)
        s[size_t(atSec * SR) + i] += amp * noise() * std::exp(-i / (0.005f * SR));
}

struct Result {
    int taps = 0;
    std::vector<int> patterns;
    float lag = 0; // of the last pattern
};

// suppressFrom: sample index at which keyboard/mouse input starts being reported.
// s2: the second mic of a two-channel array.
static Result run(const std::vector<float> &s, size_t suppressFrom = SIZE_MAX, const std::vector<float> *s2 = nullptr)
{
    Result r;
    TapDetector d;
    d.onTap = [&](const Tap &) { ++r.taps; };
    d.onPattern = [&](int n, float lag) {
        r.patterns.push_back(n);
        r.lag = lag;
    };
    for (size_t i = 0; i < s.size(); i += 480) // 10 ms packets, like WASAPI delivers
        d.process(&s[i], std::min<size_t>(480, s.size() - i), i >= suppressFrom, s2 ? &(*s2)[i] : nullptr);
    return r;
}

// What a second mic hears when the sound reaches it `samples` later, plus its own noise.
static std::vector<float> delayed(const std::vector<float> &s, int samples)
{
    std::vector<float> d = room(float(s.size()) / SR);
    for (size_t i = samples; i < s.size(); ++i)
        d[i] += s[i - samples];
    return d;
}

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond); \
            std::exit(1); \
        } \
    } while (0)

int main()
{
    { // quiet room: nothing
        Result r = run(room(2));
        CHECK(r.taps == 0 && r.patterns.empty());
    }
    { // single tap
        auto s = room(2);
        addTap(s, 0.5f);
        Result r = run(s);
        CHECK(r.taps == 1);
        CHECK(r.patterns == std::vector<int>{1});
    }
    { // double tap, 200 ms apart
        auto s = room(2);
        addTap(s, 0.5f);
        addTap(s, 0.7f);
        CHECK(run(s).patterns == std::vector<int>{2});
    }
    { // triple tap
        auto s = room(2);
        addTap(s, 0.5f);
        addTap(s, 0.7f);
        addTap(s, 0.9f);
        CHECK(run(s).patterns == std::vector<int>{3});
    }
    { // two taps a second apart are two single taps, not a double
        auto s = room(3);
        addTap(s, 0.5f);
        addTap(s, 1.5f);
        CHECK((run(s).patterns == std::vector<int>{1, 1}));
    }
    { // sustained loud broadband sound (hiss, applause, a scrape) is not a tap
        auto s = room(3);
        for (int i = SR / 2; i < 2 * SR; ++i)
            s[i] += 0.3f * noise();
        Result r = run(s);
        CHECK(r.taps == 0 && r.patterns.empty());
    }
    { // a soft second tap right after a loud one still counts
        auto s = room(2);
        addTap(s, 0.5f);
        addTap(s, 0.7f, 0.03f);
        CHECK(run(s).patterns == std::vector<int>{2});
    }
    { // taps while the user is typing are ignored
        auto s = room(2);
        addTap(s, 0.5f);
        addTap(s, 0.7f);
        Result r = run(s, 0);
        CHECK(r.taps == 0 && r.patterns.empty());
    }
    { // a keystroke is heard before Windows reports it: the pattern is discarded, not fired
        auto s = room(2);
        addTap(s, 0.5f);
        Result r = run(s, size_t(0.56f * SR)); // key event arrives 60 ms after the sound
        CHECK(r.patterns == std::vector<int>{0});
    }
    { // two mics: the lag says which one the tap reached first, and by how much
        auto near = room(2);
        addTap(near, 0.5f);
        addTap(near, 0.7f);
        const auto far = delayed(near, 4);
        Result r = run(near, SIZE_MAX, &far);
        CHECK(r.patterns == std::vector<int>{2});
        CHECK(std::fabs(r.lag - 4) < 0.5f);
        r = run(far, SIZE_MAX, &near);
        CHECK(r.patterns == std::vector<int>{2});
        CHECK(std::fabs(r.lag + 4) < 0.5f);
    }
    std::printf("detector_test: all passed\n");
    return 0;
}

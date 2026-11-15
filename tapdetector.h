// Tap detection DSP. Pure C++ (no Windows/Qt) so detector_test.cpp can run it offline.
#ifndef TAPDETECTOR_H
#define TAPDETECTOR_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// Calibration knobs. Mics and chassis differ a lot between laptops: tune these
// against the per-tap numbers the app prints in its log.
struct TapConfig {
    float sampleRate = 48000.f;
    // Measured on a Realtek mic array: a chassis tap is a broadband click, while
    // fan/room noise sits low. Cutting below 4 kHz took taps from 4-9x the noise
    // floor to 7-32x. A mic with little treble may need this lower.
    float highpassHz = 4000.f;
    float onsetRatio = 4.f;     // block RMS must exceed noise floor * this (UI "sensitivity")
    float minRms = 0.002f;      // absolute floor, so a silent room can't trigger on nothing
    float maxTapMs = 90.f;      // louder for longer than this = speech/music/scrape, not a tap
    float refractoryMs = 70.f;  // min spacing between tap onsets (covers chassis ringing)
    float patternGapMs = 400.f; // taps closer than this belong to one pattern
    // Left/right, two-channel mics only: the channels are cross-correlated over
    // the first sideWindowMs of a tap, and the mic that heard it first is the
    // nearer one. Measured on recorded taps (tuning/sweep.cpp): left taps lag by
    // +2.1..+2.9 samples, right taps by -2.2..-1.8. The comparison must stay
    // below 6 kHz: soft taps ring at ~15 kHz, and matching on that slips by whole
    // cycles (lags of +-5, +-23) or sticks at 0, which put 7 of 33 soft taps on
    // the wrong side. In 2-6 kHz all 33 soft and all 20 hard taps come out right.
    // ponytail: band and range are tuned to one laptop; one with mics further
    // apart needs a larger sideMaxLagMs, re-run the sweep on a recording from it.
    float sideWindowMs = 5.f;
    float sideMaxLagMs = 0.15f; // search range; real lags here stay within 3 samples (0.06 ms)
    float sideLowHz = 2000.f;   // band the two channels are compared in
    float sideHighHz = 6000.f;
};

struct Tap {
    float peak;       // 0..1 after high-pass
    float durationMs;
    float freqHz;     // zero-crossing estimate of dominant frequency (cheap spectral-centroid stand-in)
    float lag;        // samples by which channel 2 heard the tap after channel 1 (negative: before). 0 for mono.
};

class TapDetector
{
public:
    TapConfig cfg;
    std::function<void(const Tap &)> onTap; // every accepted tap
    // Tap count and the taps' mean lag, once the pattern has closed. Count 0 = a
    // pending pattern was thrown away because the keyboard/mouse was used before it closed.
    std::function<void(int, float)> onPattern;

    // Float samples; x2 is the second channel of a two-mic array, or null for
    // mono. suppress = user is typing/mousing right now. Key and
    // click sounds look like taps, so taps that complete while it is set are
    // dropped, and so is a pattern still pending when it becomes set: a finger
    // hitting a key is heard a few ms before Windows reports the keystroke.
    void process(const float *x, size_t n, bool suppress = false, const float *x2 = nullptr)
    {
        const Coef hp = coef(cfg.highpassHz, true), sideLo = coef(cfg.sideLowHz, true), sideHi = coef(cfg.sideHighHz, false);
        const int blockLen = std::max(1, int(cfg.sampleRate * BlockMs / 1000.f));
        stereo_ = x2 != nullptr;
        for (size_t i = 0; i < n; ++i) {
            float y = hp_[0].run(x[i], hp);
            if (x2) { // detect on the average of the two mics, keep each (in the side band) for the lag estimate
                y = 0.5f * (y + hp_[1].run(x2[i], hp));
                ring_[0][pos_ & RingMask] = sideHi_[0].run(sideLo_[0].run(x[i], sideLo), sideHi);
                ring_[1][pos_ & RingMask] = sideHi_[1].run(sideLo_[1].run(x2[i], sideLo), sideHi);
            }
            ++pos_;
            sumSq_ += y * y;
            blockPeak_ = std::max(blockPeak_, std::fabs(y));
            if ((y >= 0) != positive_) {
                positive_ = !positive_;
                ++zeroCrossings_;
            }
            if (++blockN_ == blockLen)
                endBlock(suppress);
        }
    }

private:
    static constexpr float BlockMs = 2.5f; // taps are over in a few ms; longer blocks dilute them
    static constexpr float WarmupMs = 300.f;
    static constexpr int64_t RingMask = 8191; // 170 ms at 48 kHz: a whole tap (maxTapMs) and then some

    // 2nd-order Butterworth high- or low-pass (RBJ cookbook biquad); b2 == b0 for both
    struct Coef { float b0, b1, a1, a2; };
    Coef coef(float hz, bool high) const
    {
        const float w = 2.f * 3.14159265f * hz / cfg.sampleRate;
        const float cosw = std::cos(w), alpha = std::sin(w) / 1.41421356f, norm = 1.f / (1.f + alpha);
        const float b0 = (high ? 1.f + cosw : 1.f - cosw) / 2.f * norm;
        return {b0, (high ? -2.f : 2.f) * b0, -2.f * cosw * norm, (1.f - alpha) * norm};
    }

    struct Biquad {
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        float run(float x, const Coef &c)
        {
            const float y = c.b0 * (x + x2) + c.b1 * x1 - c.a1 * y1 - c.a2 * y2;
            x2 = x1;
            x1 = x;
            y2 = y1;
            y1 = y;
            return y;
        }
    };

    // Lag (in samples, fractional) at which channel 2 best matches channel 1 around the tap's onset.
    float onsetLag() const
    {
        if (!stereo_)
            return 0;
        const int maxLag = std::max(1, int(cfg.sampleRate * cfg.sideMaxLagMs / 1000.f));
        // Start one lag-range early (the click may begin just before the block that
        // tripped the threshold) and stop short of samples that haven't arrived yet.
        const int64_t from = evPos_ - 2 * maxLag;
        const int64_t to = std::min(evPos_ + int64_t(cfg.sampleRate * cfg.sideWindowMs / 1000.f), pos_ - maxLag);
        std::vector<double> c(2 * maxLag + 1, 0.0);
        int best = maxLag;
        for (int lag = -maxLag; lag <= maxLag; ++lag) {
            double &sum = c[lag + maxLag];
            for (int64_t t = from; t < to; ++t)
                sum += double(ring_[0][t & RingMask]) * ring_[1][(t + lag) & RingMask];
            if (sum > c[best])
                best = lag + maxLag;
        }
        float lag = float(best - maxLag);
        if (best > 0 && best < 2 * maxLag) { // parabola through the peak and its neighbours: sub-sample precision
            const double bend = c[best - 1] - 2 * c[best] + c[best + 1];
            if (bend < 0)
                lag += float(0.5 * (c[best - 1] - c[best + 1]) / bend);
        }
        return lag;
    }

    void endBlock(bool suppress)
    {
        const float rms = std::sqrt(sumSq_ / blockN_);
        now_ += BlockMs;

        if (!inEvent_) {
            const float threshold = std::max(floor_ * cfg.onsetRatio, cfg.minRms);
            // The first WarmupMs only teach the noise floor; a stream starting in
            // a noisy room would otherwise open with a false tap.
            if (rms > threshold && now_ >= WarmupMs && now_ - lastOnset_ >= cfg.refractoryMs) {
                inEvent_ = true;
                evStart_ = now_ - BlockMs;
                evPos_ = pos_ - blockN_;
                evBlocks_ = 0;
                evPeak_ = evPeakRms_ = 0;
                evZc_ = 0;
            } else {
                // Noise floor: falls fast (50 ms) but rises slowly (500 ms), so the
                // ringing of one tap can't lift it above a softer second tap.
                floor_ += (rms < floor_ || now_ < WarmupMs ? 0.05f : 0.005f) * (rms - floor_);
            }
        }
        if (inEvent_) {
            ++evBlocks_;
            evPeak_ = std::max(evPeak_, blockPeak_);
            evPeakRms_ = std::max(evPeakRms_, rms);
            evZc_ += zeroCrossings_;
            const float dur = evBlocks_ * BlockMs;
            // Over once it has died away, or (for a tap barely above the noise) sunk back into it.
            if (rms < std::max(evPeakRms_ * 0.25f, floor_ * 2.f)) {
                inEvent_ = false;
                lastOnset_ = evStart_;
                if (!suppress) {
                    const float lag = onsetLag();
                    ++count_;
                    lagSum_ += lag;
                    lastTap_ = now_;
                    if (onTap)
                        onTap({evPeak_, dur, evZc_ / (2.f * dur / 1000.f), lag});
                }
            } else if (dur > cfg.maxTapMs) { // sustained sound, not a tap
                inEvent_ = false;
                lastOnset_ = now_;
                floor_ = rms; // adopt it as the new floor so it can't retrigger
            }
        }
        if (count_ && suppress) {
            count_ = 0;
            lagSum_ = 0;
            if (onPattern)
                onPattern(0, 0);
        }
        if (count_ && !inEvent_ && now_ - lastTap_ > cfg.patternGapMs) {
            if (onPattern)
                onPattern(count_, lagSum_ / count_);
            count_ = 0;
            lagSum_ = 0;
        }
        sumSq_ = blockPeak_ = 0;
        blockN_ = zeroCrossings_ = 0;
    }

    Biquad hp_[2], sideLo_[2], sideHi_[2];
    float ring_[2][RingMask + 1] = {}; // recent side-band samples of each channel
    int64_t pos_ = 0, evPos_ = 0;      // samples processed; where the current tap began
    bool stereo_ = false;
    float lagSum_ = 0;
    float sumSq_ = 0, blockPeak_ = 0;
    int blockN_ = 0, zeroCrossings_ = 0;
    bool positive_ = true;

    double now_ = 0, lastOnset_ = -1e9, lastTap_ = 0, evStart_ = 0; // ms of audio processed
    float floor_ = 0;
    bool inEvent_ = false;
    int evBlocks_ = 0, evZc_ = 0, count_ = 0;
    float evPeak_ = 0, evPeakRms_ = 0;
};

#endif // TAPDETECTOR_H

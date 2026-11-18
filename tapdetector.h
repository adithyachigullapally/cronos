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
    // Louder for longer than this = speech, a cough, a scrape, something put down:
    // not a tap. Recorded taps last 5-25 ms; speech that got through at 40 lasted 38-42.
    float maxTapMs = 30.f;
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
    // Where taps come from: the lag a tap on each side measures (the app learns the
    // two with "Calibrate..."). A sound further than sideTolerance from both did not
    // come from either spot and is not counted. Both equal = not known, count
    // everything. On the recordings taps stay within 0.65 of their side's usual lag,
    // while hands moving and things nearby measured -0.1, -1.0, -3.9.
    // ponytail: a sound arriving from the same direction as a palm rest still passes;
    // telling those apart needs another cue (spectrum, decay), measured on recordings.
    float sideLag[2] = {0, 0};
    float sideTolerance = 0.75f;
    // A tap shakes the chassis, so its click comes with a thump below 300 Hz. A sound
    // that only arrives through the air (a hand slapped on a shoulder, a clap) is the
    // click without the thump. A sound counts only if its thump (Tap::thumpDb) is at
    // least this. Measured on recordings (tuning/replay.cpp): 69 taps -8.9 dB or more,
    // five slaps on a shoulder -16 to -30, which is why the app uses -14.
    // -200 = don't check, and it stays that until "Calibrate..." has run.
    // ponytail: a soft sound passes whatever it is, room rumble stands in for the
    // missing thump. A fingernail tap, or any tap on another laptop, is unmeasured:
    // the app's log prints every sound's thump to check against.
    float minThumpDb = -200.f;
    // A tap makes the chassis ring, so above the high-pass its click crosses zero at
    // 13-16 kHz (Tap::freqHz; 82 recorded taps, 13050 Hz the lowest). Speech and
    // cracked knuckles reach the mic through the air without that ring: most measured
    // 3-9 kHz. A sound counts only if its freqHz is at least this. 0 = don't check,
    // and it stays that until "Calibrate..." has run: another chassis rings elsewhere.
    float minRingHz = 0.f;
};

struct Tap {
    float peak;       // 0..1 after high-pass
    float durationMs;
    float freqHz;     // zero-crossing estimate of dominant frequency (cheap spectral-centroid stand-in)
    float lag;        // samples by which channel 2 heard the tap after channel 1 (negative: before). 0 for mono.
    float thumpDb;    // energy at 80-300 Hz around the click, relative to the click's own energy above the high-pass
    bool counted;     // false: heard, but not a tap on a palm rest (see TapConfig::sideLag, minThumpDb, minRingHz), so left out of the pattern
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
        const Coef thumpLo = coef(ThumpLowHz, true), thumpHi = coef(ThumpHighHz, false);
        const int blockLen = std::max(1, int(cfg.sampleRate * BlockMs / 1000.f));
        stereo_ = x2 != nullptr;
        for (size_t i = 0; i < n; ++i) {
            float y = hp_[0].run(x[i], hp), raw = x[i];
            if (x2) { // detect on the average of the two mics, keep each (in the side band) for the lag estimate
                y = 0.5f * (y + hp_[1].run(x2[i], hp));
                raw = 0.5f * (raw + x2[i]);
                ring_[0][pos_ & RingMask] = sideHi_[0].run(sideLo_[0].run(x[i], sideLo), sideHi);
                ring_[1][pos_ & RingMask] = sideHi_[1].run(sideLo_[1].run(x2[i], sideLo), sideHi);
            }
            // Each filter twice: the rest of a loud click must not leak into the thump band.
            const float low = thump_[3].run(thump_[2].run(thump_[1].run(thump_[0].run(raw, thumpLo), thumpLo), thumpHi), thumpHi);
            sumSqLow_ += low * low;
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
    // The thump (see TapConfig::minThumpDb) is listened for from 5 ms before the click
    // to 20 ms after its start. Measured on recordings: by the time the click has died
    // (5-15 ms) the thump has not arrived yet, and past 20 ms room rumble only dilutes it.
    static constexpr float ThumpLowHz = 80.f, ThumpHighHz = 300.f;
    static constexpr int ThumpBlocks = 8;

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
            if (rms > threshold && now_ >= WarmupMs && now_ - lastOnset_ >= cfg.refractoryMs && !pending_) {
                inEvent_ = true;
                evStart_ = now_ - BlockMs;
                evPos_ = pos_ - blockN_;
                evBlocks_ = 0;
                evPeak_ = evPeakRms_ = evEnergy_ = 0;
                evZc_ = 0;
                evLow_ = lowBefore_[0] + lowBefore_[1];
                lowBlocks_ = 0;
            } else {
                // Noise floor: falls fast (50 ms) but rises slowly (500 ms), so the
                // ringing of one tap can't lift it above a softer second tap.
                floor_ += (rms < floor_ || now_ < WarmupMs ? 0.05f : 0.005f) * (rms - floor_);
            }
        }
        if (lowBlocks_ < ThumpBlocks) { // listening for the thump, whether or not the click is still going
            evLow_ += sumSqLow_;
            ++lowBlocks_;
        }
        if (inEvent_) {
            ++evBlocks_;
            evPeak_ = std::max(evPeak_, blockPeak_);
            evPeakRms_ = std::max(evPeakRms_, rms);
            evEnergy_ += sumSq_;
            evZc_ += zeroCrossings_;
            // Over once it has died away, or (for a tap barely above the noise) sunk back into it.
            if (rms < std::max(evPeakRms_ * 0.25f, floor_ * 2.f)) {
                inEvent_ = false;
                lastOnset_ = evStart_;
                pending_ = true; // whether it counts is decided once the thump has had its time
                evEnd_ = now_;
                evSuppress_ = suppress;
                evLag_ = onsetLag();
            } else if (evBlocks_ * BlockMs > cfg.maxTapMs) { // sustained sound, not a tap
                inEvent_ = false;
                lastOnset_ = now_;
                floor_ = rms; // adopt it as the new floor so it can't retrigger
            }
        }
        if (pending_ && lowBlocks_ >= ThumpBlocks) {
            pending_ = false;
            if (!evSuppress_ && !suppress) {
                const float dur = evBlocks_ * BlockMs;
                const float thump = 10.f * std::log10((evLow_ + 1e-12f) / (evEnergy_ + 1e-12f));
                const float hz = evZc_ / (2.f * dur / 1000.f);
                const bool counted = thump >= cfg.minThumpDb && hz >= cfg.minRingHz
                    && (!stereo_ || cfg.sideLag[0] == cfg.sideLag[1]
                        || std::fabs(evLag_ - cfg.sideLag[0]) <= cfg.sideTolerance
                        || std::fabs(evLag_ - cfg.sideLag[1]) <= cfg.sideTolerance);
                if (counted) {
                    ++count_;
                    lagSum_ += evLag_;
                    lastTap_ = evEnd_;
                }
                if (onTap)
                    onTap({evPeak_, dur, hz, evLag_, thump, counted});
            }
        }
        if (count_ && suppress) {
            count_ = 0;
            lagSum_ = 0;
            if (onPattern)
                onPattern(0, 0);
        }
        if (count_ && !inEvent_ && !pending_ && now_ - lastTap_ > cfg.patternGapMs) {
            if (onPattern)
                onPattern(count_, lagSum_ / count_);
            count_ = 0;
            lagSum_ = 0;
        }
        lowBefore_[0] = lowBefore_[1];
        lowBefore_[1] = sumSqLow_;
        sumSq_ = blockPeak_ = sumSqLow_ = 0;
        blockN_ = zeroCrossings_ = 0;
    }

    Biquad hp_[2], sideLo_[2], sideHi_[2], thump_[4];
    float ring_[2][RingMask + 1] = {}; // recent side-band samples of each channel
    int64_t pos_ = 0, evPos_ = 0;      // samples processed; where the current tap began
    bool stereo_ = false;
    float lagSum_ = 0;
    float sumSq_ = 0, blockPeak_ = 0, sumSqLow_ = 0;
    float lowBefore_[2] = {0, 0}; // thump-band energy of the two blocks before this one
    int blockN_ = 0, zeroCrossings_ = 0;
    bool positive_ = true;

    double now_ = 0, lastOnset_ = -1e9, lastTap_ = 0, evStart_ = 0, evEnd_ = 0; // ms of audio processed
    float floor_ = 0;
    bool inEvent_ = false;
    bool pending_ = false, evSuppress_ = false; // the click is over, the thump is still being listened for
    int evBlocks_ = 0, evZc_ = 0, count_ = 0, lowBlocks_ = ThumpBlocks;
    float evPeak_ = 0, evPeakRms_ = 0, evEnergy_ = 0, evLow_ = 0, evLag_ = 0;
};

#endif // TAPDETECTOR_H

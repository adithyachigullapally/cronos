# Tap Detection App

## Overview
A Windows desktop application that uses the laptop's built-in microphone to detect and classify physical taps on the device chassis (e.g., lid, palm rest, deck) and maps recognized tap patterns to configurable user actions (shortcuts, macros, media controls, etc.).

## Status
Working MVP (Nov 2026): capture, detection, single/double/triple tap patterns, a left and a right side with
their own actions, settings, tray.

| File | Role |
|---|---|
| `tapengine.cpp/.h` | WASAPI capture thread (shared mode, raw stream when the driver allows it, mic picker, both channels of a mic array) |
| `tapdetector.h` | High-pass, adaptive noise floor, onset + duration check, pattern grouping, left/right lag. No Windows/Qt deps. Tuning knobs are in `TapConfig`. |
| `detector_test.cpp` | Detector check on synthetic audio (`ctest`) |
| `mainwindow.cpp/.h` | Window, tray, action mapping (media keys, open an app, shortcut, command), calibration, settings in `%APPDATA%\tap-tap\tap-tap.ini` |
| `widgets.h` | Light/dark colours and the custom-painted parts: laptop drawing, side tiles, popover, switch |
| `ui_test.cpp` | Popover open/close behaviour, driven without showing the window (`ctest`). `ui_test <folder>` also saves pictures of the window (the README images in `docs/images`). |
| `tuning/` | Recorder, replay and sweep tools for tuning the detector against recorded taps |

Left/right: the laptop's mic array has two channels. Each tap's first 5 ms are cross-correlated between them
(band-passed to 2-6 kHz); the lag (in samples, printed per tap in the activity log) says which mic heard it
first. Measured on real taps on this laptop: left taps lag +2.1..+2.9 samples, right taps -2.3..-1.8, so the
default dividing line of 0 works without calibrating. "Calibrate..." (five taps per side) is there for other
laptops. Comparing above 4 kHz was tried first and failed on soft taps: they are dominated by a ~15 kHz ring,
and matching on that slips by whole cycles. See `tuning/sweep.cpp` for how the band was chosen.

Not built: tap intensity classes, ML classifier, tap-and-hold (a held finger makes no sound).
Tuned against a 20 s recording of real double/triple taps on the palm rest (Realtek mic array, raw stream):
taps are broadband clicks 15-20 ms long, about 200 ms apart, on top of loud low-frequency noise. High-passing
at 4 kHz is what makes them detectable; at 150 Hz most were lost in the noise. All 4 doubles and 4 triples in
that recording are recognised at onset ratios 3-5 (default 4), with no false triggers.

Typing and mouse clicks are rejected by ignoring taps within 300 ms after any keyboard/mouse input, and by
discarding a pending pattern if input arrives before it closes (a keystroke is heard before Windows reports it).
Other sharp noises (clap, cup on the desk) still count as taps; single tap is unmapped by default for that reason.

## Core Concept
- Continuously capture audio from the built-in microphone.
- Detect tap events from the audio signal (transient/impulse detection).
- Classify taps by characteristics such as location, intensity, or pattern (e.g., single tap, double tap, tap sequence).
- Trigger a user-configured action based on the classified tap.

## Technical Approach
- **Audio Capture:** WASAPI (Windows Audio Session API) in C++ for low-latency access to the microphone input stream.
- **Language/Stack:** C++ (native Windows app).
- **Platform:** Windows.

## Suggested Architecture (for reference as you build)
1. **Audio Capture Layer**
   - WASAPI shared or exclusive mode capture loop.
   - Ring buffer to hold recent audio frames for analysis.
2. **Signal Processing / Detection Layer**
   - Preprocessing: noise floor estimation, high-pass filtering to remove low-frequency rumble.
   - Onset/transient detection to identify candidate "tap" events (e.g., energy spikes, spectral flux).
   - Feature extraction per event: peak amplitude, duration, spectral shape/centroid — useful for distinguishing tap type/location.
3. **Classification Layer**
   - Simple heuristic thresholds initially (amplitude/duration bands), or
   - Lightweight ML classifier (e.g., small decision tree/SVM) trained on labeled tap samples if heuristics prove insufficient.
   - Pattern recognition for multi-tap sequences (double tap, triple tap, tap-and-hold) via timing windows.
4. **Action Mapping Layer**
   - Configurable mapping: tap pattern → action (keystroke injection, app launch, media control, custom script).
   - Config stored in a simple file (JSON/INI) or a settings UI.
5. **UI/Tray Component (optional for MVP)**
   - System tray icon for status, enable/disable toggle, and access to configuration.

## Open Design Questions to Resolve
- How to reliably distinguish an intentional tap from ambient noise/typing/clicks.
- Whether tap "location" (e.g., lid vs. deck) is actually distinguishable via a single built-in mic, or if this needs multiple mics/accelerometer data instead.
- Latency budget — how fast a tap needs to be recognized and acted on to feel responsive.
- False positive tolerance — a tap-based interface needs a very low false trigger rate to be usable.

## Next Steps (typical for this stage)
- Get a stable WASAPI capture loop running and log raw amplitude data to validate signal quality.
- Collect sample recordings of real taps vs. background noise to inform detection thresholds.
- Build a minimal onset detector and validate against recorded samples before building the full classification/action pipeline.

---
*Generated from project notes — update as the implementation progresses.*

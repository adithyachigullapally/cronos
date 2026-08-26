# Tap Detection App

## Overview
A Windows desktop application that uses the laptop's built-in microphone to detect and classify physical taps on the device chassis (e.g., lid, palm rest, deck) and maps recognized tap patterns to configurable user actions (shortcuts, macros, media controls, etc.).

## Status
Early implementation stage.

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

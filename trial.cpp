// tap_detector.cpp
// Minimal WASAPI mic capture + amplitude-spike tap detection.
// Build (MSVC): cl tap_detector.cpp /EHsc ole32.lib
// Or with MinGW-w64: g++ tap_detector.cpp -o tap_detector.exe -lole32 -lwinmm

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <cstdio>
#include <cmath>
#include <vector>
#include <chrono>

#pragma comment(lib, "ole32.lib")

// ---- tunables ----
static const float  THRESHOLD = 0.04f;   // amplitude threshold (0.0-1.0), raise if too sensitive
static const int    DEBOUNCE_MS = 200;      // ignore new taps within this window after one fires
static const int    NOISE_CALIB_MS = 1500;     // time spent measuring ambient noise floor at startup

#define REFTIMES_PER_SEC  10000000
#define EXIT_ON_ERROR(hres) if (FAILED(hres)) { printf("Error 0x%08x at line %d\n", hres, __LINE__); goto Exit; }
#define SAFE_RELEASE(p) if ((p)) { (p)->Release(); (p) = nullptr; }

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0); // disable output buffering so prints show immediately

    HRESULT hr;
    IMMDeviceEnumerator* pEnumerator = nullptr;
    IMMDevice* pDevice = nullptr;
    IAudioClient* pAudioClient = nullptr;
    IAudioCaptureClient* pCaptureClient = nullptr;
    WAVEFORMATEX* pwfx = nullptr;
    UINT32 bufferFrameCount;
    UINT32 numFramesAvailable;
    BYTE* pData;
    DWORD flags;

    hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    EXIT_ON_ERROR(hr);

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    EXIT_ON_ERROR(hr);

    hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
    EXIT_ON_ERROR(hr);

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&pAudioClient);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->GetMixFormat(&pwfx);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0,
        REFTIMES_PER_SEC, 0, pwfx, nullptr);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCaptureClient);
    EXIT_ON_ERROR(hr);

    hr = pAudioClient->Start();
    EXIT_ON_ERROR(hr);

    printf("Mic: %d ch, %d Hz, %d-bit float\n",
        pwfx->nChannels, pwfx->nSamplesPerSec, pwfx->wBitsPerSample);
    printf("wFormatTag: %d (1=PCM, 3=FLOAT, 0xFFFE=EXTENSIBLE)\n", pwfx->wFormatTag);
    printf("Calibrating noise floor for %d ms... stay quiet.\n", NOISE_CALIB_MS);

    {
        auto start = std::chrono::steady_clock::now();
        auto lastTap = start - std::chrono::milliseconds(DEBOUNCE_MS);
        float noiseFloor = 0.0f;
        bool calibrating = true;

        while (true) {
            Sleep(10); // poll every 10ms

            UINT32 packetLength = 0;
            hr = pCaptureClient->GetNextPacketSize(&packetLength);
            EXIT_ON_ERROR(hr);

            while (packetLength != 0) {
                hr = pCaptureClient->GetBuffer(&pData, &numFramesAvailable, &flags, nullptr, nullptr);
                EXIT_ON_ERROR(hr);

                float peak = 0.0f;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pData != nullptr) {
                    // mix format is typically 32-bit float
                    float* samples = reinterpret_cast<float*>(pData);
                    UINT32 totalSamples = numFramesAvailable * pwfx->nChannels;
                    for (UINT32 i = 0; i < totalSamples; i++) {
                        float v = std::fabs(samples[i]);
                        if (v > peak) peak = v;
                    }
                }

                auto now = std::chrono::steady_clock::now();
                double elapsedMs = std::chrono::duration<double, std::milli>(now - start).count();

                if (calibrating) {
                    if (peak > noiseFloor) noiseFloor = peak;
                    if (elapsedMs >= NOISE_CALIB_MS) {
                        calibrating = false;
                        printf("Noise floor: %.4f. Listening for taps (threshold %.2f)...\n",
                            noiseFloor, THRESHOLD);
                    }
                }
                else {
                    // DEBUG: print peak level every ~200ms so you can see raw mic levels
                    static auto lastDebugPrint = now;
                    if (std::chrono::duration<double, std::milli>(now - lastDebugPrint).count() > 200) {
                        printf("[debug] peak=%.5f flags=%lu silent=%d packetLen=%u\n",
                            peak, flags, (flags & AUDCLNT_BUFFERFLAGS_SILENT) ? 1 : 0, numFramesAvailable);
                        lastDebugPrint = now;
                    }

                    float effectiveThreshold = (THRESHOLD > noiseFloor * 3.0f) ? THRESHOLD : noiseFloor * 3.0f;
                    if (peak > effectiveThreshold) {
                        auto sinceLastTap = std::chrono::duration<double, std::milli>(now - lastTap).count();
                        if (sinceLastTap > DEBOUNCE_MS) {
                            printf("Tap detected! (peak=%.3f)\n", peak);
                            lastTap = now;
                        }
                    }
                }

                hr = pCaptureClient->ReleaseBuffer(numFramesAvailable);
                EXIT_ON_ERROR(hr);

                hr = pCaptureClient->GetNextPacketSize(&packetLength);
                EXIT_ON_ERROR(hr);
            }
        }
    }

Exit:
    if (pAudioClient) pAudioClient->Stop();
    if (pwfx) CoTaskMemFree(pwfx);
    SAFE_RELEASE(pCaptureClient);
    SAFE_RELEASE(pAudioClient);
    SAFE_RELEASE(pDevice);
    SAFE_RELEASE(pEnumerator);
    CoUninitialize();
    return 0;
}
// rec <file> <seconds>: records the default mic the way the app does (raw, 2 channels,
// float, 48 kHz) into an interleaved .f32 file.
#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    if (argc < 3)
        return 9;
    const double seconds = atof(argv[2]);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDeviceEnumerator *en = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en));
    IMMDevice *dev = nullptr;
    if (FAILED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &dev)))
        return 1;
    IAudioClient *c = nullptr;
    if (FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void **)&c)))
        return 2;
    bool raw = false;
    IAudioClient2 *c2 = nullptr;
    if (SUCCEEDED(c->QueryInterface(__uuidof(IAudioClient2), (void **)&c2))) {
        AudioClientProperties props{};
        props.cbSize = sizeof(props);
        props.eCategory = AudioCategory_Other;
        props.Options = AUDCLNT_STREAMOPTIONS_RAW;
        raw = SUCCEEDED(c2->SetClientProperties(&props));
        c2->Release();
    }
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wf.nChannels = 2;
    wf.nSamplesPerSec = 48000;
    wf.wBitsPerSample = 32;
    wf.nBlockAlign = 8;
    wf.nAvgBytesPerSec = 48000 * 8;
    if (FAILED(c->Initialize(AUDCLNT_SHAREMODE_SHARED,
                             AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 2000000, 0,
                             &wf, nullptr)))
        return 3;
    IAudioCaptureClient *cc = nullptr;
    c->GetService(__uuidof(IAudioCaptureClient), (void **)&cc);
    FILE *f = fopen(argv[1], "wb");
    if (!f)
        return 4;
    c->Start();
    long long n = 0;
    const long long want = (long long)(seconds * 48000);
    float peak = 0;
    while (n < want) {
        Sleep(10);
        UINT32 packet = 0;
        while (SUCCEEDED(cc->GetNextPacketSize(&packet)) && packet) {
            BYTE *data;
            UINT32 frames;
            DWORD flags;
            if (FAILED(cc->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;
            fwrite(data, 8, frames, f);
            const float *s = (const float *)data;
            for (UINT32 i = 0; i < frames * 2; ++i)
                peak = std::fmax(peak, std::fabs(s[i]));
            n += frames;
            cc->ReleaseBuffer(frames);
        }
    }
    c->Stop();
    fclose(f);
    printf("recorded %.1f s, %s, peak %.3f\n", n / 48000.0, raw ? "raw" : "processed", peak);
    return 0;
}

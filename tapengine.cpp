#include "tapengine.h"
#include "tapdetector.h"

#include <windows.h>
#include <initguid.h> // must precede the header below so PKEY_Device_FriendlyName gets defined here
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <vector>

using Microsoft::WRL::ComPtr;

static const int SampleRate = 48000;

static QString hrText(const char *what, HRESULT hr)
{
    return QStringLiteral("%1 failed (0x%2)").arg(QLatin1String(what)).arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

// Key and mouse-click sounds are short transients too. Windows timestamps the
// last input event, so anything that coincides with one is not a chassis tap.
static bool userInputActive()
{
    LASTINPUTINFO li{sizeof(li), 0};
    return GetLastInputInfo(&li) && GetTickCount() - li.dwTime < 300;
}

static QString friendlyName(IMMDevice *device)
{
    QString name;
    ComPtr<IPropertyStore> props;
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))
        && SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
        name = QString::fromWCharArray(value.pwszVal);
    PropVariantClear(&value);
    return name;
}

QList<TapEngine::Mic> TapEngine::microphones()
{
    QList<Mic> mics;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); // Qt usually did this already
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDeviceCollection> devices;
        UINT count = 0;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       IID_PPV_ARGS(&enumerator)))
            && SUCCEEDED(enumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &devices))
            && SUCCEEDED(devices->GetCount(&count))) {
            for (UINT i = 0; i < count; ++i) {
                ComPtr<IMMDevice> device;
                LPWSTR id = nullptr;
                if (SUCCEEDED(devices->Item(i, &device)) && SUCCEEDED(device->GetId(&id))) {
                    mics.append({QString::fromWCharArray(id), friendlyName(device.Get())});
                    CoTaskMemFree(id);
                }
            }
        }
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return mics;
}

void TapEngine::run()
{
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    while (!isInterruptionRequested()) {
        const QString error = capture(event);
        if (error.isEmpty())
            break;
        // Mic unplugged, default device changed, privacy toggle off... retry.
        emit status(error + QStringLiteral(" - retrying"));
        for (int i = 0; i < 20 && !isInterruptionRequested(); ++i)
            msleep(100);
    }
    CloseHandle(event);
    CoUninitialize();
    emit level(0);
    emit status(QStringLiteral("Stopped"));
}

QString TapEngine::capture(void *event)
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  IID_PPV_ARGS(&enumerator));
    if (FAILED(hr))
        return hrText("Audio device enumerator", hr);
    // The chosen mic, or the Windows default if none was chosen or it has been unplugged.
    ComPtr<IMMDevice> device;
    if (deviceId.isEmpty() || FAILED(enumerator->GetDevice(reinterpret_cast<LPCWSTR>(deviceId.utf16()), &device))) {
        hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
        if (FAILED(hr))
            return QStringLiteral("No microphone found");
    }

    // Raw mode bypasses the driver's noise suppression / AGC, which is built to
    // remove exactly the kind of transient we are looking for. Not every driver
    // supports it, so fall back to the processed stream.
    ComPtr<IAudioClient> client;
    bool raw = false;
    int channels = 1;
    for (bool tryRaw : {true, false}) {
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
        if (FAILED(hr))
            return hrText("Opening the microphone", hr);
        raw = false;
        ComPtr<IAudioClient2> client2;
        if (tryRaw && SUCCEEDED(client.As(&client2))) {
            AudioClientProperties props{};
            props.cbSize = sizeof(props);
            props.eCategory = AudioCategory_Other;
            props.Options = AUDCLNT_STREAMOPTIONS_RAW;
            raw = SUCCEEDED(client2->SetClientProperties(&props));
        }
        // Ask for float at 48 kHz and let Windows convert, but keep a mic array's
        // two channels apart: their time difference tells left taps from right.
        // ponytail: an array with more than two mics gets mixed down to two by
        // Windows; pick channels explicitly (WAVEFORMATEXTENSIBLE) if one shows up.
        WAVEFORMATEX *mix = nullptr;
        channels = SUCCEEDED(client->GetMixFormat(&mix)) && mix->nChannels >= 2 ? 2 : 1;
        CoTaskMemFree(mix);
        WAVEFORMATEX wf{};
        wf.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        wf.nChannels = WORD(channels);
        wf.nSamplesPerSec = SampleRate;
        wf.wBitsPerSample = 32;
        wf.nBlockAlign = WORD(4 * channels);
        wf.nAvgBytesPerSec = SampleRate * wf.nBlockAlign;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
                                    | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                200000 /* 20 ms */, 0, &wf, nullptr);
        if (SUCCEEDED(hr))
            break;
        client.Reset();
    }
    if (FAILED(hr))
        return hrText("Starting capture", hr);

    ComPtr<IAudioCaptureClient> captureClient;
    if (FAILED(hr = client->SetEventHandle(event))
        || FAILED(hr = client->GetService(IID_PPV_ARGS(&captureClient)))
        || FAILED(hr = client->Start()))
        return hrText("Starting capture", hr);
    stereo = channels == 2;
    emit status(QStringLiteral("Listening on %1 (%2 signal, %3)")
                    .arg(friendlyName(device.Get()), raw ? QStringLiteral("raw") : QStringLiteral("driver-processed"),
                         stereo ? QStringLiteral("2 channels") : QStringLiteral("1 channel: no left/right")));

    TapDetector detector;
    detector.cfg.sampleRate = SampleRate;
    detector.onTap = [this](const Tap &t) { emit tap(t.peak, t.durationMs, t.freqHz, t.lag); };
    detector.onPattern = [this](int n, float lag) { emit pattern(n, lag); };

    std::vector<float> left, right;
    float meterPeak = 0;
    int meterFrames = 0;
    while (!isInterruptionRequested()) {
        WaitForSingleObject(event, 200);
        UINT32 packet = 0;
        while (SUCCEEDED(hr = captureClient->GetNextPacketSize(&packet)) && packet) {
            BYTE *data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(hr = captureClient->GetBuffer(&data, &frames, &flags, nullptr, nullptr)))
                break;
            const float *samples = reinterpret_cast<const float *>(data);
            left.assign(frames, 0.f);
            right.assign(frames, 0.f);
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && data) {
                for (UINT32 i = 0; i < frames; ++i) {
                    left[i] = samples[i * channels];
                    right[i] = samples[i * channels + channels - 1];
                }
            }
            detector.cfg.onsetRatio = onsetRatio;
            detector.process(left.data(), frames, ignoreWhileInput && userInputActive(),
                             stereo ? right.data() : nullptr);

            for (UINT32 i = 0; i < frames; ++i)
                meterPeak = std::max(meterPeak, std::fabs(left[i]));
            if ((meterFrames += frames) >= SampleRate / 20) {
                emit level(meterPeak);
                meterPeak = 0;
                meterFrames = 0;
            }
            captureClient->ReleaseBuffer(frames);
        }
        if (FAILED(hr)) {
            client->Stop();
            return hrText("Reading the microphone", hr);
        }
    }
    client->Stop();
    return {};
}

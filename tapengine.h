#ifndef TAPENGINE_H
#define TAPENGINE_H

#include <QThread>
#include <atomic>

// Captures the default microphone with WASAPI on its own thread and runs
// TapDetector over it. start() to listen, stop() to release the mic.
class TapEngine : public QThread
{
    Q_OBJECT

public:
    using QThread::QThread;
    ~TapEngine() override { stop(); }

    void stop()
    {
        requestInterruption();
        wait();
    }

    struct Mic {
        QString id, name;
    };
    static QList<Mic> microphones(); // capture devices that are plugged in right now

    QString deviceId; // empty = Windows default mic. Only change while stopped.
    std::atomic<float> onsetRatio{4.f};       // see TapConfig::onsetRatio
    std::atomic<bool> ignoreWhileInput{true}; // drop taps while keyboard/mouse is in use
    std::atomic<bool> stereo{false};          // the mic in use has two channels, so lags mean something

signals:
    void level(float peak); // raw mic peak, ~20x per second
    void tap(float peak, float durationMs, float freqHz, float lag); // lag: see Tap::lag
    void pattern(int count, float lag); // count 0 = discarded because of keyboard/mouse input
    void status(const QString &text);

protected:
    void run() override;

private:
    QString capture(void *event); // returns an error message, or empty when interrupted
};

#endif // TAPENGINE_H

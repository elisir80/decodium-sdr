// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/IqAnalysisLab.h"
#include "hal/IqFileReader.h"
#include "dsp/SpectrumAnalyzer.h"
#include <QVariantList>

#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <vector>

namespace dsdr::core {

IqAnalysisLab::IqAnalysisLab(QObject *parent) : QObject(parent) {}
IqAnalysisLab::~IqAnalysisLab()
{
    m_cancel.store(true);
    if (m_thread) { m_thread->wait(); delete m_thread; }
}
void IqAnalysisLab::cancel() { m_cancel.store(true); }

bool IqAnalysisLab::start(const QString &input, int slot)
{
    return analyze(input, slot, 0, 0, 1024, 1);
}

bool IqAnalysisLab::analyze(const QString &input, int slot, double startSeconds,
                            double durationSeconds, int fftSize, int window)
{
    if (m_running || (slot != 0 && slot != 1) || input.isEmpty() ||
        !std::isfinite(startSeconds) || !std::isfinite(durationSeconds) ||
        startSeconds < 0 || durationSeconds < 0 || fftSize < 64 || fftSize > 16384 ||
        (fftSize & (fftSize - 1)) != 0 || window < 0 || window > 2)
        return false;
    if (m_thread) { m_thread->wait(); delete m_thread; m_thread = nullptr; }
    const QUrl url(input);
    const QString path = url.isLocalFile() ? url.toLocalFile() : input;
    m_cancel.store(false);
    m_running = true;
    m_progress = 0;
    m_error.clear();
    // Clear the selected slot immediately: a failed run must never look like
    // a successful analysis of the newly selected file.
    (slot == 0 ? m_a : m_b).clear();
    emit changed();
    m_thread = QThread::create([this, path, slot, startSeconds, durationSeconds, fftSize, window] {
        hal::iqfile::IqFileReader reader;
        QString error;
        QVariantMap result;
        if (reader.open(path, &error)) {
            const auto info = reader.info();
            const double fileDuration = double(info.frameCount) / info.sampleRate;
            const double endSeconds = durationSeconds == 0 ? fileDuration : startSeconds + durationSeconds;
            if (startSeconds >= fileDuration || !std::isfinite(endSeconds) || endSeconds > fileDuration + 1e-9) {
                error = tr("Intervallo fuori dalla registrazione.");
            }
            const qint64 firstFrame = error.isEmpty() ? qint64(startSeconds * info.sampleRate + 0.5) : 0;
            const qint64 endFrame = error.isEmpty() ? std::min(info.frameCount, qint64(endSeconds * info.sampleRate + 0.5)) : 0;
            const qint64 selectedFrames = endFrame - firstFrame;
            if (error.isEmpty() && selectedFrames < fftSize)
                error = tr("L'intervallo deve contenere almeno una finestra FFT completa.");
            if (error.isEmpty() && !reader.seek(firstFrame))
                error = tr("Impossibile raggiungere l'intervallo selezionato.");
            dsp::SpectrumAnalyzer analyzer;
            const auto windowType = window == 0 ? dsp::WindowType::Rectangular :
                                    window == 1 ? dsp::WindowType::Hann : dsp::WindowType::BlackmanHarris;
            if (error.isEmpty() && !analyzer.configure(fftSize, info.sampleRate, windowType))
                error = tr("Impossibile creare la FFT.");
            analyzer.setOverlap(0);
            analyzer.setAveraging(1);
            std::vector<dsp::Complex> fftInput;
            fftInput.reserve(fftSize);
            std::vector<float> samples(2 * 16384);
            double sumI = 0, sumQ = 0, power = 0, peak = 0;
            qint64 count = 0, clipped = 0;
            const double positiveLimit = info.format == hal::iqfile::SampleFormat::Int16
                ? 32767.0 / 32768.0 : 1.0;
            QElapsedTimer timer;
            timer.start();
            while (error.isEmpty() && !m_cancel.load() && count < selectedFrames) {
                const auto n = reader.read(samples.data(), std::min<std::size_t>(samples.size() / 2, static_cast<std::size_t>(selectedFrames - count)));
                if (!n) { error = tr("Lettura della registrazione interrotta."); break; }
                for (std::size_t i = 0; i < n; ++i) {
                    const double iv = samples[2*i], qv = samples[2*i+1];
                    if (!std::isfinite(iv) || !std::isfinite(qv)) {
                        error = tr("La registrazione contiene campioni non validi.");
                        break;
                    }
                    if (fftInput.size() < static_cast<std::size_t>(fftSize))
                        fftInput.emplace_back(float(iv), float(qv));
                    sumI += iv; sumQ += qv;
                    const double mag2 = iv*iv + qv*qv;
                    power += mag2;
                    peak = std::max(peak, std::sqrt(mag2));
                    if (iv >= positiveLimit || iv <= -1.0 ||
                        qv >= positiveLimit || qv <= -1.0) ++clipped;
                }
                if (!error.isEmpty()) break;
                count += static_cast<qint64>(n);
                if (timer.elapsed() >= 100) {
                    const double progress = double(count) / double(selectedFrames);
                    QMetaObject::invokeMethod(this, [this, progress] {
                        if (m_running) { m_progress = progress; emit changed(); }
                    }, Qt::QueuedConnection);
                    timer.restart();
                }
            }
            if (error.isEmpty() && !m_cancel.load() && count > 0) {
                analyzer.push(fftInput.data(), fftInput.size());
                const auto &bins = analyzer.magnitudesDb();
                QVariantList spectrum;
                spectrum.reserve(fftSize);
                for (float db : bins) spectrum.append(double(db) - 6.020599913279624);
                const auto maxIt = std::max_element(bins.begin(), bins.end());
                const int peakBin = int(maxIt - bins.begin());
                int left = peakBin, right = peakBin;
                while (left > 0 && bins[left - 1] >= *maxIt - 3.0f) --left;
                while (right + 1 < fftSize && bins[right + 1] >= *maxIt - 3.0f) ++right;
                const double binHz = info.sampleRate / fftSize;
                const double peakOffset = (peakBin - fftSize / 2) * binHz;
                result = {{"spectrum", spectrum}, {"fftSize", fftSize}, {"window", window},
                          {"binHz", binHz}, {"fftFrames", 1},
                          {"fftStartSeconds", double(firstFrame) / info.sampleRate},
                          {"peakValid", power > 1e-20}, {"peakOffsetHz", peakOffset},
                          {"peakDbfs", double(*maxIt) - 6.020599913279624},
                          {"peakRfHz", info.centerFrequencyHz + peakOffset},
                          {"bandwidth3dbHz", (right - left + 1) * binHz},
                          {"path", path}, {"name", QFileInfo(path).fileName()},
                          {"sampleRate", info.sampleRate}, {"centerHz", info.centerFrequencyHz},
                          {"hasSidecar", info.hasSidecar}, {"frames", count},
                          {"durationSeconds", double(count)/info.sampleRate},
                          {"meanI", sumI/count}, {"meanQ", sumQ/count},
                          {"rms", std::sqrt(power/count)}, {"peak", peak},
                          {"clippedPercent", 100.0*clipped/count}};
            }
        }
        const bool cancelled = m_cancel.load();
        QMetaObject::invokeMethod(this, [this, slot, result, error, cancelled] {
            m_running = false;
            m_progress = result.isEmpty() ? 0 : 1;
            m_error = cancelled ? tr("Analisi annullata.") : error;
            if (!result.isEmpty() && !cancelled) (slot == 0 ? m_a : m_b) = result;
            emit changed();
        }, Qt::QueuedConnection);
    });
    m_thread->start();
    return true;
}
}

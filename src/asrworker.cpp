// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#include "asrworker.h"

#include <QDebug>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "audiobuffer.h"

namespace {

float clampSample(float value)
{
    return std::max(-1.0f, std::min(1.0f, value));
}

int32_t readSignedInt(const unsigned char *data, int bytesPerSample, bool littleEndian)
{
    uint32_t raw = 0;
    if (littleEndian) {
        for (int byte = 0; byte < bytesPerSample; ++byte) {
            raw |= static_cast<uint32_t>(data[byte]) << (byte * 8);
        }
    } else {
        for (int byte = 0; byte < bytesPerSample; ++byte) {
            raw = (raw << 8) | static_cast<uint32_t>(data[byte]);
        }
    }

    const int shift = 32 - bytesPerSample * 8;
    return static_cast<int32_t>(raw << shift) >> shift;
}

uint32_t readUnsignedInt(const unsigned char *data, int bytesPerSample, bool littleEndian)
{
    uint32_t raw = 0;
    if (littleEndian) {
        for (int byte = 0; byte < bytesPerSample; ++byte) {
            raw |= static_cast<uint32_t>(data[byte]) << (byte * 8);
        }
    } else {
        for (int byte = 0; byte < bytesPerSample; ++byte) {
            raw = (raw << 8) | static_cast<uint32_t>(data[byte]);
        }
    }

    return raw;
}

float decodePcmValue(const unsigned char *data, const QAudioFormat &format)
{
    const int bytesPerSample = format.sampleSize() / 8;
    const bool littleEndian = format.byteOrder() == QAudioFormat::LittleEndian;
    if (bytesPerSample <= 0) {
        return 0.0f;
    }

    switch (format.sampleType()) {
    case QAudioFormat::SignedInt: {
        const int32_t sample = readSignedInt(data, bytesPerSample, littleEndian);
        const double normalizer = std::pow(2.0, format.sampleSize() - 1);
        return clampSample(static_cast<float>(sample / normalizer));
    }
    case QAudioFormat::UnSignedInt: {
        const uint32_t sample = readUnsignedInt(data, bytesPerSample, littleEndian);
        const double midpoint = std::pow(2.0, format.sampleSize() - 1);
        return clampSample(static_cast<float>((static_cast<double>(sample) - midpoint) / midpoint));
    }
    case QAudioFormat::Float:
        if (format.sampleSize() == 32) {
            float sample = 0.0f;
            std::memcpy(&sample, data, sizeof(float));
            return clampSample(sample);
        }
        break;
    case QAudioFormat::Unknown:
        break;
    }

    return 0.0f;
}

bool decodeRecordedAudio(const QByteArray &recordedAudio,
                         const QAudioFormat &audioFormat,
                         AudioBuffer *audio,
                         QString *errorText)
{
    if (!audio) {
        if (errorText) {
            *errorText = QObject::tr("Internal audio buffer error.");
        }
        return false;
    }

    if (recordedAudio.isEmpty()) {
        if (errorText) {
            *errorText = QObject::tr("No audio data was recorded.");
        }
        return false;
    }

    if (audioFormat.codec() != QStringLiteral("audio/pcm")) {
        if (errorText) {
            *errorText = QObject::tr("Unsupported input codec: only PCM is supported.");
        }
        return false;
    }

    const int bytesPerSample = audioFormat.sampleSize() / 8;
    const int bytesPerFrame = audioFormat.bytesPerFrame();
    const int channels = audioFormat.channelCount();
    if (bytesPerSample <= 0 || bytesPerFrame <= 0 || channels <= 0) {
        if (errorText) {
            *errorText = QObject::tr("Unsupported audio format from microphone.");
        }
        return false;
    }

    const int frameCount = recordedAudio.size() / bytesPerFrame;
    if (frameCount <= 0) {
        if (errorText) {
            *errorText = QObject::tr("Recorded audio is empty.");
        }
        return false;
    }

    audio->sampleRate = audioFormat.sampleRate();
    audio->samples.clear();
    audio->samples.reserve(static_cast<size_t>(frameCount));

    const unsigned char *raw = reinterpret_cast<const unsigned char *>(recordedAudio.constData());
    for (int frame = 0; frame < frameCount; ++frame) {
        const unsigned char *frameData = raw + frame * bytesPerFrame;
        float mixed = 0.0f;
        for (int channel = 0; channel < channels; ++channel) {
            mixed += decodePcmValue(frameData + channel * bytesPerSample, audioFormat);
        }
        audio->samples.push_back(clampSample(mixed / static_cast<float>(channels)));
    }

    if (audio->samples.size() < static_cast<size_t>(audio->sampleRate / 10)) {
        if (errorText) {
            *errorText = QObject::tr("Captured audio is too short.");
        }
        return false;
    }

    return true;
}

} // namespace

AsrWorker::AsrWorker(QObject *parent)
    : QObject(parent)
{
}

void AsrWorker::loadModel(const QString &modelDirectory)
{
    qDebug() << "AsrWorker::loadModel: model directory =" << modelDirectory;

    try {
        ModelLayout layout;
        layout.modelDir = modelDirectory.toStdString();
        layout.encoder = QDir(modelDirectory).filePath(QStringLiteral("v3_e2e_rnnt_encoder.int8.onnx")).toStdString();
        layout.decoder = QDir(modelDirectory).filePath(QStringLiteral("v3_e2e_rnnt_decoder.int8.onnx")).toStdString();
        layout.joint = QDir(modelDirectory).filePath(QStringLiteral("v3_e2e_rnnt_joint.int8.onnx")).toStdString();
        layout.vocab = QDir(modelDirectory).filePath(QStringLiteral("v3_e2e_rnnt_vocab.txt")).toStdString();

        m_recognizer.reset(new RnntRecognizer(layout));
        emit modelLoaded(true, QString());
    } catch (const std::exception &e) {
        qWarning() << "AsrWorker::loadModel: failed:" << e.what();
        m_recognizer.reset();
        emit modelLoaded(false, QString::fromUtf8(e.what()));
    }
}

void AsrWorker::transcribe(const QByteArray &recordedAudio, const QAudioFormat &audioFormat, quint64 requestId)
{
    qDebug() << "AsrWorker::transcribe: requestId =" << requestId
             << "bytes =" << recordedAudio.size();

    if (!m_recognizer) {
        emit transcriptionFinished(requestId, QString(), tr("Model is not loaded yet."));
        return;
    }

    AudioBuffer audio;
    QString errorText;
    if (!decodeRecordedAudio(recordedAudio, audioFormat, &audio, &errorText)) {
        emit transcriptionFinished(requestId, QString(), errorText);
        return;
    }

    try {
        const std::string recognizedText = m_recognizer->Recognize(audio);
        if (recognizedText.empty()) {
            emit transcriptionFinished(requestId, QString(), tr("Speech was not recognized."));
            return;
        }

        emit transcriptionFinished(requestId, QString::fromUtf8(recognizedText.c_str()), QString());
    } catch (const std::exception &e) {
        qWarning() << "AsrWorker::transcribe: failed:" << e.what();
        emit transcriptionFinished(requestId, QString(), QString::fromUtf8(e.what()));
    }
}

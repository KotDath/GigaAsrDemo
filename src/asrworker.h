// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ASRWORKER_H
#define ASRWORKER_H

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <memory>

#include "rnntrecognizer.h"

class AsrWorker : public QObject
{
    Q_OBJECT

public:
    explicit AsrWorker(QObject *parent = nullptr);

public slots:
    void loadModel(const QString &modelDirectory);
    void transcribe(const QByteArray &recordedAudio, const QAudioFormat &audioFormat, quint64 requestId);

signals:
    void modelLoaded(bool success, const QString &errorText);
    void transcriptionFinished(quint64 requestId, const QString &transcript, const QString &errorText);

private:
    std::unique_ptr<RnntRecognizer> m_recognizer;
};

#endif // ASRWORKER_H

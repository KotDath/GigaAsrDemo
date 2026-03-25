// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef GIGAASRRUNNER_H
#define GIGAASRRUNNER_H

#include <QAudio>
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QThread>

class AsrWorker;
class CommandWorker;
class QAudioInput;
class QIODevice;

class GigaAsrRunner : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isModelLoaded READ isModelLoaded NOTIFY isModelLoadedChanged)
    Q_PROPERTY(bool isCommandModelLoaded READ isCommandModelLoaded NOTIFY isCommandModelLoadedChanged)
    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool isTranscribing READ isTranscribing NOTIFY isTranscribingChanged)
    Q_PROPERTY(bool isAnalyzing READ isAnalyzing NOTIFY isAnalyzingChanged)
    Q_PROPERTY(bool isExecuting READ isExecuting NOTIFY isExecutingChanged)
    Q_PROPERTY(QString transcript READ transcript NOTIFY transcriptChanged)
    Q_PROPERTY(QString recognizedAction READ recognizedAction NOTIFY recognizedActionChanged)
    Q_PROPERTY(QString executionResult READ executionResult NOTIFY executionResultChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    explicit GigaAsrRunner(QObject *parent = nullptr);
    ~GigaAsrRunner() override;

    bool isModelLoaded() const { return m_isModelLoaded; }
    bool isCommandModelLoaded() const { return m_isCommandModelLoaded; }
    bool isRecording() const { return m_isRecording; }
    bool isTranscribing() const { return m_isTranscribing; }
    bool isAnalyzing() const { return m_isAnalyzing; }
    bool isExecuting() const { return m_isExecuting; }
    QString transcript() const { return m_transcript; }
    QString recognizedAction() const { return m_recognizedAction; }
    QString executionResult() const { return m_executionResult; }
    QString statusText() const { return m_statusText; }
    QString errorText() const { return m_errorText; }

public slots:
    void loadModel();
    void loadCommandModel();
    void toggleRecording();

signals:
    void isModelLoadedChanged();
    void isCommandModelLoadedChanged();
    void isRecordingChanged();
    void isTranscribingChanged();
    void isAnalyzingChanged();
    void isExecutingChanged();
    void transcriptChanged();
    void recognizedActionChanged();
    void executionResultChanged();
    void statusTextChanged();
    void errorTextChanged();

    void requestModelLoad(const QString &modelDirectory);
    void requestTranscription(const QByteArray &recordedAudio, const QAudioFormat &audioFormat, quint64 requestId);
    void requestCommandModelLoad(const QString &preferredPath);
    void requestCommandProcessing(const QString &transcript, quint64 requestId);

private slots:
    void readAudioData();
    void handleAudioStateChanged(QAudio::State state);
    void onWorkerModelLoaded(bool success, const QString &errorText);
    void onWorkerTranscriptionFinished(quint64 requestId, const QString &transcript, const QString &errorText);
    void onCommandModelLoaded(bool success, const QString &errorText);
    void onCommandExecutionStarted(quint64 requestId, const QString &prettyCall);
    void onCommandFinished(quint64 requestId,
                           const QString &prettyCall,
                           const QString &resultText,
                           const QString &errorText);

private:
    void setModelLoaded(bool value);
    void setCommandModelLoaded(bool value);
    void setRecording(bool value);
    void setTranscribing(bool value);
    void setAnalyzing(bool value);
    void setExecuting(bool value);
    void setTranscript(const QString &value);
    void setRecognizedAction(const QString &value);
    void setExecutionResult(const QString &value);
    void setStatusText(const QString &value);
    void setErrorText(const QString &value);
    void updateReadyStatus();

    QString resolveModelDirectory() const;
    QString resolveCommandModelPath() const;
    bool startRecording();
    void stopRecording();
    void resetAudioCapture();

    AsrWorker *m_worker = nullptr;
    CommandWorker *m_commandWorker = nullptr;
    QThread m_workerThread;
    QThread m_commandThread;
    QAudioInput *m_audioInput = nullptr;
    QIODevice *m_audioIODevice = nullptr;
    QAudioFormat m_audioFormat;
    QByteArray m_recordedAudio;
    bool m_isModelLoaded = false;
    bool m_isCommandModelLoaded = false;
    bool m_isRecording = false;
    bool m_isTranscribing = false;
    bool m_isAnalyzing = false;
    bool m_isExecuting = false;
    bool m_isModelLoading = false;
    bool m_isCommandModelLoading = false;
    quint64 m_nextRequestId = 0;
    quint64 m_activeRequestId = 0;
    QString m_transcript;
    QString m_recognizedAction;
    QString m_executionResult;
    QString m_statusText;
    QString m_errorText;
};

#endif // GIGAASRRUNNER_H

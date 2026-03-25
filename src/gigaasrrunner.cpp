// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#include "gigaasrrunner.h"

#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QDebug>
#include <QMetaType>
#include <QTimer>

#include "asrworker.h"
#include "commandworker.h"

GigaAsrRunner::GigaAsrRunner(QObject *parent)
    : QObject(parent)
    , m_worker(new AsrWorker())
    , m_commandWorker(new CommandWorker())
{
    qRegisterMetaType<QAudioFormat>("QAudioFormat");

    m_worker->moveToThread(&m_workerThread);
    connect(this, SIGNAL(requestModelLoad(QString)), m_worker, SLOT(loadModel(QString)));
    connect(this, SIGNAL(requestTranscription(QByteArray,QAudioFormat,quint64)),
            m_worker, SLOT(transcribe(QByteArray,QAudioFormat,quint64)));
    connect(m_worker, SIGNAL(modelLoaded(bool,QString)),
            this, SLOT(onWorkerModelLoaded(bool,QString)));
    connect(m_worker, SIGNAL(transcriptionFinished(quint64,QString,QString)),
            this, SLOT(onWorkerTranscriptionFinished(quint64,QString,QString)));
    m_workerThread.start();

    m_commandWorker->moveToThread(&m_commandThread);
    connect(this, SIGNAL(requestCommandModelLoad(QString)),
            m_commandWorker, SLOT(loadModel(QString)));
    connect(this, SIGNAL(requestCommandProcessing(QString,quint64)),
            m_commandWorker, SLOT(processTranscript(QString,quint64)));
    connect(m_commandWorker, SIGNAL(modelLoaded(bool,QString)),
            this, SLOT(onCommandModelLoaded(bool,QString)));
    connect(m_commandWorker, SIGNAL(toolExecutionStarted(quint64,QString)),
            this, SLOT(onCommandExecutionStarted(quint64,QString)));
    connect(m_commandWorker, SIGNAL(commandFinished(quint64,QString,QString,QString)),
            this, SLOT(onCommandFinished(quint64,QString,QString,QString)));
    m_commandThread.start();

    qDebug() << "GigaAsrRunner: worker threads started";
    setStatusText(tr("Loading models..."));
    QTimer::singleShot(0, this, SLOT(loadModel()));
    QTimer::singleShot(0, this, SLOT(loadCommandModel()));
}

GigaAsrRunner::~GigaAsrRunner()
{
    qDebug() << "GigaAsrRunner: destroying";
    resetAudioCapture();
    m_workerThread.quit();
    m_commandThread.quit();
    m_workerThread.wait();
    m_commandThread.wait();
    delete m_worker;
    m_worker = nullptr;
    delete m_commandWorker;
    m_commandWorker = nullptr;
}

void GigaAsrRunner::loadModel()
{
    qDebug() << "GigaAsrRunner::loadModel: requested";
    if (m_isModelLoading) {
        qDebug() << "GigaAsrRunner::loadModel: already running";
        return;
    }

    m_isModelLoading = true;
    setErrorText(QString());
    setModelLoaded(false);
    setStatusText(tr("Loading model..."));
    emit requestModelLoad(resolveModelDirectory());
}

void GigaAsrRunner::loadCommandModel()
{
    qDebug() << "GigaAsrRunner::loadCommandModel: requested";
    if (m_isCommandModelLoading) {
        qDebug() << "GigaAsrRunner::loadCommandModel: already running";
        return;
    }

    m_isCommandModelLoading = true;
    setCommandModelLoaded(false);
    updateReadyStatus();
    emit requestCommandModelLoad(resolveCommandModelPath());
}

void GigaAsrRunner::toggleRecording()
{
    qDebug() << "GigaAsrRunner::toggleRecording:"
             << "isModelLoaded=" << m_isModelLoaded
             << "isCommandModelLoaded=" << m_isCommandModelLoaded
             << "isRecording=" << m_isRecording
             << "isTranscribing=" << m_isTranscribing
             << "isAnalyzing=" << m_isAnalyzing
             << "isExecuting=" << m_isExecuting
             << "isModelLoading=" << m_isModelLoading;

    if (!m_isModelLoaded) {
        setErrorText(tr("Speech model is not loaded yet."));
        return;
    }

    if (m_isTranscribing || m_isAnalyzing || m_isExecuting || m_isModelLoading) {
        return;
    }

    if (m_isRecording) {
        stopRecording();
    } else {
        startRecording();
    }
}

void GigaAsrRunner::readAudioData()
{
    if (!m_audioIODevice) {
        return;
    }

    const QByteArray chunk = m_audioIODevice->readAll();
    if (!chunk.isEmpty()) {
        m_recordedAudio.append(chunk);
    }
}

void GigaAsrRunner::handleAudioStateChanged(QAudio::State state)
{
    if (!m_audioInput) {
        qWarning() << "GigaAsrRunner::handleAudioStateChanged: missing audio input, state =" << state;
        return;
    }

    qDebug() << "GigaAsrRunner::handleAudioStateChanged:"
             << "state=" << state
             << "error=" << m_audioInput->error()
             << "isRecording=" << m_isRecording;

    if (state == QAudio::StoppedState && m_audioInput->error() != QAudio::NoError && m_isRecording) {
        setRecording(false);
        updateReadyStatus();
        setErrorText(tr("Audio capture failed."));
        resetAudioCapture();
    }
}

void GigaAsrRunner::onWorkerModelLoaded(bool success, const QString &errorText)
{
    qDebug() << "GigaAsrRunner::onWorkerModelLoaded:"
             << "success=" << success
             << "errorText=" << errorText;

    m_isModelLoading = false;
    setModelLoaded(success);
    if (success) {
        setErrorText(QString());
        updateReadyStatus();
        return;
    }

    updateReadyStatus();
    setErrorText(errorText.isEmpty() ? tr("Failed to load speech model.") : errorText);
}

void GigaAsrRunner::onWorkerTranscriptionFinished(quint64 requestId,
                                                  const QString &transcript,
                                                  const QString &errorText)
{
    if (requestId != m_activeRequestId) {
        qDebug() << "GigaAsrRunner::onWorkerTranscriptionFinished: ignoring stale requestId =" << requestId;
        return;
    }

    qDebug() << "GigaAsrRunner::onWorkerTranscriptionFinished:"
             << "requestId=" << requestId
             << "transcriptLength=" << transcript.length()
             << "errorText=" << errorText;

    setTranscribing(false);

    if (!errorText.isEmpty()) {
        m_activeRequestId = 0;
        updateReadyStatus();
        setErrorText(errorText);
        return;
    }

    setTranscript(transcript);
    setErrorText(QString());
    setRecognizedAction(QString());
    setExecutionResult(QString());

    if (!m_isCommandModelLoaded) {
        m_activeRequestId = 0;
        setStatusText(tr("Recognition finished"));
        setExecutionResult(tr("Command model is not loaded. Transcript only."));
        return;
    }

    setAnalyzing(true);
    setStatusText(tr("Analyzing command..."));
    emit requestCommandProcessing(transcript, requestId);
}

void GigaAsrRunner::onCommandModelLoaded(bool success, const QString &errorText)
{
    qDebug() << "GigaAsrRunner::onCommandModelLoaded:"
             << "success=" << success
             << "errorText=" << errorText;

    m_isCommandModelLoading = false;
    setCommandModelLoaded(success);
    if (!success) {
        qWarning() << "GigaAsrRunner::onCommandModelLoaded: command model unavailable:" << errorText;
    }
    updateReadyStatus();
}

void GigaAsrRunner::onCommandExecutionStarted(quint64 requestId, const QString &prettyCall)
{
    if (requestId != m_activeRequestId) {
        qDebug() << "GigaAsrRunner::onCommandExecutionStarted: ignoring stale requestId =" << requestId;
        return;
    }

    setRecognizedAction(prettyCall);
    setAnalyzing(false);
    setExecuting(true);
    setStatusText(tr("Executing action..."));
}

void GigaAsrRunner::onCommandFinished(quint64 requestId,
                                      const QString &prettyCall,
                                      const QString &resultText,
                                      const QString &errorText)
{
    if (requestId != m_activeRequestId) {
        qDebug() << "GigaAsrRunner::onCommandFinished: ignoring stale requestId =" << requestId;
        return;
    }

    qDebug() << "GigaAsrRunner::onCommandFinished:"
             << "requestId=" << requestId
             << "prettyCall=" << prettyCall
             << "resultText=" << resultText
             << "errorText=" << errorText;

    m_activeRequestId = 0;
    setAnalyzing(false);
    setExecuting(false);
    setRecognizedAction(prettyCall);
    setExecutionResult(resultText);

    if (!errorText.isEmpty()) {
        setStatusText(tr("Action failed"));
        setErrorText(errorText);
        return;
    }

    setErrorText(QString());
    setStatusText(prettyCall.isEmpty() ? tr("No supported action detected") : tr("Done"));
}

void GigaAsrRunner::setModelLoaded(bool value)
{
    if (m_isModelLoaded == value) {
        return;
    }

    m_isModelLoaded = value;
    emit isModelLoadedChanged();
}

void GigaAsrRunner::setCommandModelLoaded(bool value)
{
    if (m_isCommandModelLoaded == value) {
        return;
    }

    m_isCommandModelLoaded = value;
    emit isCommandModelLoadedChanged();
}

void GigaAsrRunner::setRecording(bool value)
{
    if (m_isRecording == value) {
        return;
    }

    m_isRecording = value;
    emit isRecordingChanged();
}

void GigaAsrRunner::setTranscribing(bool value)
{
    if (m_isTranscribing == value) {
        return;
    }

    m_isTranscribing = value;
    emit isTranscribingChanged();
}

void GigaAsrRunner::setAnalyzing(bool value)
{
    if (m_isAnalyzing == value) {
        return;
    }

    m_isAnalyzing = value;
    emit isAnalyzingChanged();
}

void GigaAsrRunner::setExecuting(bool value)
{
    if (m_isExecuting == value) {
        return;
    }

    m_isExecuting = value;
    emit isExecutingChanged();
}

void GigaAsrRunner::setTranscript(const QString &value)
{
    if (m_transcript == value) {
        return;
    }

    m_transcript = value;
    emit transcriptChanged();
}

void GigaAsrRunner::setRecognizedAction(const QString &value)
{
    if (m_recognizedAction == value) {
        return;
    }

    m_recognizedAction = value;
    emit recognizedActionChanged();
}

void GigaAsrRunner::setExecutionResult(const QString &value)
{
    if (m_executionResult == value) {
        return;
    }

    m_executionResult = value;
    emit executionResultChanged();
}

void GigaAsrRunner::setStatusText(const QString &value)
{
    if (m_statusText == value) {
        return;
    }

    m_statusText = value;
    emit statusTextChanged();
}

void GigaAsrRunner::setErrorText(const QString &value)
{
    if (m_errorText == value) {
        return;
    }

    m_errorText = value;
    emit errorTextChanged();
}

void GigaAsrRunner::updateReadyStatus()
{
    if (m_isModelLoading || m_isCommandModelLoading) {
        setStatusText(tr("Loading models..."));
        return;
    }

    if (!m_isModelLoaded) {
        setStatusText(tr("Speech model is not loaded"));
        return;
    }

    setStatusText(m_isCommandModelLoaded
                  ? tr("Ready to record")
                  : tr("Ready to record (command model unavailable)"));
}

QString GigaAsrRunner::resolveModelDirectory() const
{
    return QStringLiteral("/usr/share/ru.auroraos.GigaAsrDemo/models/gigaam-v3-e2e-rnnt");
}

QString GigaAsrRunner::resolveCommandModelPath() const
{
    return QStringLiteral("/usr/share/ru.auroraos.GigaAsrDemo/models/functiongemma");
}

bool GigaAsrRunner::startRecording()
{
    qDebug() << "GigaAsrRunner::startRecording: begin";

    const QList<QAudioDeviceInfo> devices = QAudioDeviceInfo::availableDevices(QAudio::AudioInput);
    if (devices.isEmpty()) {
        qWarning() << "GigaAsrRunner::startRecording: no audio input devices";
        setErrorText(tr("Microphone input is not available."));
        return false;
    }

    resetAudioCapture();
    setTranscript(QString());
    setRecognizedAction(QString());
    setExecutionResult(QString());
    setErrorText(QString());
    setAnalyzing(false);
    setExecuting(false);

    QAudioFormat requestedFormat;
    requestedFormat.setCodec(QStringLiteral("audio/pcm"));
    requestedFormat.setSampleRate(16000);
    requestedFormat.setChannelCount(1);
    requestedFormat.setSampleSize(16);
    requestedFormat.setSampleType(QAudioFormat::SignedInt);
    requestedFormat.setByteOrder(QAudioFormat::LittleEndian);

    const QAudioDeviceInfo deviceInfo = QAudioDeviceInfo::defaultInputDevice();
    m_audioFormat = deviceInfo.isFormatSupported(requestedFormat) ? requestedFormat : deviceInfo.nearestFormat(requestedFormat);
    qDebug() << "GigaAsrRunner::startRecording: device =" << deviceInfo.deviceName()
             << "sampleRate=" << m_audioFormat.sampleRate()
             << "channels=" << m_audioFormat.channelCount()
             << "sampleSize=" << m_audioFormat.sampleSize();

    m_audioInput = new QAudioInput(deviceInfo, m_audioFormat, this);
    connect(m_audioInput, SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleAudioStateChanged(QAudio::State)));

    m_audioIODevice = m_audioInput->start();
    if (!m_audioIODevice) {
        qWarning() << "GigaAsrRunner::startRecording: failed to start audio device";
        setErrorText(tr("Failed to start audio capture."));
        resetAudioCapture();
        return false;
    }

    connect(m_audioIODevice, SIGNAL(readyRead()), this, SLOT(readAudioData()));

    setRecording(true);
    setStatusText(tr("Recording... Tap again to stop."));
    return true;
}

void GigaAsrRunner::stopRecording()
{
    qDebug() << "GigaAsrRunner::stopRecording: begin"
             << "isRecording=" << m_isRecording
             << "recordedBytes=" << m_recordedAudio.size();

    if (!m_isRecording) {
        qDebug() << "GigaAsrRunner::stopRecording: ignored, not recording";
        return;
    }

    readAudioData();
    setRecording(false);

    if (m_audioIODevice) {
        m_audioIODevice->disconnect(this);
        m_audioIODevice = nullptr;
    }

    if (m_audioInput) {
        qDebug() << "GigaAsrRunner::stopRecording: stopping QAudioInput";
        m_audioInput->stop();
    }

    const QByteArray recordedAudio = m_recordedAudio;
    const QAudioFormat audioFormat = m_audioFormat;
    resetAudioCapture();

    setErrorText(QString());
    setTranscribing(true);
    setStatusText(tr("Recognizing speech..."));

    m_activeRequestId = ++m_nextRequestId;
    qDebug() << "GigaAsrRunner::stopRecording: queued requestId =" << m_activeRequestId;
    emit requestTranscription(recordedAudio, audioFormat, m_activeRequestId);
}

void GigaAsrRunner::resetAudioCapture()
{
    if (m_audioIODevice) {
        m_audioIODevice = nullptr;
    }

    if (m_audioInput) {
        m_audioInput->disconnect(this);
        delete m_audioInput;
        m_audioInput = nullptr;
    }

    m_recordedAudio.clear();
}

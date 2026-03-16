// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#include "gigaasrrunner.h"

#include <QAudioDeviceInfo>
#include <QAudioInput>
#include <QDebug>
#include <QMetaType>
#include <QTimer>

#include "asrworker.h"

GigaAsrRunner::GigaAsrRunner(QObject *parent)
    : QObject(parent)
    , m_worker(new AsrWorker())
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

    qDebug() << "GigaAsrRunner: worker thread started";
    setStatusText(tr("Loading model..."));
    QTimer::singleShot(0, this, SLOT(loadModel()));
}

GigaAsrRunner::~GigaAsrRunner()
{
    qDebug() << "GigaAsrRunner: destroying";
    resetAudioCapture();
    m_workerThread.quit();
    m_workerThread.wait();
    delete m_worker;
    m_worker = nullptr;
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

void GigaAsrRunner::toggleRecording()
{
    qDebug() << "GigaAsrRunner::toggleRecording:"
             << "isModelLoaded=" << m_isModelLoaded
             << "isRecording=" << m_isRecording
             << "isTranscribing=" << m_isTranscribing
             << "isModelLoading=" << m_isModelLoading;

    if (!m_isModelLoaded) {
        setErrorText(tr("Model is not loaded yet."));
        return;
    }

    if (m_isTranscribing || m_isModelLoading) {
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
        setStatusText(tr("Ready to record"));
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
        setStatusText(tr("Ready to record"));
        setErrorText(QString());
        return;
    }

    setStatusText(tr("Model is not loaded"));
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

    m_activeRequestId = 0;
    setTranscribing(false);

    if (!errorText.isEmpty()) {
        setStatusText(tr("Ready to record"));
        setErrorText(errorText);
        return;
    }

    setTranscript(transcript);
    setStatusText(tr("Recognition finished"));
    setErrorText(QString());
}

void GigaAsrRunner::setModelLoaded(bool value)
{
    if (m_isModelLoaded == value) {
        return;
    }

    m_isModelLoaded = value;
    emit isModelLoadedChanged();
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

void GigaAsrRunner::setTranscript(const QString &value)
{
    if (m_transcript == value) {
        return;
    }

    m_transcript = value;
    emit transcriptChanged();
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

QString GigaAsrRunner::resolveModelDirectory() const
{
    return QStringLiteral("/usr/share/ru.auroraos.GigaAsrDemo/models/gigaam-v3-e2e-rnnt");
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
    setErrorText(QString());

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

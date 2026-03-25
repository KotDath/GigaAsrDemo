// SPDX-FileCopyrightText: 2026 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#include "commandworker.h"

#include <QDebug>

CommandWorker::CommandWorker(QObject *parent)
    : QObject(parent)
    , m_engine(&m_modelManager)
{
}

void CommandWorker::loadModel(const QString &preferredPath)
{
    QString resolvedModelPath;
    QString errorText;
    const bool success = m_modelManager.loadModel(preferredPath, &resolvedModelPath, &errorText);

    qDebug() << "CommandWorker::loadModel:"
             << "success=" << success
             << "path=" << resolvedModelPath
             << "errorText=" << errorText;

    emit modelLoaded(success, errorText);
}

void CommandWorker::processTranscript(const QString &transcript, quint64 requestId)
{
    qDebug() << "CommandWorker::processTranscript:"
             << "requestId=" << requestId
             << "transcript=" << transcript;

    if (!m_modelManager.isLoaded()) {
        emit commandFinished(requestId,
                             QString(),
                             QString(),
                             tr("Command model is not loaded."));
        return;
    }

    const ParsedFunctionCall call = m_engine.inferToolCall(transcript);
    if (!call.valid) {
        if (!call.rawText.isEmpty()) {
            emit commandFinished(requestId,
                                 QString(),
                                 tr("No supported action detected."),
                                 QString());
        } else {
            emit commandFinished(requestId,
                                 QString(),
                                 QString(),
                                 call.errorText);
        }
        return;
    }

    emit toolExecutionStarted(requestId, call.prettyText);

    const ToolExecutionResult execution = m_executor.execute(call);
    emit commandFinished(requestId,
                         execution.prettyCall,
                         execution.resultText,
                         execution.errorText);
}

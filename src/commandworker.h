// SPDX-FileCopyrightText: 2026 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMMANDWORKER_H
#define COMMANDWORKER_H

#include <QObject>
#include <QString>

#include "commandmodelmanager.h"
#include "functioncallengine.h"
#include "toolexecutor.h"

class CommandWorker : public QObject
{
    Q_OBJECT

public:
    explicit CommandWorker(QObject *parent = nullptr);

public slots:
    void loadModel(const QString &preferredPath);
    void processTranscript(const QString &transcript, quint64 requestId);

signals:
    void modelLoaded(bool success, const QString &errorText);
    void toolExecutionStarted(quint64 requestId, const QString &prettyCall);
    void commandFinished(quint64 requestId,
                         const QString &prettyCall,
                         const QString &resultText,
                         const QString &errorText);

private:
    CommandModelManager m_modelManager;
    FunctionCallEngine m_engine;
    ToolExecutor m_executor;
};

#endif // COMMANDWORKER_H

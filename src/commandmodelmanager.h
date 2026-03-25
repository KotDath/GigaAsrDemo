// SPDX-FileCopyrightText: 2026 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef COMMANDMODELMANAGER_H
#define COMMANDMODELMANAGER_H

#include <QString>

struct llama_model;
struct llama_context;
struct llama_sampler;

class CommandModelManager
{
public:
    CommandModelManager();
    ~CommandModelManager();

    bool loadModel(const QString &preferredPath, QString *resolvedModelPath, QString *errorText);
    bool isLoaded() const { return m_model && m_context && m_sampler; }

    llama_model *model() const { return m_model; }
    llama_context *context() const { return m_context; }
    llama_sampler *sampler() const { return m_sampler; }

    QString findModelPath(const QString &preferredPath = QString()) const;

private:
    void releaseModel();
    QString resolveCandidate(const QString &candidate) const;

    llama_model *m_model = nullptr;
    llama_context *m_context = nullptr;
    llama_sampler *m_sampler = nullptr;
    bool m_backendInitialized = false;
};

#endif // COMMANDMODELMANAGER_H

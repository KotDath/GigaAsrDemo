// SPDX-FileCopyrightText: 2026 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#include "commandmodelmanager.h"

#include <llama.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QStandardPaths>

namespace {

const int kContextSize = 4096;

QString firstModelInDirectory(const QString &directoryPath)
{
    const QDir dir(directoryPath);
    if (!dir.exists()) {
        return QString();
    }

    const QStringList candidates = dir.entryList(QStringList() << QStringLiteral("*.gguf"),
                                                 QDir::Files,
                                                 QDir::Name);
    if (candidates.isEmpty()) {
        return QString();
    }

    const QString exactQ8 = QStringLiteral("functiongemma-q8_0.gguf");
    if (candidates.contains(exactQ8)) {
        return dir.absoluteFilePath(exactQ8);
    }

    for (const QString &candidate : candidates) {
        if (candidate.endsWith(QStringLiteral("-q8_0.gguf"), Qt::CaseInsensitive)) {
            return dir.absoluteFilePath(candidate);
        }
    }

    return dir.absoluteFilePath(candidates.first());
}

} // namespace

CommandModelManager::CommandModelManager()
{
}

CommandModelManager::~CommandModelManager()
{
    releaseModel();
    if (m_backendInitialized) {
        llama_backend_free();
    }
}

bool CommandModelManager::loadModel(const QString &preferredPath,
                                    QString *resolvedModelPath,
                                    QString *errorText)
{
    const QString modelPath = findModelPath(preferredPath);
    if (resolvedModelPath) {
        *resolvedModelPath = modelPath;
    }

    if (modelPath.isEmpty()) {
        if (errorText) {
            *errorText = QObject::tr("Function model GGUF file was not found.");
        }
        return false;
    }

    releaseModel();

    if (!m_backendInitialized) {
        llama_backend_init();
        llama_numa_init(GGML_NUMA_STRATEGY_DISABLED);
        m_backendInitialized = true;
    }

    const QByteArray modelPathBytes = modelPath.toUtf8();
    llama_model_params modelParams = llama_model_default_params();
    m_model = llama_model_load_from_file(modelPathBytes.constData(), modelParams);
    if (!m_model) {
        if (errorText) {
            *errorText = QObject::tr("Failed to load FunctionGemma model from %1.")
                    .arg(modelPath);
        }
        return false;
    }

    llama_context_params contextParams = llama_context_default_params();
    contextParams.n_ctx = kContextSize;
    m_context = llama_init_from_model(m_model, contextParams);
    if (!m_context) {
        if (errorText) {
            *errorText = QObject::tr("Failed to initialize llama.cpp context.");
        }
        releaseModel();
        return false;
    }

    m_sampler = llama_sampler_chain_init(llama_sampler_chain_default_params());
    if (!m_sampler) {
        if (errorText) {
            *errorText = QObject::tr("Failed to initialize llama.cpp sampler.");
        }
        releaseModel();
        return false;
    }

    llama_sampler_chain_add(m_sampler, llama_sampler_init_greedy());
    if (errorText) {
        errorText->clear();
    }
    return true;
}

QString CommandModelManager::findModelPath(const QString &preferredPath) const
{
    QStringList searchCandidates;
    if (!preferredPath.trimmed().isEmpty()) {
        searchCandidates << preferredPath.trimmed();
    }

    const QByteArray envModelPath = qgetenv("LLAMA_MODEL_PATH");
    if (!envModelPath.isEmpty()) {
        searchCandidates << QString::fromUtf8(envModelPath);
    }

    const QString bundledDir = QStringLiteral("/usr/share/ru.auroraos.GigaAsrDemo/models/functiongemma");
    const QString appDir = QCoreApplication::applicationDirPath();
    searchCandidates << bundledDir
                     << QDir(appDir).absoluteFilePath(QStringLiteral("../share/ru.auroraos.GigaAsrDemo/models/functiongemma"))
                     << QDir(appDir).absoluteFilePath(QStringLiteral("../models/functiongemma"))
                     << QDir(appDir).absoluteFilePath(QStringLiteral("models/functiongemma"))
                     << QDir::current().absoluteFilePath(QStringLiteral("models/functiongemma"))
                     << QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).absoluteFilePath(QStringLiteral("functiongemma"))
                     << QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).absoluteFilePath(QStringLiteral("functiongemma"));

    for (const QString &candidate : searchCandidates) {
        const QString resolved = resolveCandidate(candidate);
        if (!resolved.isEmpty()) {
            return resolved;
        }
    }

    return QString();
}

void CommandModelManager::releaseModel()
{
    if (m_sampler) {
        llama_sampler_free(m_sampler);
        m_sampler = nullptr;
    }

    if (m_context) {
        llama_free(m_context);
        m_context = nullptr;
    }

    if (m_model) {
        llama_model_free(m_model);
        m_model = nullptr;
    }
}

QString CommandModelManager::resolveCandidate(const QString &candidate) const
{
    const QFileInfo info(candidate);
    if (info.exists() && info.isReadable()) {
        if (info.isFile() && info.suffix() == QStringLiteral("gguf")) {
            return info.absoluteFilePath();
        }
        if (info.isDir()) {
            return firstModelInDirectory(info.absoluteFilePath());
        }
    }

    return QString();
}

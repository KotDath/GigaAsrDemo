// SPDX-FileCopyrightText: 2026 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef FUNCTIONCALLENGINE_H
#define FUNCTIONCALLENGINE_H

#include <QString>
#include <QVariantMap>

class CommandModelManager;

struct ParsedFunctionCall {
    bool valid = false;
    QString name;
    QVariantMap arguments;
    QString prettyText;
    QString rawText;
    QString errorText;
};

class FunctionCallEngine
{
public:
    explicit FunctionCallEngine(CommandModelManager *modelManager = nullptr);

    void setModelManager(CommandModelManager *modelManager) { m_modelManager = modelManager; }
    ParsedFunctionCall inferToolCall(const QString &userText);

private:
    QString buildPrompt(const QString &userText) const;
    QString generate(const QString &prompt, QString *errorText) const;
    ParsedFunctionCall parseFunctionCall(const QString &generatedText) const;

    CommandModelManager *m_modelManager = nullptr;
};

#endif // FUNCTIONCALLENGINE_H

// SPDX-FileCopyrightText: 2026 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#include "functioncallengine.h"

#include "commandmodelmanager.h"

#include <llama.h>

#include <QByteArray>
#include <QObject>
#include <QRegularExpression>
#include <vector>

namespace {

const int kMaxGeneratedTokens = 128;
const char kFunctionPrompt[] =
        "<start_of_turn>developer\n"
        "You are a model that can do function calling with the following functions"
        "<start_function_declaration>"
        "declaration:toggle_flashlight"
        "{description:<escape>Turn the device flashlight on or off, or invert the current state.<escape>,"
        "parameters:{properties:{state:{description:<escape>Desired flashlight state.<escape>,"
        "enum:[<escape>on<escape>,<escape>off<escape>,<escape>toggle<escape>],"
        "type:<escape>STRING<escape>}},required:[<escape>state<escape>],type:<escape>OBJECT<escape>}}"
        "<end_function_declaration>"
        "<start_function_declaration>"
        "declaration:take_screenshot"
        "{description:<escape>Capture the current screen and save the resulting image file.<escape>,"
        "parameters:{properties:{format:{description:<escape>Image format to save.<escape>,"
        "enum:[<escape>png<escape>],type:<escape>STRING<escape>},"
        "overwrite:{description:<escape>Whether an existing file may be overwritten.<escape>,type:<escape>BOOLEAN<escape>},"
        "path:{description:<escape>Absolute output path for the screenshot PNG file.<escape>,type:<escape>STRING<escape>}},"
        "required:[<escape>path<escape>],type:<escape>OBJECT<escape>}}"
        "<end_function_declaration><end_of_turn>\n"
        "<start_of_turn>user\n";
const char kPromptSuffix[] = "<end_of_turn>\n<start_of_turn>model\n";
const char kStartFunctionCall[] = "<start_function_call>";
const char kEndFunctionCall[] = "<end_function_call>";
const char kStartFunctionResponse[] = "<start_function_response>";

QString captureEscapedValue(const QString &body, const QString &key)
{
    const QRegularExpression pattern(
            QStringLiteral("%1:<escape>([^<]*)<escape>")
                .arg(QRegularExpression::escape(key)));
    const QRegularExpressionMatch match = pattern.match(body);
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

bool captureBooleanValue(const QString &body, const QString &key, bool *value)
{
    if (!value) {
        return false;
    }

    const QRegularExpression pattern(
            QStringLiteral("%1:(true|false)")
                .arg(QRegularExpression::escape(key)));
    const QRegularExpressionMatch match = pattern.match(body);
    if (!match.hasMatch()) {
        return false;
    }

    *value = match.captured(1) == QStringLiteral("true");
    return true;
}

QString trimToFirstMarker(const QString &text)
{
    QString result = text;
    const int responseMarker = result.indexOf(QString::fromLatin1(kStartFunctionResponse));
    if (responseMarker >= 0) {
        result = result.left(responseMarker);
    }
    const int endMarker = result.indexOf(QString::fromLatin1(kEndFunctionCall));
    if (endMarker >= 0) {
        result = result.left(endMarker + int(sizeof(kEndFunctionCall)) - 1);
    }
    return result.trimmed();
}

} // namespace

FunctionCallEngine::FunctionCallEngine(CommandModelManager *modelManager)
    : m_modelManager(modelManager)
{
}

ParsedFunctionCall FunctionCallEngine::inferToolCall(const QString &userText)
{
    ParsedFunctionCall result;
    const QString prompt = buildPrompt(userText);
    QString errorText;
    const QString generatedText = generate(prompt, &errorText);
    result.rawText = generatedText;
    if (!errorText.isEmpty()) {
        result.errorText = errorText;
        return result;
    }

    return parseFunctionCall(generatedText);
}

QString FunctionCallEngine::buildPrompt(const QString &userText) const
{
    return QString::fromLatin1(kFunctionPrompt)
            + userText.trimmed()
            + QString::fromLatin1(kPromptSuffix);
}

QString FunctionCallEngine::generate(const QString &prompt, QString *errorText) const
{
    if (!m_modelManager || !m_modelManager->isLoaded()) {
        if (errorText) {
            *errorText = QObject::tr("Command model is not loaded.");
        }
        return QString();
    }

    llama_model *model = m_modelManager->model();
    llama_context *context = m_modelManager->context();
    llama_sampler *sampler = m_modelManager->sampler();

    if (!model || !context || !sampler) {
        if (errorText) {
            *errorText = QObject::tr("Command model is not initialized.");
        }
        return QString();
    }

    const llama_vocab *vocab = llama_model_get_vocab(model);
    const QByteArray promptBytes = prompt.toUtf8();
    const int promptTokenCount = -llama_tokenize(vocab,
                                                 promptBytes.constData(),
                                                 promptBytes.size(),
                                                 nullptr,
                                                 0,
                                                 true,
                                                 true);
    if (promptTokenCount <= 0) {
        if (errorText) {
            *errorText = QObject::tr("Failed to tokenize FunctionGemma prompt.");
        }
        return QString();
    }

    if (promptTokenCount + kMaxGeneratedTokens >= static_cast<int>(llama_n_ctx(context))) {
        if (errorText) {
            *errorText = QObject::tr("FunctionGemma prompt does not fit into the context window.");
        }
        return QString();
    }

    std::vector<llama_token> promptTokens(static_cast<size_t>(promptTokenCount));
    if (llama_tokenize(vocab,
                       promptBytes.constData(),
                       promptBytes.size(),
                       promptTokens.data(),
                       promptTokens.size(),
                       true,
                       true) < 0) {
        if (errorText) {
            *errorText = QObject::tr("Failed to tokenize FunctionGemma prompt.");
        }
        return QString();
    }

    llama_memory_clear(llama_get_memory(context), true);
    llama_sampler_reset(sampler);

    QString generatedText;
    llama_batch batch = llama_batch_get_one(promptTokens.data(), promptTokens.size());

    for (int generated = 0; generated < kMaxGeneratedTokens; ++generated) {
        if (llama_decode(context, batch) != 0) {
            if (errorText) {
                *errorText = QObject::tr("llama.cpp failed to decode FunctionGemma prompt.");
            }
            return QString();
        }

        const llama_token tokenId = llama_sampler_sample(sampler, context, -1);
        if (llama_vocab_is_eog(vocab, tokenId)) {
            break;
        }

        char tokenBuffer[512];
        const int tokenLength = llama_token_to_piece(vocab, tokenId, tokenBuffer, sizeof(tokenBuffer), 0, true);
        if (tokenLength <= 0) {
            continue;
        }

        generatedText += QString::fromUtf8(tokenBuffer, tokenLength);
        if (generatedText.contains(QString::fromLatin1(kEndFunctionCall))
                || generatedText.contains(QString::fromLatin1(kStartFunctionResponse))) {
            break;
        }

        llama_token nextToken = tokenId;
        batch = llama_batch_get_one(&nextToken, 1);
    }

    if (errorText) {
        errorText->clear();
    }
    return trimToFirstMarker(generatedText);
}

ParsedFunctionCall FunctionCallEngine::parseFunctionCall(const QString &generatedText) const
{
    ParsedFunctionCall result;
    result.rawText = generatedText.trimmed();

    QString payload = result.rawText;
    const int startMarker = payload.indexOf(QString::fromLatin1(kStartFunctionCall));
    if (startMarker >= 0) {
        payload = payload.mid(startMarker + int(sizeof(kStartFunctionCall)) - 1);
    }
    const int endMarker = payload.indexOf(QString::fromLatin1(kEndFunctionCall));
    if (endMarker >= 0) {
        payload = payload.left(endMarker);
    }
    payload = payload.trimmed();

    const QRegularExpression callPattern(QStringLiteral("^call:([A-Za-z0-9_]+)\\{(.*)\\}$"),
                                         QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch callMatch = callPattern.match(payload);
    if (!callMatch.hasMatch()) {
        result.errorText = QObject::tr("No function call was found in the model output.");
        return result;
    }

    result.name = callMatch.captured(1).trimmed();
    const QString body = callMatch.captured(2).trimmed();

    if (result.name == QStringLiteral("toggle_flashlight")) {
        const QString state = captureEscapedValue(body, QStringLiteral("state"));
        if (state != QStringLiteral("on")
                && state != QStringLiteral("off")
                && state != QStringLiteral("toggle")) {
            result.errorText = QObject::tr("FunctionGemma returned an invalid flashlight state.");
            return result;
        }

        result.valid = true;
        result.arguments.insert(QStringLiteral("state"), state);
        result.prettyText = QStringLiteral("toggle_flashlight(state=\"%1\")").arg(state);
        return result;
    }

    if (result.name == QStringLiteral("take_screenshot")) {
        const QString path = captureEscapedValue(body, QStringLiteral("path"));
        const QString format = captureEscapedValue(body, QStringLiteral("format"));
        bool overwrite = false;
        const bool hasOverwrite = captureBooleanValue(body, QStringLiteral("overwrite"), &overwrite);

        if (!path.isEmpty()) {
            result.arguments.insert(QStringLiteral("path"), path);
        }
        result.arguments.insert(QStringLiteral("format"),
                                format.isEmpty() ? QStringLiteral("png") : format);
        result.arguments.insert(QStringLiteral("overwrite"), hasOverwrite ? overwrite : false);

        const QString displayPath = path.isEmpty() ? QStringLiteral("<default>") : path;
        result.valid = true;
        result.prettyText = QStringLiteral("take_screenshot(path=\"%1\", format=\"%2\", overwrite=%3)")
                .arg(displayPath,
                     result.arguments.value(QStringLiteral("format")).toString(),
                     result.arguments.value(QStringLiteral("overwrite")).toBool()
                        ? QStringLiteral("true")
                        : QStringLiteral("false"));
        return result;
    }

    result.errorText = QObject::tr("FunctionGemma requested an unsupported tool: %1.")
            .arg(result.name);
    return result;
}

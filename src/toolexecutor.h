// SPDX-FileCopyrightText: 2026 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#ifndef TOOLEXECUTOR_H
#define TOOLEXECUTOR_H

#include "functioncallengine.h"

#include <QString>

struct ToolExecutionResult {
    QString prettyCall;
    QString resultText;
    QString errorText;
};

class ToolExecutor
{
public:
    ToolExecutionResult execute(const ParsedFunctionCall &call) const;

private:
    ToolExecutionResult executeFlashlight(const ParsedFunctionCall &call) const;
    ToolExecutionResult executeScreenshot(const ParsedFunctionCall &call) const;
    ToolExecutionResult executeScreenGrabScreenshot(const ParsedFunctionCall &call,
                                                    const QString &targetPath) const;
    ToolExecutionResult executeLipstickScreenshot(const ParsedFunctionCall &call,
                                                  const QString &targetPath) const;
    QString sanitizeScreenshotPath(const QString &requestedPath) const;
};

#endif // TOOLEXECUTOR_H

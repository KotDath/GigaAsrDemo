// SPDX-FileCopyrightText: 2026 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#include "toolexecutor.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <QVariantMap>

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <unistd.h>

namespace {

const char kFlashlightService[] = "com.jolla.settings.system.flashlight";
const char kFlashlightPath[] = "/com/jolla/settings/system/flashlight";
const char kFlashlightInterface[] = "com.jolla.settings.system.flashlight";

const char kScreenGrabService[] = "ru.auroraos.ScreenGrab1";
const char kScreenGrabPath[] = "/ru/auroraos/ScreenGrab1/primary";
const char kScreenGrabInterface[] = "ru.auroraos.ScreenGrab1.Screen";

const char kScreenshotService[] = "org.nemomobile.lipstick";
const char kScreenshotPath[] = "/org/nemomobile/lipstick/screenshot";
const char kScreenshotInterface[] = "org.nemomobile.lipstick";

const int kScreenGrabPollTimeoutMs = 400;
const int kScreenGrabMaxReadAttempts = 24;
const int kScreenGrabMaxBytes = 64 * 1024 * 1024;

QString screenshotRootDirectory()
{
    QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (pictures.isEmpty()) {
        pictures = QDir::homePath() + QStringLiteral("/Pictures");
    }
    return QDir(pictures).absoluteFilePath(QStringLiteral("Screenshots"));
}

QString timestampedScreenshotPath()
{
    const QString fileName = QStringLiteral("shot_%1.png")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_hhmmss")));
    return QDir(screenshotRootDirectory()).absoluteFilePath(fileName);
}

QString dbusErrorText(const QDBusMessage &reply, const QString &fallback)
{
    if (reply.type() != QDBusMessage::ErrorMessage) {
        return QString();
    }

    const QString errorName = reply.errorName().trimmed();
    const QString error = reply.errorMessage().trimmed();
    if (!errorName.isEmpty() && !error.isEmpty()) {
        return QStringLiteral("%1: %2").arg(errorName, error);
    }
    if (!error.isEmpty()) {
        return error;
    }
    if (!errorName.isEmpty()) {
        return errorName;
    }
    return fallback;
}

QString dbusInterfaceErrorText(const QDBusInterface &iface, const QString &fallback)
{
    const QDBusError error = iface.lastError();
    if (!error.isValid()) {
        return fallback;
    }

    const QString name = error.name().trimmed();
    const QString message = error.message().trimmed();
    if (!name.isEmpty() && !message.isEmpty()) {
        return QStringLiteral("%1: %2").arg(name, message);
    }
    if (!message.isEmpty()) {
        return message;
    }
    if (!name.isEmpty()) {
        return name;
    }
    return fallback;
}

QString formatReplyState(const QDBusReply<bool> &reply)
{
    if (!reply.isValid()) {
        return QStringLiteral("error:%1").arg(reply.error().name().trimmed().isEmpty()
                ? reply.error().message().trimmed()
                : QStringLiteral("%1:%2").arg(reply.error().name().trimmed(),
                                              reply.error().message().trimmed()));
    }
    return reply.value() ? QStringLiteral("true") : QStringLiteral("false");
}

QString formatReplyState(const QDBusReply<uint> &reply)
{
    if (!reply.isValid()) {
        return QStringLiteral("error:%1").arg(reply.error().name().trimmed().isEmpty()
                ? reply.error().message().trimmed()
                : QStringLiteral("%1:%2").arg(reply.error().name().trimmed(),
                                              reply.error().message().trimmed()));
    }
    return QString::number(reply.value());
}

QString formatReplyState(const QDBusReply<QString> &reply)
{
    if (!reply.isValid()) {
        return QStringLiteral("error:%1").arg(reply.error().name().trimmed().isEmpty()
                ? reply.error().message().trimmed()
                : QStringLiteral("%1:%2").arg(reply.error().name().trimmed(),
                                              reply.error().message().trimmed()));
    }
    return reply.value().isEmpty() ? QStringLiteral("<empty>") : reply.value();
}

QString formatReplyError(const QDBusError &error)
{
    const QString name = error.name().trimmed();
    const QString message = error.message().trimmed();
    if (!name.isEmpty() && !message.isEmpty()) {
        return QStringLiteral("%1:%2").arg(name, message);
    }
    if (!message.isEmpty()) {
        return message;
    }
    if (!name.isEmpty()) {
        return name;
    }
    return QStringLiteral("<unknown>");
}

QString activatableServiceState(const QDBusConnection &bus, const QString &serviceName)
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                       QStringLiteral("/org/freedesktop/DBus"),
                                                       QStringLiteral("org.freedesktop.DBus"),
                                                       QStringLiteral("ListActivatableNames"));
    const QDBusMessage reply = bus.call(call);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        return QStringLiteral("error:%1").arg(dbusErrorText(reply, QStringLiteral("<dbus-error>")));
    }
    if (reply.arguments().isEmpty()) {
        return QStringLiteral("error:<empty-reply>");
    }

    const QStringList names = qdbus_cast<QStringList>(reply.arguments().constFirst());
    return names.contains(serviceName) ? QStringLiteral("true") : QStringLiteral("false");
}

QString dbusServiceDiagnostic(const QDBusConnection &bus, const QString &serviceName)
{
    QStringList parts;
    parts << QStringLiteral("pid=%1").arg(QCoreApplication::applicationPid())
          << QStringLiteral("busConnected=%1").arg(bus.isConnected() ? QStringLiteral("true")
                                                                     : QStringLiteral("false"));

    if (!bus.isConnected()) {
        const QDBusError lastBusError = bus.lastError();
        if (lastBusError.isValid()) {
            parts << QStringLiteral("busError=%1:%2")
                        .arg(lastBusError.name().trimmed(), lastBusError.message().trimmed());
        }
        return parts.join(QStringLiteral(", "));
    }

    QDBusConnectionInterface *dbusIface = bus.interface();
    if (!dbusIface) {
        parts << QStringLiteral("dbusInterface=<null>");
        return parts.join(QStringLiteral(", "));
    }

    const QDBusReply<bool> isRegistered = dbusIface->isServiceRegistered(serviceName);
    const QDBusReply<QString> owner = dbusIface->serviceOwner(serviceName);
    const QDBusReply<uint> servicePid = dbusIface->servicePid(serviceName);

    parts << QStringLiteral("registered=%1").arg(formatReplyState(isRegistered))
          << QStringLiteral("activatable=%1").arg(activatableServiceState(bus, serviceName))
          << QStringLiteral("owner=%1").arg(formatReplyState(owner))
          << QStringLiteral("servicePid=%1").arg(formatReplyState(servicePid));

    const QByteArray busAddress = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    if (!busAddress.isEmpty()) {
        parts << QStringLiteral("dbusAddress=%1").arg(QString::fromUtf8(busAddress));
    }

    return parts.join(QStringLiteral(", "));
}

QByteArray readPipeBytes(int fileDescriptor)
{
    QByteArray buffer;
    buffer.reserve(2 * 1024 * 1024);

    pollfd pollFd;
    pollFd.fd = fileDescriptor;
    pollFd.events = POLLIN;
    pollFd.revents = 0;

    char chunk[16384];
    int attempts = 0;
    while (attempts < kScreenGrabMaxReadAttempts && buffer.size() < kScreenGrabMaxBytes) {
        const int pollResult = ::poll(&pollFd, 1, kScreenGrabPollTimeoutMs);
        if (pollResult == 0) {
            ++attempts;
            continue;
        }
        if (pollResult < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        if (pollFd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            break;
        }

        if (!(pollFd.revents & POLLIN)) {
            ++attempts;
            continue;
        }

        const ssize_t bytesRead = ::read(fileDescriptor, chunk, sizeof(chunk));
        if (bytesRead > 0) {
            buffer.append(chunk, static_cast<int>(bytesRead));
            attempts = 0;
            continue;
        }
        if (bytesRead == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ++attempts;
            continue;
        }
        break;
    }

    return buffer;
}

bool writeBytesToFile(const QString &targetPath, const QByteArray &data)
{
    QSaveFile output(targetPath);
    if (!output.open(QIODevice::WriteOnly)) {
        return false;
    }
    if (output.write(data) != data.size()) {
        output.cancelWriting();
        return false;
    }
    return output.commit();
}

QImage decodeScreenGrabImage(const QByteArray &payload, int width, int height)
{
    if (payload.isEmpty()) {
        return QImage();
    }

    const QImage encodedImage = QImage::fromData(payload);
    if (!encodedImage.isNull()) {
        return encodedImage;
    }

    if (width <= 0 || height <= 0) {
        return QImage();
    }

    const int rgbaBytes = width * height * 4;
    if (payload.size() < rgbaBytes) {
        return QImage();
    }

    const uchar *rawData = reinterpret_cast<const uchar *>(payload.constData());

    QImage bgraImage(rawData, width, height, width * 4, QImage::Format_ARGB32);
    if (!bgraImage.isNull()) {
        return bgraImage.copy();
    }

    QImage rgbaImage(rawData, width, height, width * 4, QImage::Format_RGBA8888);
    if (!rgbaImage.isNull()) {
        return rgbaImage.copy();
    }

    return QImage();
}

} // namespace

ToolExecutionResult ToolExecutor::execute(const ParsedFunctionCall &call) const
{
    if (call.name == QStringLiteral("toggle_flashlight")) {
        return executeFlashlight(call);
    }
    if (call.name == QStringLiteral("take_screenshot")) {
        return executeScreenshot(call);
    }

    ToolExecutionResult result;
    result.errorText = QObject::tr("Unsupported tool: %1.").arg(call.name);
    return result;
}

ToolExecutionResult ToolExecutor::executeFlashlight(const ParsedFunctionCall &call) const
{
    ToolExecutionResult result;
    result.prettyCall = call.prettyText;

    const QDBusConnection bus = QDBusConnection::sessionBus();
    const QString desiredState = call.arguments.value(QStringLiteral("state")).toString();
    QDBusMessage getStateCall = QDBusMessage::createMethodCall(QString::fromLatin1(kFlashlightService),
                                                              QString::fromLatin1(kFlashlightPath),
                                                              QStringLiteral("org.freedesktop.DBus.Properties"),
                                                              QStringLiteral("Get"));
    getStateCall.setArguments(QList<QVariant>()
                              << QString::fromLatin1(kFlashlightInterface)
                              << QStringLiteral("flashlightOn"));
    const QDBusMessage stateReply = bus.call(getStateCall);
    if (stateReply.type() == QDBusMessage::ErrorMessage || stateReply.arguments().isEmpty()) {
        result.errorText = QStringLiteral("%1 [%2]")
                .arg(dbusErrorText(stateReply,
                                   QObject::tr("Failed to read the current flashlight state.")),
                     dbusServiceDiagnostic(bus, QString::fromLatin1(kFlashlightService)));
        return result;
    }

    const QVariant currentStateVariant = qvariant_cast<QDBusVariant>(stateReply.arguments().constFirst()).variant();
    bool currentState = currentStateVariant.toBool();
    const bool shouldToggle = (desiredState == QStringLiteral("toggle"))
            || (desiredState == QStringLiteral("on") && !currentState)
            || (desiredState == QStringLiteral("off") && currentState);

    if (shouldToggle) {
        const QDBusMessage toggleCall = QDBusMessage::createMethodCall(QString::fromLatin1(kFlashlightService),
                                                                       QString::fromLatin1(kFlashlightPath),
                                                                       QString::fromLatin1(kFlashlightInterface),
                                                                       QStringLiteral("toggleFlashlight"));
        const QDBusMessage reply = bus.call(toggleCall);
        const QString error = dbusErrorText(reply, QObject::tr("Flashlight D-Bus call failed."));
        if (!error.isEmpty()) {
            result.errorText = error;
            return result;
        }
    }

    const QDBusMessage updatedStateReply = bus.call(getStateCall);
    if (updatedStateReply.type() != QDBusMessage::ErrorMessage && !updatedStateReply.arguments().isEmpty()) {
        const QVariant updatedStateVariant =
                qvariant_cast<QDBusVariant>(updatedStateReply.arguments().constFirst()).variant();
        currentState = updatedStateVariant.toBool();
    }

    result.resultText = currentState
            ? QObject::tr("Flashlight is on.")
            : QObject::tr("Flashlight is off.");
    return result;
}

ToolExecutionResult ToolExecutor::executeScreenshot(const ParsedFunctionCall &call) const
{
    const QString requestedPath = call.arguments.value(QStringLiteral("path")).toString();
    const QString targetPath = sanitizeScreenshotPath(requestedPath);
    ToolExecutionResult result = executeScreenGrabScreenshot(call, targetPath);
    if (result.errorText.isEmpty()) {
        return result;
    }

    ToolExecutionResult fallback = executeLipstickScreenshot(call, targetPath);
    if (fallback.errorText.isEmpty()) {
        return fallback;
    }

    fallback.errorText = QObject::tr("ScreenGrab1 failed: %1 Lipstick fallback failed: %2")
            .arg(result.errorText, fallback.errorText);
    return fallback;
}

ToolExecutionResult ToolExecutor::executeScreenGrabScreenshot(const ParsedFunctionCall &call,
                                                              const QString &targetPath) const
{
    ToolExecutionResult result;
    result.prettyCall = QStringLiteral("take_screenshot(path=\"%1\", format=\"png\", overwrite=%2)")
            .arg(targetPath,
                 call.arguments.value(QStringLiteral("overwrite")).toBool()
                    ? QStringLiteral("true")
                    : QStringLiteral("false"));

    QDir().mkpath(QFileInfo(targetPath).absolutePath());

    const QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage getWidthCall = QDBusMessage::createMethodCall(QString::fromLatin1(kScreenGrabService),
                                                               QString::fromLatin1(kScreenGrabPath),
                                                               QStringLiteral("org.freedesktop.DBus.Properties"),
                                                               QStringLiteral("Get"));
    getWidthCall.setArguments(QList<QVariant>()
                              << QString::fromLatin1(kScreenGrabInterface)
                              << QStringLiteral("Width"));
    const QDBusMessage widthReply = bus.call(getWidthCall);
    int width = 0;
    if (widthReply.type() != QDBusMessage::ErrorMessage && !widthReply.arguments().isEmpty()) {
        width = qvariant_cast<QDBusVariant>(widthReply.arguments().constFirst()).variant().toInt();
    }

    QDBusMessage getHeightCall = QDBusMessage::createMethodCall(QString::fromLatin1(kScreenGrabService),
                                                                QString::fromLatin1(kScreenGrabPath),
                                                                QStringLiteral("org.freedesktop.DBus.Properties"),
                                                                QStringLiteral("Get"));
    getHeightCall.setArguments(QList<QVariant>()
                               << QString::fromLatin1(kScreenGrabInterface)
                               << QStringLiteral("Height"));
    const QDBusMessage heightReply = bus.call(getHeightCall);
    int height = 0;
    if (heightReply.type() != QDBusMessage::ErrorMessage && !heightReply.arguments().isEmpty()) {
        height = qvariant_cast<QDBusVariant>(heightReply.arguments().constFirst()).variant().toInt();
    }

    QDBusMessage request = QDBusMessage::createMethodCall(QString::fromLatin1(kScreenGrabService),
                                                          QString::fromLatin1(kScreenGrabPath),
                                                          QString::fromLatin1(kScreenGrabInterface),
                                                          QStringLiteral("RequestVideoPipe"));
    request.setArguments(QList<QVariant>() << QVariant::fromValue(QVariantMap()));
    const QDBusMessage reply = bus.call(request);
    if (reply.type() == QDBusMessage::ErrorMessage) {
        result.errorText = QStringLiteral("%1 [%2]")
                .arg(dbusErrorText(reply, QObject::tr("ScreenGrab1 D-Bus call failed.")),
                     dbusServiceDiagnostic(bus, QString::fromLatin1(kScreenGrabService)));
        return result;
    }

    if (reply.arguments().isEmpty()) {
        result.errorText = QStringLiteral("%1 [%2]")
                .arg(QObject::tr("ScreenGrab1 did not return a file descriptor."),
                     dbusServiceDiagnostic(bus, QString::fromLatin1(kScreenGrabService)));
        return result;
    }

    const QDBusUnixFileDescriptor pipeFd =
            qvariant_cast<QDBusUnixFileDescriptor>(reply.arguments().constFirst());
    if (!pipeFd.isValid()) {
        result.errorText = QStringLiteral("%1 [%2]")
                .arg(QObject::tr("ScreenGrab1 returned an invalid file descriptor."),
                     dbusServiceDiagnostic(bus, QString::fromLatin1(kScreenGrabService)));
        return result;
    }

    const int ownedFd = ::dup(pipeFd.fileDescriptor());
    if (ownedFd < 0) {
        result.errorText = QObject::tr("Failed to duplicate ScreenGrab1 file descriptor: %1")
                .arg(QString::fromLocal8Bit(std::strerror(errno)));
        return result;
    }

    const QByteArray payload = readPipeBytes(ownedFd);
    ::close(ownedFd);
    if (payload.isEmpty()) {
        result.errorText = QObject::tr("ScreenGrab1 returned an empty image stream.");
        return result;
    }

    const bool isPng = payload.startsWith(QByteArray::fromHex("89504e470d0a1a0a"));
    if (isPng && writeBytesToFile(targetPath, payload)) {
        result.resultText = QObject::tr("Screenshot saved to %1.").arg(targetPath);
        return result;
    }

    QImage image = decodeScreenGrabImage(payload, width, height);
    if (image.isNull()) {
        result.errorText = QObject::tr("ScreenGrab1 returned an unsupported image stream.");
        return result;
    }

    if (!image.save(targetPath, "PNG")) {
        result.errorText = QObject::tr("Failed to save screenshot to %1.").arg(targetPath);
        return result;
    }

    result.resultText = QObject::tr("Screenshot saved to %1.").arg(targetPath);
    return result;
}

ToolExecutionResult ToolExecutor::executeLipstickScreenshot(const ParsedFunctionCall &call,
                                                            const QString &targetPath) const
{
    ToolExecutionResult result;
    result.prettyCall = QStringLiteral("take_screenshot(path=\"%1\", format=\"png\", overwrite=%2)")
            .arg(targetPath,
                 call.arguments.value(QStringLiteral("overwrite")).toBool()
                    ? QStringLiteral("true")
                    : QStringLiteral("false"));

    QDir().mkpath(QFileInfo(targetPath).absolutePath());

    const QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface(QString::fromLatin1(kScreenshotService),
                         QString::fromLatin1(kScreenshotPath),
                         QString::fromLatin1(kScreenshotInterface),
                         bus);
    if (!iface.isValid()) {
        result.errorText = QStringLiteral("%1 [%2]")
                .arg(dbusInterfaceErrorText(iface,
                                            QObject::tr("Lipstick screenshot service is not available on D-Bus.")),
                     dbusServiceDiagnostic(bus, QString::fromLatin1(kScreenshotService)));
        return result;
    }

    const QDBusMessage reply = iface.call(QStringLiteral("saveScreenshot"), targetPath);
    const QString error = dbusErrorText(reply, QObject::tr("Lipstick screenshot D-Bus call failed."));
    if (!error.isEmpty()) {
        result.errorText = error;
        return result;
    }

    for (int attempt = 0; attempt < 10; ++attempt) {
        if (QFileInfo::exists(targetPath)) {
            break;
        }
        QThread::msleep(100);
    }

    result.resultText = QFileInfo::exists(targetPath)
            ? QObject::tr("Screenshot saved to %1.").arg(targetPath)
            : QObject::tr("Screenshot request sent for %1.").arg(targetPath);
    return result;
}

QString ToolExecutor::sanitizeScreenshotPath(const QString &requestedPath) const
{
    const QString rootDir = screenshotRootDirectory();
    QDir().mkpath(rootDir);

    if (requestedPath.trimmed().isEmpty()) {
        return timestampedScreenshotPath();
    }

    QFileInfo requestedInfo(QDir::cleanPath(requestedPath));
    QString fileName = requestedInfo.fileName();
    if (fileName.isEmpty()) {
        fileName = QStringLiteral("screenshot.png");
    }
    if (!fileName.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        fileName += QStringLiteral(".png");
    }

    const QString normalizedRoot = QDir(rootDir).canonicalPath().isEmpty()
            ? QDir(rootDir).absolutePath()
            : QDir(rootDir).canonicalPath();

    if (requestedInfo.isAbsolute()) {
        const QString absolutePath = QDir::cleanPath(requestedInfo.absoluteFilePath());
        if (absolutePath.startsWith(normalizedRoot + QLatin1Char('/'))
                || absolutePath == normalizedRoot) {
            return absolutePath;
        }
    }

    return QDir(rootDir).absoluteFilePath(fileName);
}

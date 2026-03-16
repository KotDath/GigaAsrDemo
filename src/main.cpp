// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

#include <QQmlContext>
#include <QtQuick>
#include <auroraapp.h>

#include "gigaasrrunner.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> application(Aurora::Application::application(argc, argv));
    application->setOrganizationName(QStringLiteral("ru.auroraos"));
    application->setApplicationName(QStringLiteral("GigaAsrDemo"));

    QScopedPointer<QQuickView> view(Aurora::Application::createView());

    GigaAsrRunner *gigaAsrRunner = new GigaAsrRunner(view.data());
    view->rootContext()->setContextProperty("gigaAsrRunner", QVariant::fromValue(gigaAsrRunner));

    view->setSource(Aurora::Application::pathTo(QStringLiteral("qml/GigaAsrDemo.qml")));
    view->show();

    return application->exec();
}

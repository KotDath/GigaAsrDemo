// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.0
import Sailfish.Silica 1.0

CoverBackground {
    objectName: "defaultCover"

    CoverTemplate {
        objectName: "applicationCover"
        primaryText: qsTr("GigaAsrDemo")
        secondaryText: gigaAsrRunner && gigaAsrRunner.isRecording
                       ? qsTr("Recording")
                       : qsTr("Ready")
        icon {
            source: Qt.resolvedUrl("../icons/GigaAsrDemo.svg")
            sourceSize { width: icon.width; height: icon.height }
        }
    }
}

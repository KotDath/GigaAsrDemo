// SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
// SPDX-License-Identifier: BSD-3-Clause

import QtQuick 2.0
import Sailfish.Silica 1.0

Page {
    objectName: "mainPage"
    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: contentColumn.height + Theme.paddingLarge

        Column {
            id: contentColumn
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("Speech to text")
            }

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.horizontalPageMargin
                anchors.rightMargin: Theme.horizontalPageMargin
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                text: gigaAsrRunner && gigaAsrRunner.transcript.length > 0
                      ? gigaAsrRunner.transcript
                      : qsTr("Press the button and speak")
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Large
                running: gigaAsrRunner
                         && (gigaAsrRunner.isTranscribing
                             || gigaAsrRunner.isAnalyzing
                             || gigaAsrRunner.isExecuting)
                visible: running
            }

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.horizontalPageMargin
                anchors.rightMargin: Theme.horizontalPageMargin
                color: Theme.primaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                visible: gigaAsrRunner && gigaAsrRunner.recognizedAction.length > 0
                text: gigaAsrRunner
                      ? qsTr("Action: %1").arg(gigaAsrRunner.recognizedAction)
                      : ""
            }

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.horizontalPageMargin
                anchors.rightMargin: Theme.horizontalPageMargin
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                visible: gigaAsrRunner && gigaAsrRunner.executionResult.length > 0
                text: gigaAsrRunner ? gigaAsrRunner.executionResult : ""
            }

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.horizontalPageMargin
                anchors.rightMargin: Theme.horizontalPageMargin
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeExtraSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                text: gigaAsrRunner ? gigaAsrRunner.statusText : ""
                visible: text.length > 0
            }

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.horizontalPageMargin
                anchors.rightMargin: Theme.horizontalPageMargin
                color: Theme.errorColor
                font.pixelSize: Theme.fontSizeExtraSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                text: gigaAsrRunner ? gigaAsrRunner.errorText : ""
                visible: text.length > 0
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - Theme.horizontalPageMargin * 2
                text: gigaAsrRunner && gigaAsrRunner.isRecording
                      ? qsTr("Stop recording")
                      : qsTr("Start recording")
                enabled: gigaAsrRunner
                         && gigaAsrRunner.isModelLoaded
                         && !gigaAsrRunner.isTranscribing
                         && !gigaAsrRunner.isAnalyzing
                         && !gigaAsrRunner.isExecuting
                onClicked: gigaAsrRunner.toggleRecording()
            }

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.horizontalPageMargin
                anchors.rightMargin: Theme.horizontalPageMargin
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                visible: gigaAsrRunner && !gigaAsrRunner.isModelLoaded
                text: qsTr("The speech model is loading. The button will become active when loading finishes.")
            }

            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.horizontalPageMargin
                anchors.rightMargin: Theme.horizontalPageMargin
                color: Theme.secondaryColor
                font.pixelSize: Theme.fontSizeExtraSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                visible: gigaAsrRunner
                         && gigaAsrRunner.isModelLoaded
                         && !gigaAsrRunner.isCommandModelLoaded
                text: qsTr("The command model is loading or unavailable. Speech will still be transcribed.")
            }
        }

        VerticalScrollDecorator {}
    }
}

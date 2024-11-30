import QtQuick

Window {
    visible: true
    flags: Qt.FramelessWindowHint
    width: 500; height: 500

    property var margins: SafeArea.margins
    SafeArea.additionalMargins {
        top: 10; left: 20
        bottom: 30; right: 40
    }

    Item {
        objectName: "additionalItem"
        anchors.fill: parent
        SafeArea.additionalMargins {
            left: 120; top: 110
            right: 140; bottom: 130
        }
        property var margins: SafeArea.margins

        Item {
            objectName: "additionalChild"
            anchors {
                fill: parent; margins: 3
            }
            property var margins: SafeArea.margins
        }
    }

    Item {
        objectName: "additionalSibling"
        anchors.fill: parent
        property var margins: SafeArea.margins
    }
}

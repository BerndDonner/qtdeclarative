// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick.Templates as T
import QtQuick.Controls

ApplicationWindow {
    width: 400
    height: 400

    property alias contextMenu: contextMenu

    Menu {
        id: contextMenu
        objectName: "menu"
        requestNative: true

        Action {
            objectName: text
            text: "action1"
            shortcut: "A"
        }

        Action {
            objectName: text
            text: "action2"
            shortcut: "B"
        }

        Menu {
            id: subMenu
            title: "subMenu"
            objectName: title
            // TODO: remove me when the defaults are true
            requestNative: true

            Action {
                objectName: text
                text: "subAction1"
                shortcut: "1"
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: contextMenu.popup()
    }
}

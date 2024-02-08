// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root
    property bool requestNative: false
    property bool menuBarVisible: true
    property alias fileMenu: fileMenu
    property alias contents: contents

    width: 400
    height: 400
    visible: true

    header: MenuBar {
        requestNative: root.requestNative
        visible: root.menuBarVisible
        Menu {
            id: fileMenu
            title: "&File"
            MenuItem { text: "&Open..." }
            MenuItem { text: "&Save" }
            MenuItem { text: "Save &As..." }
            MenuSeparator { }
            MenuItem { text: "&Quit" }
        }
        Menu {
            title: "&Edit"
            MenuItem { text: "&Cut" }
            MenuItem { text: "&Copy" }
            MenuItem { text: "&Paste" }
        }
        Menu {
            title: "&View"
            Menu {
                title: "&Alignment"
                Menu {
                    title: "&Horizontal"
                    MenuItem { text: "&Left" }
                    MenuItem { text: "&Center" }
                    MenuItem { text: "&Right" }
                }
                Menu {
                    title: "&Vertical"
                    MenuItem { text: "&Top" }
                    MenuItem { text: "&Center" }
                    MenuItem { text: "&Bottom" }
                }
            }
        }

        Menu {
            title: "&Help"
            MenuItem { text: "&About" }
        }
    }

    Rectangle {
        id: contents
        anchors.fill: parent
        color: "green"
    }
}

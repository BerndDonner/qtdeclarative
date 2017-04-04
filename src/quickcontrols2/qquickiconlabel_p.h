/****************************************************************************
**
** Copyright (C) 2017 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the Qt Quick Controls 2 module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef QQUICKICONLABEL_P_H
#define QQUICKICONLABEL_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtQuick/qquickitem.h>
#include <QtQuickControls2/private/qtquickcontrols2global_p.h>

QT_BEGIN_NAMESPACE

class QQuickIconLabelPrivate;

class Q_QUICKCONTROLS2_PRIVATE_EXPORT QQuickIconLabel : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QQuickItem *icon READ icon WRITE setIcon FINAL)
    Q_PROPERTY(QQuickItem *text READ text WRITE setText FINAL)
    Q_PROPERTY(Display display READ display WRITE setDisplay FINAL)
    Q_PROPERTY(qreal spacing READ spacing WRITE setSpacing)
    Q_PROPERTY(bool mirrored READ isMirrored WRITE setMirrored)
    Q_PROPERTY(qreal topPadding READ topPadding WRITE setTopPadding RESET resetTopPadding)
    Q_PROPERTY(qreal leftPadding READ leftPadding WRITE setLeftPadding RESET resetLeftPadding)
    Q_PROPERTY(qreal rightPadding READ rightPadding WRITE setRightPadding RESET resetRightPadding)
    Q_PROPERTY(qreal bottomPadding READ bottomPadding WRITE setBottomPadding RESET resetBottomPadding)

public:
    enum Display {
        IconOnly,
        TextOnly,
        TextBesideIcon,
        TextUnderIcon // unused, but reserved for future use
    };
    Q_ENUM(Display)

    explicit QQuickIconLabel(QQuickItem *parent = nullptr);
    ~QQuickIconLabel();

    QQuickItem *icon() const;
    void setIcon(QQuickItem *icon);

    QQuickItem *text() const;
    void setText(QQuickItem *text);

    Display display() const;
    void setDisplay(Display display);

    qreal spacing() const;
    void setSpacing(qreal spacing);

    bool isMirrored() const;
    void setMirrored(bool mirrored);

    qreal topPadding() const;
    void setTopPadding(qreal padding);
    void resetTopPadding();

    qreal leftPadding() const;
    void setLeftPadding(qreal padding);
    void resetLeftPadding();

    qreal rightPadding() const;
    void setRightPadding(qreal padding);
    void resetRightPadding();

    qreal bottomPadding() const;
    void setBottomPadding(qreal padding);
    void resetBottomPadding();

protected:
    void componentComplete() override;
    void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    Q_DISABLE_COPY(QQuickIconLabel)
    Q_DECLARE_PRIVATE(QQuickIconLabel)
};

QT_END_NAMESPACE

QML_DECLARE_TYPE(QQuickIconLabel)

#endif // QQUICKICONLABEL_P_H

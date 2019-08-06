/****************************************************************************
**
** Copyright (C) 2020 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the test suite of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QtTest/qtest.h>
#include <QtTest/QSignalSpy>

#include <QtQuick/private/qquickpalette_p.h>
#include <QtQuick/private/qquickabstractpaletteprovider_p.h>
#include <QtQuick/private/qquickpalettecolorprovider_p.h>

class tst_QQuickPalette : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void resolvingColor();
    void resolvingColor_data();

    void newColorSubgroup();
    void newColorSubgroup_data();

    void onlyRespectiveColorSubgroupChangedAfterAssigment();

    void paletteChangedWhenColorGroupChanged();

    void createDefault();

    void changeCurrentColorGroup();

    void inheritColor();

    void inheritCurrentColor();

    void overrideColor();

    void resolveColor();

    void createFromQtPalette();
    void convertToQtPalette();
};

using GroupGetter = QQuickColorGroup* (QQuickPalette::* )() const;
Q_DECLARE_METATYPE(GroupGetter);

void tst_QQuickPalette::resolvingColor()
{
    QFETCH(QPalette::ColorGroup, colorGroup);
    QFETCH(GroupGetter, getter);

    QQuickPalette p;
    p.setWindowText(Qt::red);

    auto g = (p.*getter)();

    QVERIFY(g);

    g->setWindowText(Qt::green);
    p.setCurrentGroup(colorGroup);

    QCOMPARE(p.windowText(), Qt::green);
}

void tst_QQuickPalette::resolvingColor_data()
{
    QTest::addColumn<QPalette::ColorGroup>("colorGroup");
    QTest::addColumn<GroupGetter>("getter");

    QTest::addRow("Inactive") << QPalette::Inactive << &QQuickPalette::inactive;
    QTest::addRow("Disabled") << QPalette::Disabled << &QQuickPalette::disabled;
}

using GroupSetter   = void (QQuickPalette::* )(QQuickColorGroup *);
Q_DECLARE_METATYPE(GroupSetter);

using GroupNotifier = void (QQuickPalette::* )();
Q_DECLARE_METATYPE(GroupNotifier);

void tst_QQuickPalette::newColorSubgroup()
{
    QFETCH(GroupGetter, getter);
    QFETCH(GroupSetter, setter);
    QFETCH(GroupNotifier, notifier);

    {
        QQuickPalette p;
        p.fromQPalette(Qt::blue);

        auto defaultGroup = (p.*getter)();
        QVERIFY(defaultGroup);

        QSignalSpy subgroupChanged(&p, notifier);
        QSignalSpy paletteChanged(&p, &QQuickPalette::changed);

        QQuickPalette anotherPalette;
        anotherPalette.fromQPalette(Qt::red);
        (p.*setter)((anotherPalette.*getter)());

        QCOMPARE(subgroupChanged.count(), 1);
        QCOMPARE(paletteChanged.count(), 1);
    }
}

void tst_QQuickPalette::newColorSubgroup_data()
{
    QTest::addColumn<GroupGetter>("getter");
    QTest::addColumn<GroupSetter>("setter");
    QTest::addColumn<GroupNotifier>("notifier");

    QTest::addRow("Active")   << &QQuickPalette::active   << &QQuickPalette::setActive
                              << &QQuickPalette::activeChanged;
    QTest::addRow("Inactive") << &QQuickPalette::inactive << &QQuickPalette::setInactive
                              << &QQuickPalette::inactiveChanged;
    QTest::addRow("Disabled") << &QQuickPalette::disabled << &QQuickPalette::setDisabled
                              << &QQuickPalette::disabledChanged;
}

void tst_QQuickPalette::onlyRespectiveColorSubgroupChangedAfterAssigment()
{
    QQuickPalette palette;
    palette.setWindow(Qt::red);

    QQuickPalette anotherPalette;
    anotherPalette.active()->setWindow(Qt::green);

    // Only active subgroup should be copied
    palette.setActive(anotherPalette.active());

    QCOMPARE(palette.active()->window(), Qt::green);
    QCOMPARE(palette.disabled()->window(), Qt::red);
    QCOMPARE(palette.inactive()->window(), Qt::red);
}

void tst_QQuickPalette::paletteChangedWhenColorGroupChanged()
{
    QQuickPalette p;
    QSignalSpy sp(&p, &QQuickPalette::changed);

    p.active()->setMid(Qt::red);
    p.inactive()->setMid(Qt::green);
    p.disabled()->setMid(Qt::blue);

    QCOMPARE(sp.count(), 3);
}

void tst_QQuickPalette::createDefault()
{
    QQuickPalette palette;

    QCOMPARE(palette.currentColorGroup(), QPalette::Active);
    QCOMPARE(palette.active()->groupTag(), QPalette::Active);
    QCOMPARE(palette.inactive()->groupTag(), QPalette::Inactive);
    QCOMPARE(palette.disabled()->groupTag(), QPalette::Disabled);
}

void tst_QQuickPalette::changeCurrentColorGroup()
{
    QQuickPalette palette;

    QSignalSpy ss(&palette, &QQuickPalette::changed);
    palette.setCurrentGroup(QPalette::Disabled);

    QCOMPARE(palette.currentColorGroup(), QPalette::Disabled);
    QCOMPARE(ss.count(), 1);
}

void tst_QQuickPalette::inheritColor()
{
    QQuickPalette parentPalette;
    parentPalette.setWindowText(Qt::red);

    QQuickPalette quickPalette;
    quickPalette.inheritPalette(parentPalette.toQPalette());

    QCOMPARE(quickPalette.windowText(), Qt::red);

    QQuickPalette childQuickPalette;
    childQuickPalette.inheritPalette(quickPalette.toQPalette());

    QCOMPARE(childQuickPalette.windowText(), Qt::red);
}

void tst_QQuickPalette::inheritCurrentColor()
{
    QQuickPalette parentPalette;
    parentPalette.setWindowText(Qt::green);
    parentPalette.disabled()->setWindowText(Qt::red);


    QQuickPalette quickPalette;
    quickPalette.inheritPalette(parentPalette.toQPalette());
    quickPalette.setCurrentGroup(QPalette::Disabled);

    QCOMPARE(quickPalette.windowText(), Qt::red);
}

void tst_QQuickPalette::overrideColor()
{
    QQuickPalette rootPalette;
    rootPalette.setWindowText(Qt::red);

    QQuickPalette palette;
    palette.inheritPalette(rootPalette.toQPalette());
    palette.setWindowText(Qt::yellow);

    QCOMPARE(palette.windowText(), Qt::yellow);

    QQuickPalette childPalette;
    childPalette.inheritPalette(palette.toQPalette());
    childPalette.disabled()->setWindowText(Qt::green);

    // Color is not set for the current group. Use parent color
    QCOMPARE(childPalette.windowText(), Qt::yellow);

    // Change current group to use color, specified for this particular group
    childPalette.setCurrentGroup(QPalette::Disabled);

    QCOMPARE(childPalette.windowText(), Qt::green);
}

void tst_QQuickPalette::resolveColor()
{
    QQuickPalette palette;
    palette.setWindowText(Qt::red);

    // Disabled color should be red, because disabled palette is not specified
    palette.setCurrentGroup(QPalette::Disabled);
    QCOMPARE(palette.windowText(), Qt::red);

    // Color is changed for disabled palette, because current color group is QPalette::Disabled
    palette.disabled()->setWindowText(Qt::yellow);
    QCOMPARE(palette.windowText(), Qt::yellow);
    QCOMPARE(palette.disabled()->windowText(), Qt::yellow);

    // Change color group back to active
    palette.setCurrentGroup(QPalette::Active);
    QCOMPARE(palette.windowText(), Qt::red);
}

void tst_QQuickPalette::createFromQtPalette()
{
    QQuickPalette palette;
    QPalette somePalette(Qt::red);

    QSignalSpy sp(&palette, &QQuickColorGroup::changed);

    palette.fromQPalette(QPalette());
    QCOMPARE(sp.count(), 0);

    palette.fromQPalette(somePalette);
    QCOMPARE(sp.count(), 1);
}

void tst_QQuickPalette::convertToQtPalette()
{
    QQuickPalette palette;

    QPalette somePalette(Qt::red);
    palette.fromQPalette(somePalette);

    auto pp = palette.paletteProvider();
    QVERIFY(pp);

    QCOMPARE(palette.toQPalette(), somePalette.resolve(pp->defaultPalette()));
}

QTEST_MAIN(tst_QQuickPalette)

#include "tst_qquickpalette.moc"

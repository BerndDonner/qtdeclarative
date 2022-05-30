/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the test suite of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:GPL-EXCEPT$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include <QtTest/QtTest>
#include <QProcess>
#include <QLibraryInfo>
#include <qjstest/test262runner.h>

class tst_EcmaScriptTests : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void runInterpreted();
    void runJitted();

private:
    static QLoggingCategory::CategoryFilter priorFilter;
    static void filterCategories(QLoggingCategory *category);
};
QLoggingCategory::CategoryFilter tst_EcmaScriptTests::priorFilter = nullptr;

static inline bool isNoise(QByteArrayView name)
{
#ifdef QT_V4_WANT_ES262_WARNINGS
    return false;
#else
    const QByteArrayView noisy("qt.qml.compiler");
    return name.startsWith(noisy) && (name.size() <= noisy.size() || name[noisy.size()] == '.');
#endif
}

void tst_EcmaScriptTests::filterCategories(QLoggingCategory *category)
{
    if (priorFilter)
        priorFilter(category);

    if (isNoise(category->categoryName())) {
        category->setEnabled(QtDebugMsg, false);
        category->setEnabled(QtWarningMsg, false);
    }
}

void tst_EcmaScriptTests::initTestCase()
{
    /* Suppress lcQmlCompiler's "qt.qml.compiler" warnings; we aren't in a
       position to fix test262's many warnings and they flood messages so we
       didn't get to see actual failures unless we passed -maxwarnings with a
       huge value on the command-line (resulting in huge log output).
    */
    priorFilter = QLoggingCategory::installFilter(filterCategories);
}

void tst_EcmaScriptTests::cleanupTestCase()
{
    QLoggingCategory::installFilter(priorFilter);
}

void tst_EcmaScriptTests::runInterpreted()
{
#if defined(Q_PROCESSOR_X86_64)
    QDir::setCurrent(QLatin1String(SRCDIR));
    Test262Runner runner(QString(), "test262");
    runner.setFlags(Test262Runner::ForceBytecode|Test262Runner::WithTestExpectations|Test262Runner::Parallel|Test262Runner::Verbose);
    bool result = runner.run();
    QVERIFY(result);
#endif
}

void tst_EcmaScriptTests::runJitted()
{
#if defined(Q_PROCESSOR_X86_64)
    QDir::setCurrent(QLatin1String(SRCDIR));
    Test262Runner runner(QString(), "test262");
    runner.setFlags(Test262Runner::ForceJIT|Test262Runner::WithTestExpectations|Test262Runner::Parallel|Test262Runner::Verbose);
    bool result = runner.run();
    QVERIFY(result);
#endif
}

QTEST_GUILESS_MAIN(tst_EcmaScriptTests)

#include "tst_ecmascripttests.moc"


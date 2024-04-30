// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only

#include "tst_qmlls_highlighting.h"

#include <QtQml/private/qqmljsengine_p.h>
#include <QtQml/private/qqmljslexer_p.h>
#include <QtQml/private/qqmljsparser_p.h>
#include <QtQmlDom/private/qqmldomitem_p.h>
#include <QtQmlDom/private/qqmldomtop_p.h>
#include <QtQmlLS/private/qqmlsemantictokens_p.h>

#include <QtLanguageServer/private/qlanguageserverspectypes_p.h>

#include <qlist.h>

using namespace QLspSpecification;

tst_qmlls_highlighting::tst_qmlls_highlighting()
    : QQmlDataTest(QT_QMLLS_HIGHLIGHTS_DATADIR) , m_highlightingDataDir(QT_QMLLS_HIGHLIGHTS_DATADIR + "/highlights"_L1)
{
}

// Token encoding as in:
// https://microsoft.github.io/language-server-protocol/specifications/specification-3-16/#textDocument_semanticTokens
void tst_qmlls_highlighting::encodeSemanticTokens_data()
{
    QTest::addColumn<Highlights>("highlights");
    QTest::addColumn<QList<int>>("expectedMemoryLayout");

    {
        Highlights c;
        c.highlights().insert(0, Token());
        QTest::addRow("empty-token-single") << c << QList {0, 0, 0, 0, 0};
    }
    {
        Highlights c;
        QQmlJS::SourceLocation loc(0, 1, 1, 1);
        c.highlights().insert(0, Token(loc, 0, 0));
        QTest::addRow("single-token") << c << QList {0, 0, 1, 0, 0};
    }
    {
        Highlights c;
        Token t1(QQmlJS::SourceLocation(0, 1, 1, 1), 0, 0);
        Token t2(QQmlJS::SourceLocation(1, 1, 3, 3), 0, 0);
        c.highlights().insert(t1.offset, t1);
        c.highlights().insert(t2.offset, t2);
        QTest::addRow("different-lines") << c << QList {0, 0, 1, 0, 0, 2, 2, 1, 0, 0};
    }
    {
        Highlights c;
        Token t1(QQmlJS::SourceLocation(0, 1, 1, 1), 0, 0);
        Token t2(QQmlJS::SourceLocation(1, 1, 1, 3), 0, 0);
        c.highlights().insert(t1.offset, t1);
        c.highlights().insert(t2.offset, t2);
        QTest::addRow("same-line-different-column") << c << QList {0, 0, 1, 0, 0, 0, 2, 1, 0, 0};
    }
    {
        Highlights c;
        Token t1(QQmlJS::SourceLocation(0, 1, 1, 1), 1, 0);
        c.highlights().insert(t1.offset, t1);
        QTest::addRow("token-type") << c << QList {0, 0, 1, 1, 0};
    }
    {
        Highlights c;
        Token t1(QQmlJS::SourceLocation(0, 1, 1, 1), 1, 1);
        c.highlights().insert(t1.offset, t1);
        QTest::addRow("token-modifier") << c << QList {0, 0, 1, 1, 1};
    }
}

void tst_qmlls_highlighting::encodeSemanticTokens()
{
    QFETCH(Highlights, highlights);
    QFETCH(QList<int>, expectedMemoryLayout);
    const auto encoded = HighlightingUtils::encodeSemanticTokens(highlights);
    QCOMPARE(encoded, expectedMemoryLayout);
}

struct LineLength
{
    quint32 startLine;
    quint32 length;
};

void tst_qmlls_highlighting::sourceLocationsFromMultiLineToken_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QList<LineLength>>("expectedLines");

    QTest::addRow("multilineComment1") << R"("line 1
line 2
line 3 ")" << QList{ LineLength{ 1, 7 }, LineLength{ 2, 6 }, LineLength{ 3, 8 } };

    QTest::addRow("prePostNewlines") <<
            R"("

")" << QList{ LineLength{ 1, 1 }, LineLength{ 2, 0 }, LineLength{ 3, 1 } };
    QTest::addRow("windows-newline")
            << QString::fromUtf8("\"test\r\nwindows\r\nnewline\"")
            << QList{ LineLength{ 1, 5 }, LineLength{ 2, 7 }, LineLength{ 3, 8 } };
}

void tst_qmlls_highlighting::sourceLocationsFromMultiLineToken()
{
    QFETCH(QString, source);
    QFETCH(QList<LineLength>, expectedLines);
    using namespace QQmlJS::AST;

    QQmlJS::Engine jsEngine;
    QQmlJS::Lexer lexer(&jsEngine);
    lexer.setCode(source, 1, true);
    QQmlJS::Parser parser(&jsEngine);
    parser.parseExpression();
    const auto expression = parser.expression();

    auto *literal = QQmlJS::AST::cast<QQmlJS::AST::StringLiteral *>(expression);
    const auto locs =
            HighlightingUtils::sourceLocationsFromMultiLineToken(source, literal->literalToken);

    [&]() {
        QCOMPARE(locs.size(), expectedLines.size());

        for (auto i = 0; i < locs.size(); ++i) {
            QCOMPARE(locs[i].startLine, expectedLines[i].startLine);
            QCOMPARE(locs[i].length, expectedLines[i].length);
        }
    }();

    if (QTest::currentTestFailed()) {

        qDebug() << "Actual locations";
        for (auto i = 0; i < locs.size(); ++i) {
            qDebug() << "Startline :" << locs[i].startLine << "Length " << locs[i].length;
        }

        qDebug() << "Expected locations";
        for (auto i = 0; i < expectedLines.size(); ++i) {
            qDebug() << "Startline :" << expectedLines[i].startLine
                     << "Length :" << expectedLines[i].length;
        }
    }
}

void tst_qmlls_highlighting::highlights_data()
{
    using namespace QQmlJS::Dom;
    QTest::addColumn<DomItem>("fileItem");
    QTest::addColumn<Token>("expectedHighlightedToken");

    const auto fileObject = [](const QString &filePath){
        QFile f(filePath);
        DomItem file;
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return file;
        QString code = f.readAll();
        auto envPtr = DomEnvironment::create(
                QStringList(),
                QQmlJS::Dom::DomEnvironment::Option::SingleThreaded
                        | QQmlJS::Dom::DomEnvironment::Option::NoDependencies);
        envPtr->loadFile(FileToLoad::fromMemory(envPtr, filePath, code),
                         [&file](Path, const DomItem &, const DomItem &newIt) {
                             file = newIt.fileObject();
                         });
        envPtr->loadPendingDependencies();
        return file;
    };

    { // Comments
        const auto filePath = m_highlightingDataDir + "/comments.qml";
        const auto fileItem = fileObject(filePath);
        // Copyright (C) 2023 The Qt Company Ltd.
        QTest::addRow("single-line-1")
                << fileItem
                << Token(QQmlJS::SourceLocation(0, 41, 1, 1), int(SemanticTokenTypes::Comment), 0);

        /* single line comment    */
        QTest::addRow("single-line-2") << fileItem
                                       << Token(QQmlJS::SourceLocation(162, 28, 9, 1),
                                                int(SemanticTokenTypes::Comment), 0);

        // Multiline comments are split into multiple locations
        QTest::addRow("multiline-first-line")
                << fileItem
                << Token(QQmlJS::SourceLocation(133, 2, 5, 1), int(SemanticTokenTypes::Comment), 0);
        QTest::addRow("multiline-second-line") << fileItem
                                               << Token(QQmlJS::SourceLocation(136, 21, 6, 1),
                                                        int(SemanticTokenTypes::Comment), 0);
        QTest::addRow("multiline-third-line")
                << fileItem
                << Token(QQmlJS::SourceLocation(158, 2, 7, 1), int(SemanticTokenTypes::Comment), 0);

        // Comments Inside Js blocks
        QTest::addRow("inside-js") << fileItem
                                   << Token(QQmlJS::SourceLocation(232, 5, 13, 9),
                                            int(SemanticTokenTypes::Comment), 0);
    }
}

void tst_qmlls_highlighting::highlights()
{
    using namespace QQmlJS::Dom;
    QFETCH(DomItem, fileItem);
    QFETCH(Token, expectedHighlightedToken);

    Highlights h;
    HighlightingVisitor hv(h);

    fileItem.visitTree(QQmlJS::Dom::Path(), hv, VisitOption::Default, emptyChildrenVisitor,
                   emptyChildrenVisitor);

    const auto highlights = h.highlights();
    QVERIFY(highlights.contains(expectedHighlightedToken.offset));
    QCOMPARE(highlights.value(expectedHighlightedToken.offset), expectedHighlightedToken);
}

QTEST_MAIN(tst_qmlls_highlighting)

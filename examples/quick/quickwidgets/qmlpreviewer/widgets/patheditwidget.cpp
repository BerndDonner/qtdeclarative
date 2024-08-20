// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "patheditwidget.h"
#include "../states/statecontroller.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QToolButton>

PathEditWidget::PathEditWidget(QWidget *parent)
    : QWidget{parent}
    , m_lineEdit{new QLineEdit}
    , m_toolButton{new QToolButton}
{
    initUI();

    setupConnections();
}

QString PathEditWidget::filePath() const
{
    return m_lineEdit->text();
}

QString PathEditWidget::saveFilePath()
{
    const QString filePath = QFileDialog::getSaveFileName(this, tr("Save File"), m_lineEdit->text(),
                                                          tr("QML Files (*.qml)"));
    if (!filePath.endsWith(".qml", Qt::CaseInsensitive))
        // TODO: show warning
        return {};
    if (!filePath.isNull())
        setFilePath(filePath);
    return filePath;
}

void PathEditWidget::openFilePath()
{
    const QString filePath = QFileDialog::getOpenFileName(this, tr("Open QML File"), QString{},
                                                          tr("QML Files (*.qml)"));
    if (!filePath.isNull())
        setFilePath(filePath);
}

void PathEditWidget::initUI()
{
    QHBoxLayout *layout = new QHBoxLayout;
    layout->addWidget(m_lineEdit, 1);
    layout->addWidget(m_toolButton);
    layout->setContentsMargins(0,0,0,0);
    setLayout(layout);

    m_lineEdit->setPlaceholderText(tr("File Path"));
    m_lineEdit->setReadOnly(true);

    m_toolButton->setText(tr("..."));
}

void PathEditWidget::setupConnections()
{
    connect(StateController::instance(), &StateController::stateChanged, this,
            &PathEditWidget::onAppStateChanged);

    connect(m_toolButton, &QToolButton::clicked, this, &PathEditWidget::openFileRequested);
}

void PathEditWidget::setFilePath(const QString &filePath)
{
    if (m_lineEdit->text() == filePath)
        return;

    m_lineEdit->setText(filePath);
    emit fileSelected(filePath);
}

void PathEditWidget::onAppStateChanged(int oldState, int newState)
{
    Q_UNUSED(oldState);

    switch (newState) {
    case StateController::InitState:
        m_lineEdit->setText("");
        break;

    case StateController::NewState:
    case StateController::OpenState:
        if (const QString path = StateController::instance()->filePath(); m_lineEdit->text() != path)
            m_lineEdit->setText(path);
        break;

    default:
        break;
    }
}
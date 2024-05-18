// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#ifndef QANDROIDTYPECONVERTER_P_H
#define QANDROIDTYPECONVERTER_P_H

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.

#include <QtQuick/private/qandroidtypes_p.h>

#include <QtCore/qjniobject.h>
#include <QtCore/qjnienvironment.h>
#include <QtCore/qjnitypes.h>

QT_BEGIN_NAMESPACE

namespace QAndroidTypeConverter
{
    [[maybe_unused]] static QVariant toQVariant(const QJniObject &object)
    {
        if (!object.isValid())
            return QVariant{};
        const QByteArray classname(object.className());

        if (classname == QtJniTypes::Traits<QtJniTypes::String>::className())
            return object.toString();
        else if (classname == QtJniTypes::Traits<QtJniTypes::Integer>::className())
            return object.callMethod<jint>("intValue");
        else if (classname == QtJniTypes::Traits<QtJniTypes::Double>::className())
            return object.callMethod<jdouble>("doubleValue");
        else if (classname == QtJniTypes::Traits<QtJniTypes::Float>::className())
            return object.callMethod<jfloat>("floatValue");
        else if (classname == QtJniTypes::Traits<QtJniTypes::Boolean>::className())
            return object.callMethod<jboolean>("booleanValue");

        return QVariant{};
    }

    [[maybe_unused]] Q_REQUIRED_RESULT static jobject toJavaObject(const QVariant &var, JNIEnv *env)
    {
        Q_ASSERT(env);
        switch (var.typeId()) {
        case QMetaType::Type::Int:
            return env->NewLocalRef(QJniObject::construct<QtJniTypes::Integer>(
                                            get<int>(var))
                                            .object());
        case QMetaType::Type::Double:
            return env->NewLocalRef(QJniObject::construct<QtJniTypes::Double>(
                                            get<double>(var))
                                            .object());
        case QMetaType::Type::Float:
            return env->NewLocalRef(QJniObject::construct<QtJniTypes::Float>(
                                            get<float>(var))
                                            .object());
        case QMetaType::Type::Bool:
            return env->NewLocalRef(QJniObject::construct<QtJniTypes::Boolean>(
                                            get<bool>(var))
                                            .object());
        case QMetaType::Type::QString:
            return env->NewLocalRef(
                    QJniObject::fromString(get<QString>(var)).object());
        }
        return nullptr;
    }
};

QT_END_NAMESPACE

#endif // QANDROIDTYPECONVERTER_P_H
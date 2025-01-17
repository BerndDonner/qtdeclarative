// Copyright (C) 2021 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qqmljsstoragegeneralizer_p.h"

QT_BEGIN_NAMESPACE

/*!
 * \internal
 * \class QQmlJSStorageGeneralizer
 *
 * The QQmlJSStorageGeneralizer is a compile pass that changes all type
 * annotations and the function signature to use a generic storage type like
 * QVariant or QObject*. This is necessary if we cannot rely on the original
 * type to be immediately accessible, for example because we cannot include its
 * header.
 *
 * QQmlJSStorageGeneralizer does not have to use the byte code at all but
 * operates only on the annotations and the function description.
 */

QQmlJSCompilePass::BlocksAndAnnotations QQmlJSStorageGeneralizer::run(Function *function)
{
    m_function = function;

    if (QQmlJSRegisterContent &returnType = function->returnType; returnType.isValid()) {
        if (QQmlJSScope::ConstPtr stored = m_typeResolver->genericType(
                    returnType.storedType(), QQmlJSTypeResolver::ComponentIsGeneric::Yes)) {
            m_pool->generalizeType(returnType.storage(), stored);
        } else {
            addError(QStringLiteral("Cannot store the return type %1.")
                     .arg(returnType.storedType()->internalName()));
            return {};
        }
    }

    const auto transformRegister = [&](QQmlJSRegisterContent &content) {
        QQmlJSRegisterContent specific = content.storage();
        if (specific.isValid())
            m_typeResolver->generalizeType(specific);
    };

    const auto transformRegisters = [&](VirtualRegisters &registers) {
        for (auto j = registers.begin(), jEnd = registers.end(); j != jEnd; ++j) {
            QQmlJSRegisterContent &content = j.value().content;
            transformRegister(content);
        }
    };

    for (QQmlJSRegisterContent &argument : function->argumentTypes) {
        Q_ASSERT(argument.isValid());
        transformRegister(argument);
    }

    for (auto i = m_annotations.begin(), iEnd = m_annotations.end(); i != iEnd; ++i) {
        transformRegister(i->second.changedRegister);
        transformRegisters(i->second.typeConversions);
        transformRegisters(i->second.readRegisters);
    }

    return { std::move(m_basicBlocks), std::move(m_annotations) };
}

QT_END_NAMESPACE

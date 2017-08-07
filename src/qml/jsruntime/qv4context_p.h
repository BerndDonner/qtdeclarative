/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the QtQml module of the Qt Toolkit.
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
#ifndef QMLJS_ENVIRONMENT_H
#define QMLJS_ENVIRONMENT_H

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

#include "qv4global_p.h"
#include "qv4managed_p.h"

QT_BEGIN_NAMESPACE

class QObject;
class QQmlContextData;

namespace QV4 {

namespace CompiledData {
struct CompilationUnitBase;
struct Function;
}

struct Function;
struct Identifier;
struct CallContext;
struct CatchContext;
struct QmlContext;
struct QQmlContextWrapper;

// Attention: Make sure that this structure is the same size on 32-bit and 64-bit
// architecture or you'll have to change the JIT code.
struct CallData
{
    // below is to be compatible with Value. Initialize tag to 0
#if Q_BYTE_ORDER != Q_LITTLE_ENDIAN
    uint tag;
#endif
    int argc;
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    uint tag;
#endif
    inline ReturnedValue argument(int i) const {
        return i < argc ? args[i].asReturnedValue() : Primitive::undefinedValue().asReturnedValue();
    }

    Value thisObject;
    Value args[1];

    static Q_DECL_CONSTEXPR int HeaderSize() { return offsetof(CallData, args) / sizeof(QV4::Value); }
};

Q_STATIC_ASSERT(std::is_standard_layout<CallData>::value);
Q_STATIC_ASSERT(offsetof(CallData, thisObject) == 8);
Q_STATIC_ASSERT(offsetof(CallData, args) == 16);

namespace Heap {

struct QmlContext;

#define ExecutionContextMembers(class, Member) \
    Member(class, NoMark, CallData *, callData)  \
    Member(class, Pointer, ExecutionContext *, outer) \
    Member(class, Pointer, Object *, activation) \
    Member(class, NoMark, QV4::Function *, v4Function) \

DECLARE_HEAP_OBJECT(ExecutionContext, Base) {
    DECLARE_MARK_TABLE(ExecutionContext);

    enum ContextType {
        Type_GlobalContext = 0x1,
        Type_CatchContext = 0x2,
        Type_WithContext = 0x3,
        Type_QmlContext = 0x4,
        Type_SimpleCallContext = 0x5,
        Type_CallContext = 0x6
    };

    void init(ContextType t)
    {
        Base::init();

        type = t;
    }

    quint8 type;
    bool strictMode : 8;
#if QT_POINTER_SIZE == 8
    quint8 padding_[6];
#else
    quint8 padding_[2];
#endif
};
V4_ASSERT_IS_TRIVIAL(ExecutionContext)
Q_STATIC_ASSERT(sizeof(ExecutionContext) == sizeof(Base) + sizeof(ExecutionContextData) + QT_POINTER_SIZE);

Q_STATIC_ASSERT(std::is_standard_layout<ExecutionContextData>::value);
Q_STATIC_ASSERT(offsetof(ExecutionContextData, callData) == 0);
Q_STATIC_ASSERT(offsetof(ExecutionContextData, outer) == offsetof(ExecutionContextData, callData) + QT_POINTER_SIZE);
Q_STATIC_ASSERT(offsetof(ExecutionContextData, activation) == offsetof(ExecutionContextData, outer) + QT_POINTER_SIZE);
Q_STATIC_ASSERT(offsetof(ExecutionContextData, v4Function) == offsetof(ExecutionContextData, activation) + QT_POINTER_SIZE);

#define CallContextMembers(class, Member) \
    Member(class, Pointer, FunctionObject *, function) \
    Member(class, ValueArray, ValueArray, locals)

DECLARE_HEAP_OBJECT(CallContext, ExecutionContext) {
    DECLARE_MARK_TABLE(CallContext);

    void init(ContextType t = Type_SimpleCallContext)
    {
        ExecutionContext::init(t);
    }

    inline unsigned int formalParameterCount() const;

};
V4_ASSERT_IS_TRIVIAL(CallContext)
Q_STATIC_ASSERT(std::is_standard_layout<CallContextData>::value);
Q_STATIC_ASSERT(offsetof(CallContextData, function) == 0);
Q_STATIC_ASSERT(offsetof(CallContextData, locals) == offsetof(CallContextData, function) + QT_POINTER_SIZE);
//### The following size check fails on Win8. With the ValueArray at the end of the
// CallContextMembers, it doesn't look very useful.
//#if defined(Q_PROCESSOR_ARM_32) && !defined(Q_OS_IOS)
//Q_STATIC_ASSERT(sizeof(CallContext) == sizeof(ExecutionContext) + sizeof(CallContextData) + QT_POINTER_SIZE);
//#else
//Q_STATIC_ASSERT(sizeof(CallContext) == sizeof(ExecutionContext) + sizeof(CallContextData));
//#endif

#define CatchContextMembers(class, Member) \
    Member(class, Pointer, String *, exceptionVarName) \
    Member(class, HeapValue, HeapValue, exceptionValue)

DECLARE_HEAP_OBJECT(CatchContext, ExecutionContext) {
    DECLARE_MARK_TABLE(CatchContext);

    void init(ExecutionContext *outerContext, String *exceptionVarName, const Value &exceptionValue);
};
V4_ASSERT_IS_TRIVIAL(CatchContext)

}

struct Q_QML_EXPORT ExecutionContext : public Managed
{
    enum {
        IsExecutionContext = true
    };

    V4_MANAGED(ExecutionContext, Managed)
    Q_MANAGED_TYPE(ExecutionContext)
    V4_INTERNALCLASS(ExecutionContext)

    Heap::CallContext *newCallContext(Function *f, CallData *callData);
    Heap::ExecutionContext *newWithContext(Heap::Object *with);
    Heap::CatchContext *newCatchContext(Heap::String *exceptionVarName, ReturnedValue exceptionValue);

    void createMutableBinding(String *name, bool deletable);

    bool setProperty(String *name, const Value &value);
    ReturnedValue getProperty(String *name);
    ReturnedValue getPropertyAndBase(String *name, Value *base);
    bool deleteProperty(String *name);

    inline CallContext *asCallContext();
    inline const CallContext *asCallContext() const;

    Function *getFunction() const;

    Value &thisObject() const {
        return d()->callData->thisObject;
    }
    int argc() const {
        return d()->callData->argc;
    }
    const Value *args() const {
        return d()->callData->args;
    }
    ReturnedValue argument(int i) const {
        return d()->callData->argument(i);
    }

    ReturnedValue call(ExecutionEngine *engine, CallData *callData, QV4::Function *function, const QV4::FunctionObject *f = 0);
    ReturnedValue simpleCall(ExecutionEngine *engine, CallData *callData, QV4::Function *function);
};

struct Q_QML_EXPORT CallContext : public ExecutionContext
{
    V4_MANAGED(CallContext, ExecutionContext)
    V4_INTERNALCLASS(CallContext)

    // formals are in reverse order
    Identifier * const *formals() const;
    unsigned int formalCount() const;
    Identifier * const *variables() const;
    unsigned int variableCount() const;

    inline ReturnedValue argument(int i) const;
};

inline ReturnedValue CallContext::argument(int i) const {
    return i < argc() ? args()[i].asReturnedValue() : Primitive::undefinedValue().asReturnedValue();
}

struct CatchContext : public ExecutionContext
{
    V4_MANAGED(CatchContext, ExecutionContext)
};

inline CallContext *ExecutionContext::asCallContext()
{
    return d()->type >= Heap::ExecutionContext::Type_SimpleCallContext ? static_cast<CallContext *>(this) : 0;
}

inline const CallContext *ExecutionContext::asCallContext() const
{
    return d()->type >= Heap::ExecutionContext::Type_SimpleCallContext ? static_cast<const CallContext *>(this) : 0;
}

} // namespace QV4

QT_END_NAMESPACE

#endif

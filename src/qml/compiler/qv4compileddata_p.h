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
#ifndef QV4COMPILEDDATA_P_H
#define QV4COMPILEDDATA_P_H

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

#include <QtCore/qstring.h>
#include <QVector>
#include <QStringList>
#include <QHash>
#include <QUrl>

#include <private/qv4value_p.h>
#include <private/qv4executableallocator_p.h>
#include <private/qqmlrefcount_p.h>
#include <private/qqmlnullablevalue_p.h>
#include <private/qv4identifier_p.h>
#include <private/qflagpointer_p.h>
#include <private/qendian_p.h>
#ifndef V4_BOOTSTRAP
#include <private/qqmltypenamecache_p.h>
#include <private/qqmlpropertycache_p.h>
#endif

QT_BEGIN_NAMESPACE

// Bump this whenever the compiler data structures change in an incompatible way.
#define QV4_DATA_STRUCTURE_VERSION 0x14

class QIODevice;
class QQmlPropertyCache;
class QQmlPropertyData;
class QQmlTypeNameCache;
class QQmlScriptData;
class QQmlType;
class QQmlEngine;

namespace QmlIR {
struct Document;
}

namespace QV4 {

struct Function;
class EvalISelFactory;
class CompilationUnitMapper;

namespace CompiledData {

struct String;
struct Function;
struct Lookup;
struct RegExp;
struct Unit;

template <typename ItemType, typename Container, const ItemType *(Container::*IndexedGetter)(int index) const>
struct TableIterator
{
    TableIterator(const Container *container, int index) : container(container), index(index) {}
    const Container *container;
    int index;

    const ItemType *operator->() { return (container->*IndexedGetter)(index); }
    void operator++() { ++index; }
    bool operator==(const TableIterator &rhs) const { return index == rhs.index; }
    bool operator!=(const TableIterator &rhs) const { return index != rhs.index; }
};

#if defined(Q_CC_MSVC) || defined(Q_CC_GNU)
#pragma pack(push, 1)
#endif

struct Location
{
    union {
        quint32 _dummy;
        quint32_le_bitfield<0, 20> line;
        quint32_le_bitfield<20, 12> column;
    };

    Location() : _dummy(0) { }

    inline bool operator<(const Location &other) const {
        return line < other.line ||
               (line == other.line && column < other.column);
    }
};

struct RegExp
{
    enum Flags : unsigned int {
        RegExp_Global     = 0x01,
        RegExp_IgnoreCase = 0x02,
        RegExp_Multiline  = 0x04
    };
    union {
        quint32 _dummy;
        quint32_le_bitfield<0, 4> flags;
        quint32_le_bitfield<4, 28> stringIndex;
    };

    RegExp() : _dummy(0) { }
};

struct Lookup
{
    enum Type : unsigned int {
        Type_Getter = 0x0,
        Type_Setter = 0x1,
        Type_GlobalGetter = 2
    };

    union {
        quint32 _dummy;
        quint32_le_bitfield<0, 4> type_and_flags;
        quint32_le_bitfield<4, 28> nameIndex;
    };

    Lookup() : _dummy(0) { }
};

struct JSClassMember
{
    union {
        quint32 _dummy;
        quint32_le_bitfield<0, 31> nameOffset;
        quint32_le_bitfield<31, 1> isAccessor;
    };

    JSClassMember() : _dummy(0) { }
};

struct JSClass
{
    quint32_le nMembers;
    // JSClassMember[nMembers]

    static int calculateSize(int nMembers) { return (sizeof(JSClass) + nMembers * sizeof(JSClassMember) + 7) & ~7; }
};

struct String
{
    qint32_le size;
    // uint16 strdata[]

    static int calculateSize(const QString &str) {
        return (sizeof(String) + str.length() * sizeof(quint16) + 7) & ~0x7;
    }
};

struct CodeOffsetToLine {
    quint32_le codeOffset;
    quint32_le line;
};

// Function is aligned on an 8-byte boundary to make sure there are no bus errors or penalties
// for unaligned access. The ordering of the fields is also from largest to smallest.
struct Function
{
    enum Flags : unsigned int {
        IsStrict            = 0x1,
        HasDirectEval       = 0x2,
        UsesArgumentsObject = 0x4,
        IsNamedExpression   = 0x8,
        HasCatchOrWith      = 0x10,
        CanUseSimpleCall    = 0x20
    };

    // Absolute offset into file where the code for this function is located. Only used when the function
    // is serialized.
    quint64_le codeOffset;
    quint64_le codeSize;

    quint32_le nameIndex;
    quint32_le nFormals;
    quint32_le formalsOffset;
    quint32_le nLocals;
    quint32_le localsOffset;
    quint32_le nLineNumbers;
    quint32_le lineNumberOffset;
    quint32_le nInnerFunctions;
    quint32_le nRegisters;
    Location location;

    // Qml Extensions Begin
    quint32_le nDependingIdObjects;
    quint32_le dependingIdObjectsOffset; // Array of resolved ID objects
    quint32_le nDependingContextProperties;
    quint32_le dependingContextPropertiesOffset; // Array of int pairs (property index and notify index)
    quint32_le nDependingScopeProperties;
    quint32_le dependingScopePropertiesOffset; // Array of int pairs (property index and notify index)
    // Qml Extensions End

//    quint32 formalsIndex[nFormals]
//    quint32 localsIndex[nLocals]
//    quint32 offsetForInnerFunctions[nInnerFunctions]
//    Function[nInnerFunctions]

    // Keep all unaligned data at the end
    quint8 flags;

    const quint32_le *formalsTable() const { return reinterpret_cast<const quint32_le *>(reinterpret_cast<const char *>(this) + formalsOffset); }
    const quint32_le *localsTable() const { return reinterpret_cast<const quint32_le *>(reinterpret_cast<const char *>(this) + localsOffset); }
    const CodeOffsetToLine *lineNumberTable() const { return reinterpret_cast<const CodeOffsetToLine *>(reinterpret_cast<const char *>(this) + lineNumberOffset); }
    const quint32_le *qmlIdObjectDependencyTable() const { return reinterpret_cast<const quint32_le *>(reinterpret_cast<const char *>(this) + dependingIdObjectsOffset); }
    const quint32_le *qmlContextPropertiesDependencyTable() const { return reinterpret_cast<const quint32_le *>(reinterpret_cast<const char *>(this) + dependingContextPropertiesOffset); }
    const quint32_le *qmlScopePropertiesDependencyTable() const { return reinterpret_cast<const quint32_le *>(reinterpret_cast<const char *>(this) + dependingScopePropertiesOffset); }

    // --- QQmlPropertyCacheCreator interface
    const quint32_le *formalsBegin() const { return formalsTable(); }
    const quint32_le *formalsEnd() const { return formalsTable() + nFormals; }
    // ---

    inline bool hasQmlDependencies() const { return nDependingIdObjects > 0 || nDependingContextProperties > 0 || nDependingScopeProperties > 0; }

    static int calculateSize(int nFormals, int nLocals, int nLines, int nInnerfunctions, int nIdObjectDependencies, int nPropertyDependencies) {
        int trailingData = (nFormals + nLocals + nInnerfunctions +  nIdObjectDependencies +
                2 * nPropertyDependencies)*sizeof (quint32) + nLines*sizeof(CodeOffsetToLine);
        return align(align(sizeof(Function)) + size_t(trailingData));
    }

    static size_t align(size_t a) {
        return (a + 7) & ~size_t(7);
    }
};

// Qml data structures

struct Q_QML_EXPORT TranslationData {
    quint32_le commentIndex;
    qint32_le number;
};

struct Q_QML_PRIVATE_EXPORT Binding
{
    quint32_le propertyNameIndex;

    enum ValueType : unsigned int {
        Type_Invalid,
        Type_Boolean,
        Type_Number,
        Type_String,
        Type_Translation,
        Type_TranslationById,
        Type_Script,
        Type_Object,
        Type_AttachedProperty,
        Type_GroupProperty
    };

    enum Flags : unsigned int {
        IsSignalHandlerExpression = 0x1,
        IsSignalHandlerObject = 0x2,
        IsOnAssignment = 0x4,
        InitializerForReadOnlyDeclaration = 0x8,
        IsResolvedEnum = 0x10,
        IsListItem = 0x20,
        IsBindingToAlias = 0x40,
        IsDeferredBinding = 0x80,
        IsCustomParserBinding = 0x100,
    };

    union {
        quint32_le_bitfield<0, 16> flags;
        quint32_le_bitfield<16, 16> type;
    };
    union {
        bool b;
        quint64 doubleValue; // do not access directly, needs endian protected access
        quint32_le compiledScriptIndex; // used when Type_Script
        quint32_le objectIndex;
        TranslationData translationData; // used when Type_Translation
    } value;
    quint32_le stringIndex; // Set for Type_String, Type_Translation and Type_Script (the latter because of script strings)

    Location location;
    Location valueLocation;

    bool isValueBinding() const
    {
        if (type == Type_AttachedProperty
            || type == Type_GroupProperty)
            return false;
        if (flags & IsSignalHandlerExpression
            || flags & IsSignalHandlerObject)
            return false;
        return true;
    }

    bool isValueBindingNoAlias() const { return isValueBinding() && !(flags & IsBindingToAlias); }
    bool isValueBindingToAlias() const { return isValueBinding() && (flags & IsBindingToAlias); }

    bool isSignalHandler() const
    {
        if (flags & IsSignalHandlerExpression || flags & IsSignalHandlerObject) {
            Q_ASSERT(!isValueBinding());
            Q_ASSERT(!isAttachedProperty());
            Q_ASSERT(!isGroupProperty());
            return true;
        }
        return false;
    }

    bool isAttachedProperty() const
    {
        if (type == Type_AttachedProperty) {
            Q_ASSERT(!isValueBinding());
            Q_ASSERT(!isSignalHandler());
            Q_ASSERT(!isGroupProperty());
            return true;
        }
        return false;
    }

    bool isGroupProperty() const
    {
        if (type == Type_GroupProperty) {
            Q_ASSERT(!isValueBinding());
            Q_ASSERT(!isSignalHandler());
            Q_ASSERT(!isAttachedProperty());
            return true;
        }
        return false;
    }

    static QString escapedString(const QString &string);

    bool containsTranslations() const { return type == Type_Translation || type == Type_TranslationById; }
    bool evaluatesToString() const { return type == Type_String || containsTranslations(); }

    QString valueAsString(const Unit *unit) const;
    QString valueAsScriptString(const Unit *unit) const;
    double valueAsNumber() const
    {
        if (type != Type_Number)
            return 0.0;
        quint64 intval = qFromLittleEndian<quint64>(value.doubleValue);
        double d;
        memcpy(&d, &intval, sizeof(double));
        return d;
    }
    void setNumberValueInternal(double d)
    {
        quint64 intval;
        memcpy(&intval, &d, sizeof(double));
        value.doubleValue = qToLittleEndian<quint64>(intval);
    }

    bool valueAsBoolean() const
    {
        if (type == Type_Boolean)
            return value.b;
        return false;
    }

};

struct EnumValue
{
    quint32_le nameIndex;
    qint32_le value;
    Location location;
};

struct Enum
{
    quint32_le nameIndex;
    quint32_le nEnumValues;
    Location location;

    const EnumValue *enumValueAt(int idx) const {
        return reinterpret_cast<const EnumValue*>(this + 1) + idx;
    }

    static int calculateSize(int nEnumValues) {
        return (sizeof(Enum)
                + nEnumValues * sizeof(EnumValue)
                + 7) & ~0x7;
    }

    // --- QQmlPropertyCacheCreatorInterface
    const EnumValue *enumValuesBegin() const { return enumValueAt(0); }
    const EnumValue *enumValuesEnd() const { return enumValueAt(nEnumValues); }
    int enumValueCount() const { return nEnumValues; }
    // ---
};

struct Parameter
{
    quint32_le nameIndex;
    quint32_le type;
    quint32_le customTypeNameIndex;
    Location location;
};

struct Signal
{
    quint32_le nameIndex;
    quint32_le nParameters;
    Location location;
    // Parameter parameters[1];

    const Parameter *parameterAt(int idx) const {
        return reinterpret_cast<const Parameter*>(this + 1) + idx;
    }

    static int calculateSize(int nParameters) {
        return (sizeof(Signal)
                + nParameters * sizeof(Parameter)
                + 7) & ~0x7;
    }

    // --- QQmlPropertyCacheCceatorInterface
    const Parameter *parametersBegin() const { return parameterAt(0); }
    const Parameter *parametersEnd() const { return parameterAt(nParameters); }
    int parameterCount() const { return nParameters; }
    // ---
};

struct Property
{
    enum Type : unsigned int { Var = 0, Variant, Int, Bool, Real, String, Url, Color,
                Font, Time, Date, DateTime, Rect, Point, Size,
                Vector2D, Vector3D, Vector4D, Matrix4x4, Quaternion,
                Custom, CustomList };

    enum Flags : unsigned int {
        IsReadOnly = 0x1
    };

    quint32_le nameIndex;
    union {
        quint32_le_bitfield<0, 31> type;
        quint32_le_bitfield<31, 1> flags; // readonly
    };
    quint32_le customTypeNameIndex; // If type >= Custom
    Location location;
};

struct Alias {
    enum Flags : unsigned int {
        IsReadOnly = 0x1,
        Resolved = 0x2,
        AliasPointsToPointerObject = 0x4
    };
    union {
        quint32_le_bitfield<0, 29> nameIndex;
        quint32_le_bitfield<29, 3> flags;
    };
    union {
        quint32_le idIndex; // string index
        quint32_le_bitfield<0, 31> targetObjectId; // object id index (in QQmlContextData::idValues)
        quint32_le_bitfield<31, 1> aliasToLocalAlias;
    };
    union {
        quint32_le propertyNameIndex; // string index
        qint32_le encodedMetaPropertyIndex;
        quint32_le localAliasIndex; // index in list of aliases local to the object (if targetObjectId == objectId)
    };
    Location location;
    Location referenceLocation;

    bool isObjectAlias() const {
        Q_ASSERT(flags & Resolved);
        return encodedMetaPropertyIndex == -1;
    }
};

struct Object
{
    enum Flags : unsigned int {
        NoFlag = 0x0,
        IsComponent = 0x1, // object was identified to be an explicit or implicit component boundary
        HasDeferredBindings = 0x2, // any of the bindings are deferred
        HasCustomParserBindings = 0x4
    };

    // Depending on the use, this may be the type name to instantiate before instantiating this
    // object. For grouped properties the type name will be empty and for attached properties
    // it will be the name of the attached type.
    quint32_le inheritedTypeNameIndex;
    quint32_le idNameIndex;
    union {
        quint32_le_bitfield<0, 15> flags;
        quint32_le_bitfield<15, 1> defaultPropertyIsAlias;
        qint32_le_bitfield<16, 16> id;
    };
    qint32_le indexOfDefaultPropertyOrAlias; // -1 means no default property declared in this object
    quint32_le nFunctions;
    quint32_le offsetToFunctions;
    quint32_le nProperties;
    quint32_le offsetToProperties;
    quint32_le nAliases;
    quint32_le offsetToAliases;
    quint32_le nEnums;
    quint32_le offsetToEnums; // which in turn will be a table with offsets to variable-sized Enum objects
    quint32_le nSignals;
    quint32_le offsetToSignals; // which in turn will be a table with offsets to variable-sized Signal objects
    quint32_le nBindings;
    quint32_le offsetToBindings;
    quint32_le nNamedObjectsInComponent;
    quint32_le offsetToNamedObjectsInComponent;
    Location location;
    Location locationOfIdProperty;
//    Function[]
//    Property[]
//    Signal[]
//    Binding[]

    static int calculateSizeExcludingSignalsAndEnums(int nFunctions, int nProperties, int nAliases, int nEnums, int nSignals, int nBindings, int nNamedObjectsInComponent)
    {
        return ( sizeof(Object)
                 + nFunctions * sizeof(quint32)
                 + nProperties * sizeof(Property)
                 + nAliases * sizeof(Alias)
                 + nEnums * sizeof(quint32)
                 + nSignals * sizeof(quint32)
                 + nBindings * sizeof(Binding)
                 + nNamedObjectsInComponent * sizeof(int)
                 + 0x7
               ) & ~0x7;
    }

    const quint32_le *functionOffsetTable() const
    {
        return reinterpret_cast<const quint32_le*>(reinterpret_cast<const char *>(this) + offsetToFunctions);
    }

    const Property *propertyTable() const
    {
        return reinterpret_cast<const Property*>(reinterpret_cast<const char *>(this) + offsetToProperties);
    }

    const Alias *aliasTable() const
    {
        return reinterpret_cast<const Alias*>(reinterpret_cast<const char *>(this) + offsetToAliases);
    }

    const Binding *bindingTable() const
    {
        return reinterpret_cast<const Binding*>(reinterpret_cast<const char *>(this) + offsetToBindings);
    }

    const Enum *enumAt(int idx) const
    {
        const quint32_le *offsetTable = reinterpret_cast<const quint32_le*>((reinterpret_cast<const char *>(this)) + offsetToEnums);
        const quint32_le offset = offsetTable[idx];
        return reinterpret_cast<const Enum*>(reinterpret_cast<const char*>(this) + offset);
    }

    const Signal *signalAt(int idx) const
    {
        const quint32_le *offsetTable = reinterpret_cast<const quint32_le*>((reinterpret_cast<const char *>(this)) + offsetToSignals);
        const quint32_le offset = offsetTable[idx];
        return reinterpret_cast<const Signal*>(reinterpret_cast<const char*>(this) + offset);
    }

    const quint32_le *namedObjectsInComponentTable() const
    {
        return reinterpret_cast<const quint32_le*>(reinterpret_cast<const char *>(this) + offsetToNamedObjectsInComponent);
    }

    // --- QQmlPropertyCacheCreator interface
    int propertyCount() const { return nProperties; }
    int aliasCount() const { return nAliases; }
    int enumCount() const { return nEnums; }
    int signalCount() const { return nSignals; }
    int functionCount() const { return nFunctions; }

    const Binding *bindingsBegin() const { return bindingTable(); }
    const Binding *bindingsEnd() const { return bindingTable() + nBindings; }

    const Property *propertiesBegin() const { return propertyTable(); }
    const Property *propertiesEnd() const { return propertyTable() + nProperties; }

    const Alias *aliasesBegin() const { return aliasTable(); }
    const Alias *aliasesEnd() const { return aliasTable() + nAliases; }

    typedef TableIterator<Enum, Object, &Object::enumAt> EnumIterator;
    EnumIterator enumsBegin() const { return EnumIterator(this, 0); }
    EnumIterator enumsEnd() const { return EnumIterator(this, nEnums); }

    typedef TableIterator<Signal, Object, &Object::signalAt> SignalIterator;
    SignalIterator signalsBegin() const { return SignalIterator(this, 0); }
    SignalIterator signalsEnd() const { return SignalIterator(this, nSignals); }

    int namedObjectsInComponentCount() const { return nNamedObjectsInComponent; }
    // ---
};

struct Import
{
    enum ImportType : unsigned int {
        ImportLibrary = 0x1,
        ImportFile = 0x2,
        ImportScript = 0x3
    };
    quint32_le type;

    quint32_le uriIndex;
    quint32_le qualifierIndex;

    qint32_le majorVersion;
    qint32_le minorVersion;

    Location location;

    Import() { type = 0; uriIndex = 0; qualifierIndex = 0; majorVersion = 0; minorVersion = 0; }
};

static const char magic_str[] = "qv4cdata";

struct Unit
{
    // DO NOT CHANGE THESE FIELDS EVER
    char magic[8];
    quint32_le version;
    quint32_le qtVersion;
    qint64_le sourceTimeStamp;
    quint32_le unitSize; // Size of the Unit and any depending data.
    // END DO NOT CHANGE THESE FIELDS EVER

    char md5Checksum[16]; // checksum of all bytes following this field.
    void generateChecksum();

    quint32_le architectureIndex; // string index to QSysInfo::buildAbi()
    quint32_le codeGeneratorIndex;
    char dependencyMD5Checksum[16];

    enum : unsigned int {
        IsJavascript = 0x1,
        IsQml = 0x2,
        StaticData = 0x4, // Unit data persistent in memory?
        IsSingleton = 0x8,
        IsSharedLibrary = 0x10, // .pragma shared?
        ContainsMachineCode = 0x20, // used to determine if we need to mmap with execute permissions
        PendingTypeCompilation = 0x40 // the QML data structures present are incomplete and require type compilation
    };
    quint32_le flags;
    quint32_le stringTableSize;
    quint32_le offsetToStringTable;
    quint32_le functionTableSize;
    quint32_le offsetToFunctionTable;
    quint32_le lookupTableSize;
    quint32_le offsetToLookupTable;
    quint32_le regexpTableSize;
    quint32_le offsetToRegexpTable;
    quint32_le constantTableSize;
    quint32_le offsetToConstantTable;
    quint32_le jsClassTableSize;
    quint32_le offsetToJSClassTable;
    qint32_le indexOfRootFunction;
    quint32_le sourceFileIndex;

    /* QML specific fields */
    quint32_le nImports;
    quint32_le offsetToImports;
    quint32_le nObjects;
    quint32_le offsetToObjects;
    quint32_le indexOfRootObject;

    const Import *importAt(int idx) const {
        return reinterpret_cast<const Import*>((reinterpret_cast<const char *>(this)) + offsetToImports + idx * sizeof(Import));
    }

    const Object *objectAt(int idx) const {
        const quint32_le *offsetTable = reinterpret_cast<const quint32_le*>((reinterpret_cast<const char *>(this)) + offsetToObjects);
        const quint32_le offset = offsetTable[idx];
        return reinterpret_cast<const Object*>(reinterpret_cast<const char*>(this) + offset);
    }

    bool isSingleton() const {
        return flags & Unit::IsSingleton;
    }
    /* end QML specific fields*/

    QString stringAt(int idx) const {
        const quint32_le *offsetTable = reinterpret_cast<const quint32_le*>((reinterpret_cast<const char *>(this)) + offsetToStringTable);
        const quint32_le offset = offsetTable[idx];
        const String *str = reinterpret_cast<const String*>(reinterpret_cast<const char *>(this) + offset);
        if (str->size == 0)
            return QString();
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        const QChar *characters = reinterpret_cast<const QChar *>(str + 1);
        // Too risky to do this while we unmap disk backed compilation but keep pointers to string
        // data in the identifier tables.
        //        if (flags & StaticData)
        //            return QString::fromRawData(characters, str->size);
        return QString(characters, str->size);
#else
        const quint16_le *characters = reinterpret_cast<const quint16_le *>(str + 1);
        QString qstr(str->size, Qt::Uninitialized);
        QChar *ch = qstr.data();
        for (int i = 0; i < str->size; ++i)
             ch[i] = QChar(characters[i]);
         return qstr;
#endif
    }

    const quint32_le *functionOffsetTable() const { return reinterpret_cast<const quint32_le*>((reinterpret_cast<const char *>(this)) + offsetToFunctionTable); }

    const Function *functionAt(int idx) const {
        const quint32_le *offsetTable = functionOffsetTable();
        const quint32_le offset = offsetTable[idx];
        return reinterpret_cast<const Function*>(reinterpret_cast<const char *>(this) + offset);
    }

    const Lookup *lookupTable() const { return reinterpret_cast<const Lookup*>(reinterpret_cast<const char *>(this) + offsetToLookupTable); }
    const RegExp *regexpAt(int index) const {
        return reinterpret_cast<const RegExp*>(reinterpret_cast<const char *>(this) + offsetToRegexpTable + index * sizeof(RegExp));
    }
    const quint64_le *constants() const {
        return reinterpret_cast<const quint64_le*>(reinterpret_cast<const char *>(this) + offsetToConstantTable);
    }

    const JSClassMember *jsClassAt(int idx, int *nMembers) const {
        const quint32_le *offsetTable = reinterpret_cast<const quint32_le *>(reinterpret_cast<const char *>(this) + offsetToJSClassTable);
        const quint32_le offset = offsetTable[idx];
        const char *ptr = reinterpret_cast<const char *>(this) + offset;
        const JSClass *klass = reinterpret_cast<const JSClass *>(ptr);
        *nMembers = klass->nMembers;
        return reinterpret_cast<const JSClassMember*>(ptr + sizeof(JSClass));
    }
};

#if defined(Q_CC_MSVC) || defined(Q_CC_GNU)
#pragma pack(pop)
#endif

struct TypeReference
{
    TypeReference(const Location &loc)
        : location(loc)
        , needsCreation(false)
        , errorWhenNotFound(false)
    {}
    Location location; // first use
    bool needsCreation : 1; // whether the type needs to be creatable or not
    bool errorWhenNotFound: 1;
};

// Map from name index to location of first use.
struct TypeReferenceMap : QHash<int, TypeReference>
{
    TypeReference &add(int nameIndex, const Location &loc) {
        Iterator it = find(nameIndex);
        if (it != end())
            return *it;
        return *insert(nameIndex, loc);
    }

    template <typename CompiledObject>
    void collectFromObject(const CompiledObject *obj)
    {
        if (obj->inheritedTypeNameIndex != 0) {
            TypeReference &r = this->add(obj->inheritedTypeNameIndex, obj->location);
            r.needsCreation = true;
            r.errorWhenNotFound = true;
        }

        auto prop = obj->propertiesBegin();
        auto propEnd = obj->propertiesEnd();
        for ( ; prop != propEnd; ++prop) {
            if (prop->type >= QV4::CompiledData::Property::Custom) {
                // ### FIXME: We could report the more accurate location here by using prop->location, but the old
                // compiler can't and the tests expect it to be the object location right now.
                TypeReference &r = this->add(prop->customTypeNameIndex, obj->location);
                r.errorWhenNotFound = true;
            }
        }

        auto binding = obj->bindingsBegin();
        auto bindingEnd = obj->bindingsEnd();
        for ( ; binding != bindingEnd; ++binding) {
            if (binding->type == QV4::CompiledData::Binding::Type_AttachedProperty)
                this->add(binding->propertyNameIndex, binding->location);
        }
    }

    template <typename Iterator>
    void collectFromObjects(Iterator it, Iterator end)
    {
        for (; it != end; ++it)
            collectFromObject(*it);
    }
};

#ifndef V4_BOOTSTRAP
struct ResolvedTypeReference;
// map from name index
// While this could be a hash, a map is chosen here to provide a stable
// order, which is used to calculating a check-sum on dependent meta-objects.
struct ResolvedTypeReferenceMap: public QMap<int, ResolvedTypeReference*>
{
    bool addToHash(QCryptographicHash *hash, QQmlEngine *engine) const;
};

using DependentTypesHasher = std::function<bool(QCryptographicHash *)>;
#else
struct DependentTypesHasher {};
#endif

// index is per-object binding index
typedef QVector<QQmlPropertyData*> BindingPropertyData;

// This is how this hooks into the existing structures:

struct Q_QML_PRIVATE_EXPORT CompilationUnitBase
{
    QV4::Heap::String **runtimeStrings = 0; // Array
};

Q_STATIC_ASSERT(std::is_standard_layout<CompilationUnitBase>::value);
Q_STATIC_ASSERT(offsetof(CompilationUnitBase, runtimeStrings) == 0);

struct Q_QML_PRIVATE_EXPORT CompilationUnit : public CompilationUnitBase, public QQmlRefCount
{
#ifdef V4_BOOTSTRAP
    CompilationUnit()
        : data(0)
    {}
    virtual ~CompilationUnit() {}
#else
    CompilationUnit();
    virtual ~CompilationUnit();
#endif

    const Unit *data;

    // Called only when building QML, when we build the header for JS first and append QML data
    virtual QV4::CompiledData::Unit *createUnitData(QmlIR::Document *irDocument);

#ifndef V4_BOOTSTRAP
    ExecutionEngine *engine;

    QString fileName() const { return data->stringAt(data->sourceFileIndex); }
    QUrl url() const { if (m_url.isNull) m_url = QUrl(fileName()); return m_url; }

    QV4::Lookup *runtimeLookups;
    QV4::Value *runtimeRegularExpressions;
    QV4::InternalClass **runtimeClasses;
    QVector<QV4::Function *> runtimeFunctions;
    mutable QQmlNullableValue<QUrl> m_url;

    // QML specific fields
    QQmlPropertyCacheVector propertyCaches;
    QQmlPropertyCache *rootPropertyCache() const { return propertyCaches.at(data->indexOfRootObject); }

    QQmlRefPointer<QQmlTypeNameCache> typeNameCache;

    // index is object index. This allows fast access to the
    // property data when initializing bindings, avoiding expensive
    // lookups by string (property name).
    QVector<BindingPropertyData> bindingPropertyDataPerObject;

    // mapping from component object index (CompiledData::Unit object index that points to component) to identifier hash of named objects
    // this is initialized on-demand by QQmlContextData
    QHash<int, IdentifierHash<int>> namedObjectsPerComponentCache;
    IdentifierHash<int> namedObjectsPerComponent(int componentObjectIndex);

    // pointers either to data->constants() or little-endian memory copy.
    const Value* constants;

    void finalize(QQmlEnginePrivate *engine);

    int totalBindingsCount; // Number of bindings used in this type
    int totalParserStatusCount; // Number of instantiated types that are QQmlParserStatus subclasses
    int totalObjectCount; // Number of objects explicitly instantiated

    QVector<QQmlScriptData *> dependentScripts;
    ResolvedTypeReferenceMap resolvedTypes;

    bool verifyChecksum(const DependentTypesHasher &dependencyHasher) const;

    int metaTypeId;
    int listMetaTypeId;
    bool isRegisteredWithEngine;

    QScopedPointer<CompilationUnitMapper> backingFile;

    // --- interface for QQmlPropertyCacheCreator
    typedef Object CompiledObject;
    int objectCount() const { return data->nObjects; }
    int rootObjectIndex() const { return data->indexOfRootObject; }
    const Object *objectAt(int index) const { return data->objectAt(index); }
    QString stringAt(int index) const { return data->stringAt(index); }

    struct FunctionIterator
    {
        FunctionIterator(const Unit *unit, const Object *object, int index) : unit(unit), object(object), index(index) {}
        const Unit *unit;
        const Object *object;
        int index;

        const Function *operator->() const { return unit->functionAt(object->functionOffsetTable()[index]); }
        void operator++() { ++index; }
        bool operator==(const FunctionIterator &rhs) const { return index == rhs.index; }
        bool operator!=(const FunctionIterator &rhs) const { return index != rhs.index; }
    };
    FunctionIterator objectFunctionsBegin(const Object *object) const { return FunctionIterator(data, object, 0); }
    FunctionIterator objectFunctionsEnd(const Object *object) const { return FunctionIterator(data, object, object->nFunctions); }
    // ---

    QV4::Function *linkToEngine(QV4::ExecutionEngine *engine);
    void unlink();

    void markObjects(MarkStack *markStack);

    void destroy() Q_DECL_OVERRIDE;

    bool loadFromDisk(const QUrl &url, const QDateTime &sourceTimeStamp, QString *errorString);

protected:
    virtual void linkBackendToEngine(QV4::ExecutionEngine *engine) = 0;
    virtual bool memoryMapCode(QString *errorString);
#endif // V4_BOOTSTRAP

public:
#if defined(V4_BOOTSTRAP)
    bool saveToDisk(const QString &outputFileName, QString *errorString);
#else
    bool saveToDisk(const QUrl &unitUrl, QString *errorString);
#endif

protected:
    virtual void prepareCodeOffsetsForDiskStorage(CompiledData::Unit *unit);
    virtual bool saveCodeToDisk(QIODevice *device, const CompiledData::Unit *unit, QString *errorString);
};

#ifndef V4_BOOTSTRAP
struct ResolvedTypeReference
{
    ResolvedTypeReference()
        : majorVersion(0)
        , minorVersion(0)
        , isFullyDynamicType(false)
    {}

    QQmlType type;
    QQmlRefPointer<QQmlPropertyCache> typePropertyCache;
    QQmlRefPointer<QV4::CompiledData::CompilationUnit> compilationUnit;

    int majorVersion;
    int minorVersion;
    // Types such as QQmlPropertyMap can add properties dynamically at run-time and
    // therefore cannot have a property cache installed when instantiated.
    bool isFullyDynamicType;

    QQmlPropertyCache *propertyCache() const;
    QQmlPropertyCache *createPropertyCache(QQmlEngine *);
    bool addToHash(QCryptographicHash *hash, QQmlEngine *engine);

    void doDynamicTypeCheck();
};
#endif

}

}

Q_DECLARE_TYPEINFO(QV4::CompiledData::JSClassMember, Q_PRIMITIVE_TYPE);

QT_END_NAMESPACE

#endif

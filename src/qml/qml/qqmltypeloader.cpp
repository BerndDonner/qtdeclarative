// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include <private/qqmltypeloader_p.h>

#include <private/qqmldirdata_p.h>
#include <private/qqmlprofiler_p.h>
#include <private/qqmlscriptblob_p.h>
#include <private/qqmlscriptdata_p.h>
#include <private/qqmlsourcecoordinate_p.h>
#include <private/qqmltypedata_p.h>
#include <private/qqmltypeloaderqmldircontent_p.h>
#include <private/qqmltypeloaderthread_p.h>
#include <private/qv4compiler_p.h>
#include <private/qv4compilercontext_p.h>
#include <private/qv4runtimecodegen_p.h>

#include <QtQml/qqmlabstracturlinterceptor.h>
#include <QtQml/qqmlengine.h>
#include <QtQml/qqmlextensioninterface.h>
#include <QtQml/qqmlfile.h>

#include <qtqml_tracepoints_p.h>

#include <QtCore/qdir.h>
#include <QtCore/qdiriterator.h>
#include <QtCore/qfile.h>
#include <QtCore/qthread.h>

#include <functional>

#define ASSERT_LOADTHREAD() Q_ASSERT(m_thread && m_thread->isThisThread())
#define ASSERT_ENGINETHREAD() Q_ASSERT(m_engine->thread()->isCurrentThread())

QT_BEGIN_NAMESPACE

namespace {

    template<typename LockType>
    struct LockHolder
    {
        LockType& lock;
        LockHolder(LockType *l) : lock(*l) { lock.lock(); }
        ~LockHolder() { lock.unlock(); }
    };
}

Q_TRACE_POINT(qtqml, QQmlCompiling_entry, const QUrl &url)
Q_TRACE_POINT(qtqml, QQmlCompiling_exit)

/*!
\class QQmlTypeLoader
\brief The QQmlTypeLoader class abstracts loading files and their dependencies over the network.
\internal

The QQmlTypeLoader class is provided for the exclusive use of the QQmlTypeLoader class.

Clients create QQmlDataBlob instances and submit them to the QQmlTypeLoader class
through the QQmlTypeLoader::load() or QQmlTypeLoader::loadWithStaticData() methods.
The loader then fetches the data over the network or from the local file system in an efficient way.
QQmlDataBlob is an abstract class, so should always be specialized.

Once data is received, the QQmlDataBlob::dataReceived() method is invoked on the blob.  The
derived class should use this callback to process the received data.  Processing of the data can
result in an error being set (QQmlDataBlob::setError()), or one or more dependencies being
created (QQmlDataBlob::addDependency()).  Dependencies are other QQmlDataBlob's that
are required before processing can fully complete.

To complete processing, the QQmlDataBlob::done() callback is invoked.  done() is called when
one of these three preconditions are met.

\list 1
\li The QQmlDataBlob has no dependencies.
\li The QQmlDataBlob has an error set.
\li All the QQmlDataBlob's dependencies are themselves "done()".
\endlist

Thus QQmlDataBlob::done() will always eventually be called, even if the blob has an error set.
*/

void QQmlTypeLoader::invalidate()
{
    ASSERT_ENGINETHREAD();

    shutdownThread();

#if QT_CONFIG(qml_network)
    // Need to delete the network replies after
    // the loader thread is shutdown as it could be
    // getting new replies while we clear them
    m_networkReplies.clear();
#endif // qml_network
}

void QQmlTypeLoader::addUrlInterceptor(QQmlAbstractUrlInterceptor *urlInterceptor)
{
    ASSERT_ENGINETHREAD();
    m_urlInterceptors.append(urlInterceptor);
}

void QQmlTypeLoader::removeUrlInterceptor(QQmlAbstractUrlInterceptor *urlInterceptor)
{
    ASSERT_ENGINETHREAD();
    m_urlInterceptors.removeOne(urlInterceptor);
}

QList<QQmlAbstractUrlInterceptor *> QQmlTypeLoader::urlInterceptors() const
{
    ASSERT_ENGINETHREAD();
    return m_urlInterceptors;
}

QUrl QQmlTypeLoader::interceptUrl(const QUrl &url, QQmlAbstractUrlInterceptor::DataType type) const
{
    // Can be called from either thread, but only after interceptor setup is done.

    QUrl result = url;
    for (QQmlAbstractUrlInterceptor *interceptor : m_urlInterceptors)
        result = interceptor->intercept(result, type);
    return result;
}

bool QQmlTypeLoader::hasUrlInterceptors() const
{
    // Can be called from either thread, but only after interceptor setup is done.

    return !m_urlInterceptors.isEmpty();
}

#if QT_CONFIG(qml_debug)
void QQmlTypeLoader::setProfiler(QQmlProfiler *profiler)
{
    ASSERT_ENGINETHREAD();

    Q_ASSERT(!m_profiler);
    m_profiler.reset(profiler);
}
#endif

struct PlainLoader {
    void loadThread(QQmlTypeLoader *loader, const QQmlDataBlob::Ptr &blob) const
    {
        loader->loadThread(blob);
    }
    void load(QQmlTypeLoader *loader, const QQmlDataBlob::Ptr &blob) const
    {
        loader->ensureThread()->load(blob);
    }
    void loadAsync(QQmlTypeLoader *loader, const QQmlDataBlob::Ptr &blob) const
    {
        loader->ensureThread()->loadAsync(blob);
    }
};

struct StaticLoader {
    const QByteArray &data;
    StaticLoader(const QByteArray &data) : data(data) {}

    void loadThread(QQmlTypeLoader *loader, const QQmlDataBlob::Ptr &blob) const
    {
        loader->loadWithStaticDataThread(blob, data);
    }
    void load(QQmlTypeLoader *loader, const QQmlDataBlob::Ptr &blob) const
    {
        loader->ensureThread()->loadWithStaticData(blob, data);
    }
    void loadAsync(QQmlTypeLoader *loader, const QQmlDataBlob::Ptr &blob) const
    {
        loader->ensureThread()->loadWithStaticDataAsync(blob, data);
    }
};

struct CachedLoader {
    const QQmlPrivate::CachedQmlUnit *unit;
    CachedLoader(const QQmlPrivate::CachedQmlUnit *unit) :  unit(unit) {}

    void loadThread(QQmlTypeLoader *loader, const QQmlDataBlob::Ptr &blob) const
    {
        loader->loadWithCachedUnitThread(blob, unit);
    }
    void load(QQmlTypeLoader *loader, const QQmlDataBlob::Ptr &blob) const
    {
        loader->ensureThread()->loadWithCachedUnit(blob, unit);
    }
    void loadAsync(QQmlTypeLoader *loader, const QQmlDataBlob::Ptr &blob) const
    {
        loader->ensureThread()->loadWithCachedUnitAsync(blob, unit);
    }
};

template<typename Loader>
void QQmlTypeLoader::doLoad(const Loader &loader, const QQmlDataBlob::Ptr &blob, Mode mode)
{
    // Can be called from either thread.
#ifdef DATABLOB_DEBUG
    qWarning("QQmlTypeLoader::doLoad(%s): %s thread", qPrintable(blob->urlString()),
             (m_thread && m_thread->isThisThread()) ? "Compile" : "Engine");
#endif
    blob->startLoading();

    if (m_thread && m_thread->isThisThread()) {
        unlock();
        loader.loadThread(this, blob);
        lock();
    } else if (mode == Asynchronous) {
        blob->setIsAsync(true);
        unlock();
        loader.loadAsync(this, blob);
        lock();
    } else {
        unlock();
        loader.load(this, blob);
        lock();
        if (mode == PreferSynchronous) {
            if (!blob->isCompleteOrError())
                blob->setIsAsync(true);
        } else {
            Q_ASSERT(mode == Synchronous);
            Q_ASSERT(m_thread);
            while (!blob->isCompleteOrError())
                m_thread->waitForNextMessage();
        }
    }
}

/*!
Load the provided \a blob from the network or filesystem.

The loader must be locked.
*/
void QQmlTypeLoader::load(const QQmlDataBlob::Ptr &blob, Mode mode)
{
    // Can be called from either thread.
    doLoad(PlainLoader(), blob, mode);
}

/*!
Load the provided \a blob with \a data.  The blob's URL is not used by the data loader in this case.

The loader must be locked.
*/
void QQmlTypeLoader::loadWithStaticData(
        const QQmlDataBlob::Ptr &blob, const QByteArray &data, Mode mode)
{
    // Can be called from either thread.
    doLoad(StaticLoader(data), blob, mode);
}

void QQmlTypeLoader::loadWithCachedUnit(
        const QQmlDataBlob::Ptr &blob, const QQmlPrivate::CachedQmlUnit *unit, Mode mode)
{
    // Can be called from either thread.
    doLoad(CachedLoader(unit), blob, mode);
}

void QQmlTypeLoader::drop(const QQmlDataBlob::Ptr &blob)
{
    ASSERT_ENGINETHREAD();

    // We must not destroy a QQmlDataBlob from the main thread
    // since it will shuffle its dependencies around.
    if (m_thread)
        m_thread->drop(blob);
}

void QQmlTypeLoader::loadWithStaticDataThread(const QQmlDataBlob::Ptr &blob, const QByteArray &data)
{
    ASSERT_LOADTHREAD();

    setData(blob, data);
}

void QQmlTypeLoader::loadWithCachedUnitThread(const QQmlDataBlob::Ptr &blob, const QQmlPrivate::CachedQmlUnit *unit)
{
    ASSERT_LOADTHREAD();

    setCachedUnit(blob, unit);
}

void QQmlTypeLoader::loadThread(const QQmlDataBlob::Ptr &blob)
{
    ASSERT_LOADTHREAD();

    if (blob->m_url.isEmpty()) {
        QQmlError error;
        error.setDescription(QLatin1String("Invalid null URL"));
        blob->setError(error);
        return;
    }

    if (QQmlFile::isSynchronous(blob->m_url)) {
        const QString fileName = QQmlFile::urlToLocalFileOrQrc(blob->m_url);
        if (!QQml_isFileCaseCorrect(fileName)) {
            blob->setError(QLatin1String("File name case mismatch"));
            return;
        }

        if (blob->setProgress(1.f) && blob->isAsync())
            m_thread->callDownloadProgressChanged(blob, 1.);

        setData(blob, fileName);

    } else {
#if QT_CONFIG(qml_network)
        QNetworkReply *reply = m_thread->networkAccessManager()->get(QNetworkRequest(blob->m_url));
        QQmlTypeLoaderNetworkReplyProxy *nrp = m_thread->networkReplyProxy();
        m_networkReplies.insert(reply, blob);

        if (reply->isFinished()) {
            nrp->manualFinished(reply);
        } else {
            QObject::connect(reply, &QNetworkReply::downloadProgress,
                             nrp, &QQmlTypeLoaderNetworkReplyProxy::downloadProgress);
            QObject::connect(reply, &QNetworkReply::finished,
                             nrp, &QQmlTypeLoaderNetworkReplyProxy::finished);
        }

#ifdef DATABLOB_DEBUG
        qWarning("QQmlDataBlob: requested %s", qPrintable(blob->urlString()));
#endif // DATABLOB_DEBUG
#endif // qml_network
    }
}

#define DATALOADER_MAXIMUM_REDIRECT_RECURSION 16

#ifndef TYPELOADER_MINIMUM_TRIM_THRESHOLD
#define TYPELOADER_MINIMUM_TRIM_THRESHOLD 64
#endif

#if QT_CONFIG(qml_network)
void QQmlTypeLoader::networkReplyFinished(QNetworkReply *reply)
{
    ASSERT_LOADTHREAD();

    reply->deleteLater();

    QQmlRefPointer<QQmlDataBlob> blob = m_networkReplies.take(reply);

    Q_ASSERT(blob);

    blob->m_redirectCount++;

    if (blob->m_redirectCount < DATALOADER_MAXIMUM_REDIRECT_RECURSION) {
        QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (redirect.isValid()) {
            QUrl url = reply->url().resolved(redirect.toUrl());
            blob->m_finalUrl = url;
            blob->m_finalUrlString.clear();

            QNetworkReply *reply = m_thread->networkAccessManager()->get(QNetworkRequest(url));
            QObject *nrp = m_thread->networkReplyProxy();
            QObject::connect(reply, SIGNAL(finished()), nrp, SLOT(finished()));
            m_networkReplies.insert(reply, std::move(blob));
#ifdef DATABLOB_DEBUG
            qWarning("QQmlDataBlob: redirected to %s", qPrintable(blob->finalUrlString()));
#endif
            return;
        }
    }

    if (reply->error()) {
        blob->networkError(reply->error());
    } else {
        QByteArray data = reply->readAll();
        setData(blob, data);
    }
}

void QQmlTypeLoader::networkReplyProgress(QNetworkReply *reply,
                                                  qint64 bytesReceived, qint64 bytesTotal)
{
    ASSERT_LOADTHREAD();

    const QQmlRefPointer<QQmlDataBlob> blob = m_networkReplies.value(reply);

    Q_ASSERT(blob);

    if (bytesTotal != 0) {
        const qreal progress = (qreal(bytesReceived) / qreal(bytesTotal));
        if (blob->setProgress(progress) && blob->isAsync())
            m_thread->callDownloadProgressChanged(blob, blob->progress());
    }
}
#endif // qml_network

/*! \internal
Call the initializeEngine() method on \a iface.  Used by QQmlImportDatabase to ensure it
gets called in the correct thread.
*/
template<class Interface>
void doInitializeEngine(
        Interface *iface, QQmlTypeLoaderThread *thread, QQmlEngine *engine, const char *uri)
{
    // Can be called from either thread
    // Must not touch engine if called from type loader thread

    if (thread && thread->isThisThread()) {
        thread->initializeEngine(iface, uri);
    } else {
        Q_ASSERT(engine->thread() == QThread::currentThread());
        iface->initializeEngine(engine, uri);
    }
}

void QQmlTypeLoader::initializeEngine(QQmlEngineExtensionInterface *iface, const char *uri)
{
    // Can be called from either thread
    doInitializeEngine(iface, m_thread, m_engine, uri);
}

void QQmlTypeLoader::initializeEngine(QQmlExtensionInterface *iface, const char *uri)
{
    // Can be called from either thread
    doInitializeEngine(iface, m_thread, m_engine, uri);
}

void QQmlTypeLoader::setData(const QQmlDataBlob::Ptr &blob, const QByteArray &data)
{
    ASSERT_LOADTHREAD();

    QQmlDataBlob::SourceCodeData d;
    d.inlineSourceCode = QString::fromUtf8(data);
    d.hasInlineSourceCode = true;
    setData(blob, d);
}

void QQmlTypeLoader::setData(const QQmlDataBlob::Ptr &blob, const QString &fileName)
{
    ASSERT_LOADTHREAD();

    QQmlDataBlob::SourceCodeData d;
    d.fileInfo = QFileInfo(fileName);
    setData(blob, d);
}

void QQmlTypeLoader::setData(const QQmlDataBlob::Ptr &blob, const QQmlDataBlob::SourceCodeData &d)
{
    ASSERT_LOADTHREAD();

    Q_TRACE_SCOPE(QQmlCompiling, blob->url());
    QQmlCompilingProfiler prof(profiler(), blob.data());

    blob->m_inCallback = true;

    blob->dataReceived(d);

    if (!blob->isError() && !blob->isWaiting())
        blob->allDependenciesDone();

    blob->m_inCallback = false;

    blob->tryDone();
}

void QQmlTypeLoader::setCachedUnit(const QQmlDataBlob::Ptr &blob, const QQmlPrivate::CachedQmlUnit *unit)
{
    ASSERT_LOADTHREAD();

    Q_TRACE_SCOPE(QQmlCompiling, blob->url());
    QQmlCompilingProfiler prof(profiler(), blob.data());

    blob->m_inCallback = true;

    blob->initializeFromCachedUnit(unit);

    if (!blob->isError() && !blob->isWaiting())
        blob->allDependenciesDone();

    blob->m_inCallback = false;

    blob->tryDone();
}

void QQmlTypeLoader::startThread()
{
    ASSERT_ENGINETHREAD();

    if (!m_thread) {
        // Read the relevant configuration values at the last possible moment before we start
        // the thread. After the thread has been started, changing the configuration would result
        // in UB. Therefore we can disregard this case.
        QV4::ExecutionEngine *v4 = m_engine->handle();
        m_isDebugging = v4->debugger() != nullptr;
        m_diskCacheOptions = v4->diskCacheOptions();
        m_thread = new QQmlTypeLoaderThread(this);
    }
}

void QQmlTypeLoader::shutdownThread()
{
    ASSERT_ENGINETHREAD();

    if (m_thread) {
        delete m_thread;
        m_thread = nullptr;
    }
}

QQmlTypeLoader::Blob::PendingImport::PendingImport(
        const QQmlRefPointer<Blob> &blob, const QV4::CompiledData::Import *import,
        QQmlImports::ImportFlags flags)
    : uri(blob->stringAt(import->uriIndex))
    , qualifier(blob->stringAt(import->qualifierIndex))
    , type(static_cast<QV4::CompiledData::Import::ImportType>(quint32(import->type)))
    , location(import->location)
    , flags(flags)
    , version(import->version)
{
}

QQmlTypeLoader::Blob::Blob(const QUrl &url, QQmlDataBlob::Type type, QQmlTypeLoader *loader)
  : QQmlDataBlob(url, type, loader)
  , m_importCache(new QQmlImports(), QQmlRefPointer<QQmlImports>::Adopt)
{
}

QQmlTypeLoader::Blob::~Blob()
{
}

bool QQmlTypeLoader::Blob::fetchQmldir(
        const QUrl &url, const QQmlTypeLoader::Blob::PendingImportPtr &import, int priority,
        QList<QQmlError> *errors)
{
    assertTypeLoaderThread();

    QQmlRefPointer<QQmlQmldirData> data = typeLoader()->getQmldir(url);

    data->setPriority(this, import, priority);

    if (data->status() == Error) {
        // This qmldir must not exist - which is not an error
        return true;
    } else if (data->status() == Complete) {
        // This data is already available
        return qmldirDataAvailable(data, errors);
    }

    // Wait for this data to become available
    addDependency(data.data());
    return true;
}

/*!
 * \internal
 * Import any qualified scripts of for \a import as listed in \a qmldir.
 * Precondition is that \a import is actually qualified.
 */
void QQmlTypeLoader::Blob::importQmldirScripts(
        const QQmlTypeLoader::Blob::PendingImportPtr &import,
        const QQmlTypeLoaderQmldirContent &qmldir, const QUrl &qmldirUrl)
{
    assertTypeLoaderThread();

    const auto qmldirScripts = qmldir.scripts();
    for (const QQmlDirParser::Script &script : qmldirScripts) {
        const QUrl plainUrl = QUrl(script.fileName);
        const QUrl scriptUrl = qmldirUrl.resolved(plainUrl);
        QQmlRefPointer<QQmlScriptBlob> blob = typeLoader()->getScript(scriptUrl, plainUrl);
        addDependency(blob.data());
        scriptImported(blob, import->location, script.nameSpace, import->qualifier);
    }
}

template<typename URL>
void postProcessQmldir(
        QQmlTypeLoader::Blob *self,
        const QQmlTypeLoader::Blob::PendingImportPtr &import, const QString &qmldirFilePath,
        const URL &qmldirUrl)
{
    self->assertTypeLoaderThread();

    const QQmlTypeLoaderQmldirContent qmldir = self->typeLoader()->qmldirContent(qmldirFilePath);
    if (!import->qualifier.isEmpty())
        self->importQmldirScripts(import, qmldir, QUrl(qmldirUrl));

    if (qmldir.plugins().isEmpty()) {
        // If the qmldir does not register a plugin, we might still have declaratively
        // registered types (if we are dealing with an application instead of a library)
        // We should use module name given in the qmldir rather than the one given by the
        // import since the import may be a directory import.
        auto module = QQmlMetaType::typeModule(qmldir.typeNamespace(), import->version);
        if (!module)
            QQmlMetaType::qmlRegisterModuleTypes(qmldir.typeNamespace());
        // else: If the module already exists, the types must have been already registered
    }
}

bool QQmlTypeLoader::Blob::updateQmldir(const QQmlRefPointer<QQmlQmldirData> &data, const QQmlTypeLoader::Blob::PendingImportPtr &import, QList<QQmlError> *errors)
{
    // TODO: Shouldn't this lock?

    assertTypeLoaderThread();

    QString qmldirIdentifier = data->urlString();
    QString qmldirUrl = qmldirIdentifier.left(qmldirIdentifier.lastIndexOf(QLatin1Char('/')) + 1);

    typeLoader()->setQmldirContent(qmldirIdentifier, data->content());

    const QTypeRevision version = m_importCache->updateQmldirContent(
            typeLoader(), import->uri, import->qualifier, qmldirIdentifier, qmldirUrl, errors);
    if (!version.isValid())
        return false;

    // Use more specific version for dependencies if possible
    if (version.hasMajorVersion())
        import->version = version;

    if (!loadImportDependencies(import, qmldirIdentifier, import->flags, errors))
        return false;

    import->priority = 0;

    // Release this reference at destruction
    m_qmldirs << data;

    postProcessQmldir(this, import, qmldirIdentifier, qmldirUrl);
    return true;
}

bool QQmlTypeLoader::Blob::addScriptImport(const QQmlTypeLoader::Blob::PendingImportPtr &import)
{
    assertTypeLoaderThread();
    const QUrl url(import->uri);
    QQmlTypeLoader *loader = typeLoader();
    QQmlRefPointer<QQmlScriptBlob> blob = loader->getScript(finalUrl().resolved(url), url);
    addDependency(blob.data());
    scriptImported(blob, import->location, import->qualifier, QString());
    return true;
}

bool QQmlTypeLoader::Blob::addFileImport(const QQmlTypeLoader::Blob::PendingImportPtr &import, QList<QQmlError> *errors)
{
    assertTypeLoaderThread();
    QQmlImports::ImportFlags flags;

    QUrl importUrl(import->uri);
    QString path = importUrl.path();
    path.append(QLatin1String(path.endsWith(QLatin1Char('/')) ? "qmldir" : "/qmldir"));
    importUrl.setPath(path);
    QUrl qmldirUrl = finalUrl().resolved(importUrl);
    if (!QQmlImports::isLocal(qmldirUrl)) {
        // This is a remote file; the import is currently incomplete
        flags = QQmlImports::ImportIncomplete;
    }

    const QTypeRevision version = m_importCache->addFileImport(
            typeLoader(), import->uri, import->qualifier, import->version, flags,
            import->precedence, nullptr, errors);
    if (!version.isValid())
        return false;

    // Use more specific version for the qmldir if possible
    if (version.hasMajorVersion())
        import->version = version;

    if (flags & QQmlImports::ImportIncomplete) {
        if (!fetchQmldir(qmldirUrl, import, 1, errors))
            return false;
    } else {
        const QString qmldirFilePath = QQmlFile::urlToLocalFileOrQrc(qmldirUrl);
        if (!loadImportDependencies(import, qmldirFilePath, import->flags, errors))
            return false;

        postProcessQmldir(this, import, qmldirFilePath, qmldirUrl);
    }

    return true;
}

static void addDependencyImportError(
        const QQmlTypeLoader::Blob::PendingImportPtr &import, QList<QQmlError> *errors)
{
    QQmlError error;
    QString reason = errors->front().description();
    if (reason.size() > 512)
        reason = reason.first(252) + QLatin1String("... ...") + reason.last(252);
    if (import->version.hasMajorVersion()) {
        error.setDescription(QQmlImportDatabase::tr(
                                     "module \"%1\" version %2.%3 cannot be imported because:\n%4")
                                     .arg(import->uri).arg(import->version.majorVersion())
                                     .arg(import->version.hasMinorVersion()
                                                  ? QString::number(import->version.minorVersion())
                                                  : QLatin1String("x"))
                                     .arg(reason));
    } else {
        error.setDescription(QQmlImportDatabase::tr("module \"%1\" cannot be imported because:\n%2")
                                     .arg(import->uri, reason));
    }
    errors->prepend(error);
}

bool QQmlTypeLoader::Blob::addLibraryImport(const QQmlTypeLoader::Blob::PendingImportPtr &import, QList<QQmlError> *errors)
{
    assertTypeLoaderThread();
    QQmlImportDatabase *importDatabase = typeLoader()->importDatabase();

    const QQmlImportDatabase::LocalQmldirSearchLocation searchMode =
            QQmlMetaType::isStronglyLockedModule(import->uri, import->version)
                ? QQmlImportDatabase::QmldirCacheOnly
                : QQmlImportDatabase::QmldirFileAndCache;

    const QQmlImportDatabase::LocalQmldirResult qmldirResult
            = importDatabase->locateLocalQmldir(
                import->uri, import->version, searchMode,
                [&](const QString &qmldirFilePath, const QString &qmldirUrl) {
        // This is a local library import
        const QTypeRevision actualVersion = m_importCache->addLibraryImport(
                typeLoader(), import->uri, import->qualifier, import->version, qmldirFilePath,
                qmldirUrl, import->flags, import->precedence, errors);
        if (!actualVersion.isValid())
            return false;

        // Use more specific version for dependencies if possible
        if (actualVersion.hasMajorVersion())
            import->version = actualVersion;

        if (!loadImportDependencies(import, qmldirFilePath, import->flags, errors)) {
            addDependencyImportError(import, errors);
            return false;
        }

        postProcessQmldir(this, import, qmldirFilePath, qmldirUrl);
        return true;
    });

    switch (qmldirResult) {
    case QQmlImportDatabase::QmldirFound:
        return true;
    case QQmlImportDatabase::QmldirNotFound: {
        if (!loadImportDependencies(import, QString(), import->flags, errors)) {
            addDependencyImportError(import, errors);
            return false;
        }
        break;
    }
    case QQmlImportDatabase::QmldirInterceptedToRemote:
        break;
    case QQmlImportDatabase::QmldirRejected:
        return false;
    }

    // If there is a qmldir we cannot see, yet, then we have to wait.
    // The qmldir might contain import directives.
    if (qmldirResult != QQmlImportDatabase::QmldirInterceptedToRemote && (
            // Major version of module already registered:
            // We believe that the registration is complete.
            QQmlMetaType::typeModule(import->uri, import->version)

            // Otherwise, try to register further module types.
            || QQmlMetaType::qmlRegisterModuleTypes(import->uri)

            // Otherwise, there is no way to register any further types.
            // Try with any module of that name.
            || QQmlMetaType::latestModuleVersion(import->uri).isValid())) {

        if (!m_importCache->addLibraryImport(
                    typeLoader(), import->uri, import->qualifier, import->version, QString(),
                    QString(), import->flags, import->precedence, errors).isValid()) {
            return false;
        }
    } else {
        // We haven't yet resolved this import
        m_unresolvedImports << import;

        const bool hasInterceptors = m_typeLoader->hasUrlInterceptors();

        // Query any network import paths for this library.
        // Interceptor might redirect local paths.
        QStringList remotePathList = importDatabase->importPathList(
                    hasInterceptors ? QQmlImportDatabase::LocalOrRemote
                                    : QQmlImportDatabase::Remote);
        if (!remotePathList.isEmpty()) {
            // Add this library and request the possible locations for it
            const QTypeRevision version = m_importCache->addLibraryImport(
                    typeLoader(), import->uri, import->qualifier, import->version, QString(),
                    QString(), import->flags | QQmlImports::ImportIncomplete, import->precedence,
                    errors);

            if (!version.isValid())
                return false;

            // Use more specific version for finding the qmldir if possible
            if (version.hasMajorVersion())
                import->version = version;

            // Probe for all possible locations
            int priority = 0;
            const QStringList qmlDirPaths = QQmlImports::completeQmldirPaths(
                        import->uri, remotePathList, import->version);
            for (const QString &qmldirPath : qmlDirPaths) {
                if (hasInterceptors) {
                    QUrl url = m_typeLoader->interceptUrl(
                                QQmlImports::urlFromLocalFileOrQrcOrUrl(qmldirPath),
                                QQmlAbstractUrlInterceptor::QmldirFile);
                    if (!QQmlFile::isLocalFile(url)
                            && !fetchQmldir(url, import, ++priority, errors)) {
                        return false;
                    }
                } else if (!fetchQmldir(QUrl(qmldirPath), import, ++priority, errors)) {
                    return false;
                }

            }
        }
    }

    return true;
}

bool QQmlTypeLoader::Blob::addImport(const QV4::CompiledData::Import *import,
                                     QQmlImports::ImportFlags flags, QList<QQmlError> *errors)
{
    assertTypeLoaderThread();
    return addImport(std::make_shared<PendingImport>(this, import, flags), errors);
}

bool QQmlTypeLoader::Blob::addImport(
        const QQmlTypeLoader::Blob::PendingImportPtr &import, QList<QQmlError> *errors)
{
    assertTypeLoaderThread();

    Q_ASSERT(errors);

    switch (import->type)
    {
    case QV4::CompiledData::Import::ImportLibrary:
        return addLibraryImport(import, errors);
    case QV4::CompiledData::Import::ImportFile:
        return addFileImport(import ,errors);
    case QV4::CompiledData::Import::ImportScript:
        return addScriptImport(import);
    case QV4::CompiledData::Import::ImportInlineComponent:
        Q_UNREACHABLE_RETURN(false); // addImport is never called with an inline component import
    }

    Q_UNREACHABLE_RETURN(false);
}

void QQmlTypeLoader::Blob::dependencyComplete(const QQmlDataBlob::Ptr &blob)
{
    assertTypeLoaderThread();

    if (blob->type() == QQmlDataBlob::QmldirFile) {
        QQmlQmldirData *data = static_cast<QQmlQmldirData *>(blob.data());
        QList<QQmlError> errors;
        if (!qmldirDataAvailable(data, &errors)) {
            Q_ASSERT(errors.size());
            QQmlError error(errors.takeFirst());
            error.setUrl(m_importCache->baseUrl());
            const QV4::CompiledData::Location importLocation = data->importLocation(this);
            error.setLine(qmlConvertSourceCoordinate<quint32, int>(importLocation.line()));
            error.setColumn(qmlConvertSourceCoordinate<quint32, int>(importLocation.column()));
            errors.prepend(error); // put it back on the list after filling out information.
            setError(errors);
        }
    }
}

bool QQmlTypeLoader::Blob::loadDependentImports(
        const QList<QQmlDirParser::Import> &imports, const QString &qualifier,
        QTypeRevision version, quint16 precedence, QQmlImports::ImportFlags flags,
        QList<QQmlError> *errors)
{
    assertTypeLoaderThread();

    for (const auto &import : imports) {
        if (import.flags & QQmlDirParser::Import::Optional)
            continue;
        auto dependencyImport = std::make_shared<PendingImport>();
        dependencyImport->uri = import.module;
        dependencyImport->qualifier = qualifier;
        dependencyImport->version = (import.flags & QQmlDirParser::Import::Auto)
                ? version : import.version;
        dependencyImport->flags = flags;
        dependencyImport->precedence = precedence;

        qCDebug(lcQmlImport)
                << "loading dependent import" << dependencyImport->uri << "version"
                << dependencyImport->version << "as" << dependencyImport->qualifier;

        if (!addImport(dependencyImport, errors)) {
            QQmlError error;
            error.setDescription(
                        QString::fromLatin1(
                            "Failed to load dependent import \"%1\" version %2.%3")
                        .arg(dependencyImport->uri)
                        .arg(dependencyImport->version.majorVersion())
                        .arg(dependencyImport->version.minorVersion()));
            errors->append(error);
            return false;
        }
    }

    return true;
}

bool QQmlTypeLoader::Blob::loadImportDependencies(
        const QQmlTypeLoader::Blob::PendingImportPtr &currentImport, const QString &qmldirUri,
        QQmlImports::ImportFlags flags, QList<QQmlError> *errors)
{
    assertTypeLoaderThread();

    QList<QQmlDirParser::Import> implicitImports
            = QQmlMetaType::moduleImports(currentImport->uri, currentImport->version);
    if (!qmldirUri.isEmpty())
        implicitImports += typeLoader()->qmldirContent(qmldirUri).imports();

    // Prevent overflow from one category of import into the other.
    switch (currentImport->precedence) {
    case QQmlImportInstance::Implicit - 1:
    case QQmlImportInstance::Lowest: {
        QQmlError error;
        error.setDescription(
                    QString::fromLatin1("Too many dependent imports for %1 %2.%3")
                    .arg(currentImport->uri)
                    .arg(currentImport->version.majorVersion())
                    .arg(currentImport->version.minorVersion()));
        errors->append(error);
        return false;
    }
    default:
        break;
    }

    if (!loadDependentImports(
                implicitImports, currentImport->qualifier, currentImport->version,
                currentImport->precedence + 1, flags, errors)) {
        QQmlError error;
        error.setDescription(
                    QString::fromLatin1(
                        "Failed to load dependencies for module \"%1\" version %2.%3")
                    .arg(currentImport->uri)
                    .arg(currentImport->version.majorVersion())
                    .arg(currentImport->version.minorVersion()));
        errors->append(error);
        return false;
    }

    return true;
}

bool QQmlTypeLoader::Blob::isDebugging() const
{
    return typeLoader()->m_isDebugging;
}

bool QQmlTypeLoader::Blob::readCacheFile() const
{
    return typeLoader()->m_diskCacheOptions & QV4::ExecutionEngine::DiskCache::QmlcRead;
}

bool QQmlTypeLoader::Blob::writeCacheFile() const
{
    return typeLoader()->m_diskCacheOptions & QV4::ExecutionEngine::DiskCache::QmlcWrite;
}

QQmlMetaType::CacheMode QQmlTypeLoader::Blob::aotCacheMode() const
{
    const QV4::ExecutionEngine::DiskCacheOptions options = typeLoader()->m_diskCacheOptions;
    if (!(options & QV4::ExecutionEngine::DiskCache::Aot))
        return QQmlMetaType::RejectAll;
    if (options & QV4::ExecutionEngine::DiskCache::AotByteCode)
        return QQmlMetaType::AcceptUntyped;
    return QQmlMetaType::RequireFullyTyped;
}

bool QQmlTypeLoader::Blob::qmldirDataAvailable(const QQmlRefPointer<QQmlQmldirData> &data, QList<QQmlError> *errors)
{
    assertTypeLoaderThread();
    return data->processImports(this, [&](const PendingImportPtr &import) {
        return updateQmldir(data, import, errors);
    });
}

/*!
Constructs a new type loader that uses the given \a engine.
*/
QQmlTypeLoader::QQmlTypeLoader(QQmlEngine *engine)
    : m_engine(engine)
    , m_typeCacheTrimThreshold(TYPELOADER_MINIMUM_TRIM_THRESHOLD)
{
}

/*!
Destroys the type loader, first clearing the cache of any information about
loaded files.
*/
QQmlTypeLoader::~QQmlTypeLoader()
{
    ASSERT_ENGINETHREAD();

    shutdownThread();

    // Delete the thread before clearing the cache. Otherwise it will be started up again.
    invalidate();

    clearCache();
}

QQmlImportDatabase *QQmlTypeLoader::importDatabase() const
{
    return &QQmlEnginePrivate::get(m_engine)->importDatabase;
}

QUrl QQmlTypeLoader::normalize(const QUrl &unNormalizedUrl)
{
    QUrl normalized(unNormalizedUrl);
    if (normalized.scheme() == QLatin1String("qrc"))
        normalized.setHost(QString()); // map qrc:///a.qml to qrc:/a.qml
    return normalized;
}

/*!
Returns a QQmlTypeData for the specified \a url.  The QQmlTypeData may be cached.
*/
QQmlRefPointer<QQmlTypeData> QQmlTypeLoader::getType(const QUrl &unNormalizedUrl, Mode mode)
{
    // TODO: This can be called from either thread and it's too complex for that.

    Q_ASSERT(!unNormalizedUrl.isRelative() &&
            (QQmlFile::urlToLocalFileOrQrc(unNormalizedUrl).isEmpty() ||
             !QDir::isRelativePath(QQmlFile::urlToLocalFileOrQrc(unNormalizedUrl))));

    const QUrl url = normalize(unNormalizedUrl);

    const auto handleExisting = [&](const QQmlRefPointer<QQmlTypeData> &typeData) {
        if ((mode == PreferSynchronous || mode == Synchronous) && QQmlFile::isSynchronous(url)) {
            // this was started Asynchronous, but we need to force Synchronous
            // completion now (if at all possible with this type of URL).

            // This only works when called directly from e.g. the UI thread, but not
            // when recursively called on the QML thread via resolveTypes()

            // NB: We do not want to know whether the thread is the main thread, but specifically
            //     that the thread is _not_ the thread we're waiting for.
            //     If !QT_CONFIG(qml_type_loader_thread) the QML thread is the main thread.
            if (m_thread && !m_thread->isThisThread()) {
                while (!typeData->isCompleteOrError())
                    m_thread->waitForNextMessage();
            }
        }
        return typeData;
    };

    // TODO: How long should we actually hold on to the lock?
    //       Currently, if we are in the type loader thread. The lock is held through the whole
    //       load() below. That's quite excessive.
    LockHolder<QQmlTypeLoader> holder(this);

    QQmlRefPointer<QQmlTypeData> typeData = m_typeCache.value(url);
    if (typeData)
        return handleExisting(typeData);

    // Trim before adding the new type, so that we don't immediately trim it away
    if (m_typeCache.size() >= m_typeCacheTrimThreshold)
        trimCache();

    typeData = QQml::makeRefPointer<QQmlTypeData>(url, this);

    // TODO: if (compiledData == 0), is it safe to omit this insertion?
    m_typeCache.insert(url, typeData);

    QQmlMetaType::CachedUnitLookupError error = QQmlMetaType::CachedUnitLookupError::NoError;
    const QQmlMetaType::CacheMode cacheMode = typeData->aotCacheMode();
    if (const QQmlPrivate::CachedQmlUnit *cachedUnit = (cacheMode != QQmlMetaType::RejectAll)
            ? QQmlMetaType::findCachedCompilationUnit(typeData->url(), cacheMode, &error)
            : nullptr) {
        QQmlTypeLoader::loadWithCachedUnit(QQmlDataBlob::Ptr(typeData.data()), cachedUnit, mode);
    } else {
        typeData->setCachedUnitStatus(error);
        QQmlTypeLoader::load(QQmlDataBlob::Ptr(typeData.data()), mode);
    }

    return typeData;
}

/*!
Returns a QQmlTypeData for the given \a data with the provided base \a url.  The
QQmlTypeData will not be cached.
*/
QQmlRefPointer<QQmlTypeData> QQmlTypeLoader::getType(const QByteArray &data, const QUrl &url, Mode mode)
{
    // TODO: This can be called from either thread. But why do we lock here? Or, why do we not
    //       cache the resulting QQmlTypeData?

    LockHolder<QQmlTypeLoader> holder(this);

    QQmlRefPointer<QQmlTypeData> typeData = QQml::makeRefPointer<QQmlTypeData>(url, this);
    QQmlTypeLoader::loadWithStaticData(QQmlDataBlob::Ptr(typeData.data()), data, mode);

    return typeData;
}

QQmlRefPointer<QV4::CompiledData::CompilationUnit> QQmlTypeLoader::injectScript(
        const QUrl &relativeUrl, const QV4::CompiledData::Unit *unit)
{
    ASSERT_ENGINETHREAD();

    QQmlRefPointer<QQmlScriptBlob> blob = QQml::makeRefPointer<QQmlScriptBlob>(relativeUrl, this);

    LockHolder<QQmlTypeLoader> holder(this);
    QQmlPrivate::CachedQmlUnit cached { unit, nullptr, nullptr};
    loadWithCachedUnit(blob.data(), &cached, Synchronous);
    Q_ASSERT(blob->isComplete());
    m_scriptCache.insert(relativeUrl, blob);
    return blob->scriptData()->compilationUnit();
}

/*!
Return a QQmlScriptBlob for \a url.  The QQmlScriptData may be cached.
*/
QQmlRefPointer<QQmlScriptBlob> QQmlTypeLoader::getScript(
        const QUrl &unNormalizedUrl, const QUrl &relativeUrl)
{
    // TODO: Can be called from either thread and hold on to the lock for too long.

    Q_ASSERT(!unNormalizedUrl.isRelative() &&
            (QQmlFile::urlToLocalFileOrQrc(unNormalizedUrl).isEmpty() ||
             !QDir::isRelativePath(QQmlFile::urlToLocalFileOrQrc(unNormalizedUrl))));

    const QUrl url = normalize(unNormalizedUrl);

    LockHolder<QQmlTypeLoader> holder(this);

    QQmlRefPointer<QQmlScriptBlob> scriptBlob = m_scriptCache.value(url);

    // Also try the relative URL since manually registering native modules doesn't require
    // passing an absolute URL and we don't have a reference URL for native modules.
    if (!scriptBlob && unNormalizedUrl != relativeUrl)
        scriptBlob = m_scriptCache.value(relativeUrl);

    if (!scriptBlob) {
        scriptBlob = QQml::makeRefPointer<QQmlScriptBlob>(url, this);
        m_scriptCache.insert(url, scriptBlob);

        QQmlMetaType::CachedUnitLookupError error = QQmlMetaType::CachedUnitLookupError::NoError;
        const QQmlMetaType::CacheMode cacheMode = scriptBlob->aotCacheMode();
        if (const QQmlPrivate::CachedQmlUnit *cachedUnit = (cacheMode != QQmlMetaType::RejectAll)
                ? QQmlMetaType::findCachedCompilationUnit(scriptBlob->url(), cacheMode, &error)
                : nullptr) {
            QQmlTypeLoader::loadWithCachedUnit(QQmlDataBlob::Ptr(scriptBlob.data()), cachedUnit);
        } else {
            scriptBlob->setCachedUnitStatus(error);
            QQmlTypeLoader::load(QQmlDataBlob::Ptr(scriptBlob.data()));
        }
    }

    return scriptBlob;
}

/*!
Returns a QQmlQmldirData for \a url.  The QQmlQmldirData may be cached.
*/
QQmlRefPointer<QQmlQmldirData> QQmlTypeLoader::getQmldir(const QUrl &url)
{
    // TODO: Can be called from either thread and hold on to the lock for too long.

    Q_ASSERT(!url.isRelative() &&
            (QQmlFile::urlToLocalFileOrQrc(url).isEmpty() ||
             !QDir::isRelativePath(QQmlFile::urlToLocalFileOrQrc(url))));
    LockHolder<QQmlTypeLoader> holder(this);

    QQmlRefPointer<QQmlQmldirData> qmldirData = m_qmldirCache.value(url);

    if (!qmldirData) {
        qmldirData = QQml::makeRefPointer<QQmlQmldirData>(url, this);
        m_qmldirCache.insert(url, qmldirData);
        QQmlTypeLoader::load(QQmlDataBlob::Ptr(qmldirData.data()));
    }

    return qmldirData;
}

/*!
Returns the absolute filename of path via a directory cache.
Returns a empty string if the path does not exist.

Why a directory cache?  QML checks for files in many paths with
invalid directories.  By caching whether a directory exists
we avoid many stats.  We also cache the files' existence in the
directory, for the same reason.
*/
QString QQmlTypeLoader::absoluteFilePath(const QString &path)
{
    // TODO: Can be called from either thread.

    if (path.isEmpty())
        return QString();
    if (path.at(0) == QLatin1Char(':')) {
        // qrc resource
        QFileInfo fileInfo(path);
        return fileInfo.isFile() ? fileInfo.absoluteFilePath() : QString();
    } else if (path.size() > 3 && path.at(3) == QLatin1Char(':') &&
               path.startsWith(QLatin1String("qrc"), Qt::CaseInsensitive)) {
        // qrc resource url
        QFileInfo fileInfo(QQmlFile::urlToLocalFileOrQrc(path));
        return fileInfo.isFile() ? fileInfo.absoluteFilePath() : QString();
    }
#if defined(Q_OS_ANDROID)
    else if (path.size() > 7 && path.at(6) == QLatin1Char(':') && path.at(7) == QLatin1Char('/') &&
           path.startsWith(QLatin1String("assets"), Qt::CaseInsensitive)) {
        // assets resource url
        QFileInfo fileInfo(QQmlFile::urlToLocalFileOrQrc(path));
        return fileInfo.isFile() ? fileInfo.absoluteFilePath() : QString();
    } else if (path.size() > 8 && path.at(7) == QLatin1Char(':') && path.at(8) == QLatin1Char('/') &&
           path.startsWith(QLatin1String("content"), Qt::CaseInsensitive)) {
        // content url
        QFileInfo fileInfo(QQmlFile::urlToLocalFileOrQrc(path));
        return fileInfo.isFile() ? fileInfo.absoluteFilePath() : QString();
    }
#endif

    int lastSlash = path.lastIndexOf(QLatin1Char('/'));
    QString dirPath(path.left(lastSlash));

    // TODO: Lock should be released when we're done with the cache
    LockHolder<QQmlTypeLoader> holder(this);
    if (!m_importDirCache.contains(dirPath)) {
        bool exists = QDir(dirPath).exists();
        QCache<QString, bool> *entry = exists ? new QCache<QString, bool> : nullptr;
        m_importDirCache.insert(dirPath, entry);
    }
    QCache<QString, bool> *fileSet = m_importDirCache.object(dirPath);
    if (!fileSet)
        return QString();

    QString absoluteFilePath;
    QString fileName(path.mid(lastSlash+1, path.size()-lastSlash-1));

    bool *value = fileSet->object(fileName);
    if (value) {
        if (*value)
            absoluteFilePath = path;
    } else {
        bool exists = QFile::exists(path);
        fileSet->insert(fileName, new bool(exists));
        if (exists)
            absoluteFilePath = path;
    }

    if (absoluteFilePath.size() > 2 && absoluteFilePath.at(0) != QLatin1Char('/') && absoluteFilePath.at(1) != QLatin1Char(':'))
        absoluteFilePath = QFileInfo(absoluteFilePath).absoluteFilePath();

    return absoluteFilePath;
}

bool QQmlTypeLoader::fileExists(const QString &path, const QString &file)
{
    // Can be called from either thread.

    const QChar nullChar(QChar::Null);
    if (path.isEmpty() || path.contains(nullChar) || file.isEmpty() || file.contains(nullChar))
        return false;

    Q_ASSERT(path.endsWith(QLatin1Char('/')));

    LockHolder<QQmlTypeLoader> holder(this);
    QCache<QString, bool> *fileSet = m_importDirCache.object(path);
    if (fileSet) {
        if (bool *value = fileSet->object(file))
            return *value;
    } else if (m_importDirCache.contains(path)) {
        // explicit nullptr in cache
        return false;
    }

    auto addToCache = [&](const QFileInfo &fileInfo) {
        if (!fileSet) {
            fileSet = fileInfo.dir().exists() ? new QCache<QString, bool> : nullptr;
            bool inserted = m_importDirCache.insert(path, fileSet);
            Q_ASSERT(inserted);
            if (!fileSet)
                return false;
        }

        const bool exists = fileInfo.exists();
        fileSet->insert(file, new bool(exists));
        return exists;
    };

    if (path.at(0) == QLatin1Char(':')) {
        // qrc resource
        return addToCache(QFileInfo(path + file));
    }

    if (path.size() > 3 && path.at(3) == QLatin1Char(':')
            && path.startsWith(QLatin1String("qrc"), Qt::CaseInsensitive)) {
        // qrc resource url
        return addToCache(QFileInfo(QQmlFile::urlToLocalFileOrQrc(path + file)));
    }

#if defined(Q_OS_ANDROID)
    if (path.size() > 7 && path.at(6) == QLatin1Char(':') && path.at(7) == QLatin1Char('/')
            && path.startsWith(QLatin1String("assets"), Qt::CaseInsensitive)) {
        // assets resource url
        return addToCache(QFileInfo(QQmlFile::urlToLocalFileOrQrc(path + file)));
    }

    if (path.size() > 8 && path.at(7) == QLatin1Char(':') && path.at(8) == QLatin1Char('/')
            && path.startsWith(QLatin1String("content"), Qt::CaseInsensitive)) {
        // content url
        return addToCache(QFileInfo(QQmlFile::urlToLocalFileOrQrc(path + file)));
    }
#endif

    return addToCache(QFileInfo(path + file));
}


/*!
Returns true if the path is a directory via a directory cache.  Cache is
shared with absoluteFilePath().
*/
bool QQmlTypeLoader::directoryExists(const QString &path)
{
    // Can be called from either thread.

    if (path.isEmpty())
        return false;

    bool isResource = path.at(0) == QLatin1Char(':');
#if defined(Q_OS_ANDROID)
    isResource = isResource || path.startsWith(QLatin1String("assets:/")) || path.startsWith(QLatin1String("content:/"));
#endif

    if (isResource) {
        // qrc resource
        QFileInfo fileInfo(path);
        return fileInfo.exists() && fileInfo.isDir();
    }

    int length = path.size();
    if (path.endsWith(QLatin1Char('/')))
        --length;
    QString dirPath(path.left(length));

    LockHolder<QQmlTypeLoader> holder(this);
    if (!m_importDirCache.contains(dirPath)) {
        bool exists = QDir(dirPath).exists();
        QCache<QString, bool> *files = exists ? new QCache<QString, bool> : nullptr;
        m_importDirCache.insert(dirPath, files);
    }

    QCache<QString, bool> *fileSet = m_importDirCache.object(dirPath);
    return fileSet != nullptr;
}


/*!
Return a QQmlTypeLoaderQmldirContent for absoluteFilePath.  The QQmlTypeLoaderQmldirContent may be cached.

\a filePath is a local file path.

It can also be a remote path for a remote directory import, but it will have been cached by now in this case.
*/
const QQmlTypeLoaderQmldirContent QQmlTypeLoader::qmldirContent(const QString &filePathIn)
{
    // TODO: Either this can only be called from the type loader thread. Then we don't need to lock.
    //       Or it can be called from both threads, then we need to lock all other access to the
    //       m_importQmlDirCache, too.

    LockHolder<QQmlTypeLoader> holder(this);

    QString filePath;

    // Try to guess if filePathIn is already a URL. This is necessarily fragile, because
    // - paths can contain ':', which might make them appear as URLs with schemes.
    // - windows drive letters appear as schemes (thus "< 2" below).
    // - a "file:" URL is equivalent to the respective file, but will be treated differently.
    // Yet, this heuristic is the best we can do until we pass more structured information here,
    // for example a QUrl also for local files.
    QUrl url(filePathIn);
    if (url.scheme().size() < 2) {
        filePath = filePathIn;
    } else {
        filePath = QQmlFile::urlToLocalFileOrQrc(url);
        if (filePath.isEmpty()) { // Can't load the remote here, but should be cached
            if (auto entry = m_importQmlDirCache.value(filePathIn))
                return **entry;
            else
                return QQmlTypeLoaderQmldirContent();
        }
    }

    QQmlTypeLoaderQmldirContent **val = m_importQmlDirCache.value(filePath);
    if (val)
        return **val;
    QQmlTypeLoaderQmldirContent *qmldir = new QQmlTypeLoaderQmldirContent;

#define ERROR(description) { QQmlError e; e.setDescription(description); qmldir->setError(e); }
#define NOT_READABLE_ERROR QString(QLatin1String("module \"$$URI$$\" definition \"%1\" not readable"))
#define CASE_MISMATCH_ERROR QString(QLatin1String("cannot load module \"$$URI$$\": File name case mismatch for \"%1\""))

    QFile file(filePath);
    if (!QQml_isFileCaseCorrect(filePath)) {
        ERROR(CASE_MISMATCH_ERROR.arg(filePath));
    } else if (file.open(QFile::ReadOnly)) {
        QByteArray data = file.readAll();
        qmldir->setContent(filePath, QString::fromUtf8(data));
    } else {
        ERROR(NOT_READABLE_ERROR.arg(filePath));
    }

#undef ERROR
#undef NOT_READABLE_ERROR
#undef CASE_MISMATCH_ERROR

    m_importQmlDirCache.insert(filePath, qmldir);
    return *qmldir;
}

void QQmlTypeLoader::setQmldirContent(const QString &url, const QString &content)
{
    // TODO: Should this lock?

    ASSERT_LOADTHREAD();

    QQmlTypeLoaderQmldirContent *qmldir;
    QQmlTypeLoaderQmldirContent **val = m_importQmlDirCache.value(url);
    if (val) {
        qmldir = *val;
    } else {
        qmldir = new QQmlTypeLoaderQmldirContent;
        m_importQmlDirCache.insert(url, qmldir);
    }

    if (!qmldir->hasContent())
        qmldir->setContent(url, content);
}

/*!
Clears cached information about loaded files, including any type data, scripts
and qmldir information.
*/
void QQmlTypeLoader::clearCache()
{
    // This looks dangerous because we're dropping live blobs on the engine thread.
    // However, it's safe because we shut down the type loader thread before we do so.

    ASSERT_ENGINETHREAD();

    // Temporarily shut the thread down and discard all messages, making it safe to
    // hack into the various data structures below.
    shutdownThread();

    qDeleteAll(m_importQmlDirCache);

    m_typeCache.clear();
    m_typeCacheTrimThreshold = TYPELOADER_MINIMUM_TRIM_THRESHOLD;
    m_scriptCache.clear();
    m_qmldirCache.clear();
    m_importDirCache.clear();
    m_importQmlDirCache.clear();
    m_checksumCache.clear();

    // The thread will auto-restart next time we need it.
}

void QQmlTypeLoader::updateTypeCacheTrimThreshold()
{
    // This can be called from either thread and is called from a method that locks.

    int size = m_typeCache.size();
    if (size > m_typeCacheTrimThreshold)
        m_typeCacheTrimThreshold = size * 2;
    if (size < m_typeCacheTrimThreshold / 2)
        m_typeCacheTrimThreshold = qMax(size * 2, TYPELOADER_MINIMUM_TRIM_THRESHOLD);
}

void QQmlTypeLoader::trimCache()
{
    // This can be called from either thread. It has to be called while the type loader mutex
    // is locked. It drops potentially live blobs, but only ones which are isCompleteOrError and
    // are not depended on by other blobs.

    while (true) {
        bool deletedOneType = false;
        for (TypeCache::Iterator iter = m_typeCache.begin(), end = m_typeCache.end(); iter != end;)  {
            const QQmlRefPointer<QQmlTypeData> &typeData = iter.value();

            // typeData->m_compiledData may be set early on in the proccess of loading a file, so
            // it's important to check the general loading status of the typeData before making any
            // other decisions.
            if (typeData->count() != 1 || !typeData->isCompleteOrError()) {
                ++iter;
                continue;
            }

            // isCompleteOrError means the waitingFor list of this typeData is empty.
            // Therefore, it cannot interfere with other blobs on destruction anymore.
            // Therefore, we can drop it on either the engine thread or the type loader thread.

            const QQmlRefPointer<QV4::CompiledData::CompilationUnit> &compilationUnit
                = typeData->m_compiledData;
            if (compilationUnit) {
                if (compilationUnit->count()
                        > QQmlMetaType::countInternalCompositeTypeSelfReferences(
                              compilationUnit) + 1) {
                    ++iter;
                    continue;
                }

                QQmlMetaType::unregisterInternalCompositeType(compilationUnit);
                Q_ASSERT(compilationUnit->count() == 1);
            }

            // There are no live objects of this type
            iter = m_typeCache.erase(iter);
            deletedOneType = true;
        }

        if (!deletedOneType)
            break;
    }

    // TODO: release any scripts which are no longer referenced by any types

    updateTypeCacheTrimThreshold();

    QQmlMetaType::freeUnusedTypesAndCaches();
}

bool QQmlTypeLoader::isTypeLoaded(const QUrl &url) const
{
    LockHolder<QQmlTypeLoader> holder(const_cast<QQmlTypeLoader *>(this));
    return m_typeCache.contains(url);
}

bool QQmlTypeLoader::isScriptLoaded(const QUrl &url) const
{
    LockHolder<QQmlTypeLoader> holder(const_cast<QQmlTypeLoader *>(this));
    return m_scriptCache.contains(url);
}

QT_END_NAMESPACE

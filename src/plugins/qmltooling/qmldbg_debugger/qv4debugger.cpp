// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "qv4debugger.h"
#include "qv4debugjob.h"
#include "qv4datacollector.h"

#include <private/qqmlcontext_p.h>
#include <private/qqmlengine_p.h>
#include <private/qv4scopedvalue_p.h>
#include <private/qv4script_p.h>
#include <private/qv4stackframe_p.h>

QT_BEGIN_NAMESPACE

QV4Debugger::BreakPoint::BreakPoint(const QString &fileName, int line)
    : fileName(fileName), lineNumber(line)
{}

inline size_t qHash(const QV4Debugger::BreakPoint &b, size_t seed = 0) noexcept
{
    return qHash(b.fileName, seed) ^ b.lineNumber;
}

inline bool operator==(const QV4Debugger::BreakPoint &a,
                       const QV4Debugger::BreakPoint &b)
{
    return a.lineNumber == b.lineNumber && a.fileName == b.fileName;
}

QV4Debugger::QV4Debugger(QV4::ExecutionEngine *engine)
    : m_engine(engine)
    , m_state(Running)
    , m_stepping(NotStepping)
    , m_pauseRequested(false)
    , m_haveBreakPoints(false)
    , m_breakOnThrow(false)
    , m_returnedValue(engine, QV4::Value::undefinedValue())
    , m_gatherSources(nullptr)
    , m_runningJob(nullptr)
    , m_collector(engine)
{
    static int debuggerId = qRegisterMetaType<QV4Debugger*>();
    static int pauseReasonId = qRegisterMetaType<QV4Debugger::PauseReason>();
    Q_UNUSED(debuggerId);
    Q_UNUSED(pauseReasonId);
    connect(this, &QV4Debugger::scheduleJob,
            this, &QV4Debugger::runJobUnpaused, Qt::QueuedConnection);
}

QV4::ExecutionEngine *QV4Debugger::engine() const
{
    return m_engine;
}

const QV4DataCollector *QV4Debugger::collector() const
{
    return &m_collector;
}

QV4DataCollector *QV4Debugger::collector()
{
    return &m_collector;
}

void QV4Debugger::pause()
{
    QMutexLocker locker(&m_lock);
    if (m_state == Paused)
        return;
    m_pauseRequested = true;
}

void QV4Debugger::resume(Speed speed)
{
    QMutexLocker locker(&m_lock);
    if (m_state != Paused)
        return;

    if (!m_returnedValue.isUndefined())
        m_returnedValue.set(m_engine, QV4::Encode::undefined());

    m_currentFrame = m_engine->currentStackFrame;
    m_stepping = speed;
    m_runningCondition.wakeAll();
}

QV4Debugger::State QV4Debugger::state() const
{
    return m_state;
}

void QV4Debugger::addBreakPoint(const QString &fileName, int lineNumber, const QString &condition)
{
    QMutexLocker locker(&m_lock);
    m_breakPoints.insert(BreakPoint(fileName.mid(fileName.lastIndexOf('/') + 1),
                                            lineNumber), condition);
    m_haveBreakPoints = true;
}

void QV4Debugger::removeBreakPoint(const QString &fileName, int lineNumber)
{
    QMutexLocker locker(&m_lock);
    m_breakPoints.remove(BreakPoint(fileName.mid(fileName.lastIndexOf('/') + 1),
                                            lineNumber));
    m_haveBreakPoints = !m_breakPoints.isEmpty();
}

void QV4Debugger::setBreakOnThrow(bool onoff)
{
    QMutexLocker locker(&m_lock);

    m_breakOnThrow = onoff;
}

void QV4Debugger::clearPauseRequest()
{
    QMutexLocker locker(&m_lock);
    m_pauseRequested = false;
}

QV4Debugger::ExecutionState QV4Debugger::currentExecutionState() const
{
    ExecutionState state;
    state.fileName = QUrl(getFunction()->sourceFile()).fileName();
    state.lineNumber = engine()->currentStackFrame->lineNumber();

    return state;
}

bool QV4Debugger::pauseAtNextOpportunity() const {
    return m_pauseRequested || m_haveBreakPoints || m_gatherSources || m_stepping >= StepOver;
}

QVector<QV4::StackFrame> QV4Debugger::stackTrace(int frameLimit) const
{
    return m_engine->stackTrace(frameLimit);
}

void QV4Debugger::maybeBreakAtInstruction()
{
    if (m_runningJob) // do not re-enter when we're doing a job for the debugger.
        return;

    QMutexLocker locker(&m_lock);

    if (m_gatherSources) {
        m_gatherSources->run();
        delete m_gatherSources;
        m_gatherSources = nullptr;
    }

    switch (m_stepping) {
    case StepOver:
        if (m_currentFrame != m_engine->currentStackFrame)
            break;
        Q_FALLTHROUGH();
    case StepIn:
        pauseAndWait(Step);
        return;
    case StepOut:
    case NotStepping:
        break;
    }

    if (m_pauseRequested) { // Serve debugging requests from the agent
        m_pauseRequested = false;
        pauseAndWait(PauseRequest);
    } else if (m_haveBreakPoints) {
        if (QV4::Function *f = getFunction()) {
            // lineNumber will be negative for Ret instructions, so those won't match
            const int lineNumber = engine()->currentStackFrame->lineNumber();
            if (reallyHitTheBreakPoint(f->sourceFile(), lineNumber))
                pauseAndWait(BreakPointHit);
        }
    }
}

void QV4Debugger::enteringFunction()
{
    if (m_runningJob)
        return;
    QMutexLocker locker(&m_lock);

    if (m_stepping == StepIn)
        m_currentFrame = m_engine->currentStackFrame;
}

void QV4Debugger::leavingFunction(const QV4::ReturnedValue &retVal)
{
    if (m_runningJob)
        return;
    Q_UNUSED(retVal); // TODO

    QMutexLocker locker(&m_lock);

    if (m_stepping != NotStepping && m_currentFrame == m_engine->currentStackFrame) {
        m_currentFrame = m_currentFrame->parentFrame();
        m_stepping = StepOver;
        m_returnedValue.set(m_engine, retVal);
    }
}

void QV4Debugger::aboutToThrow()
{
    if (!m_breakOnThrow)
        return;

    if (m_runningJob) // do not re-enter when we're doing a job for the debugger.
        return;

    QMutexLocker locker(&m_lock);
    pauseAndWait(Throwing);
}

QV4::Function *QV4Debugger::getFunction() const
{
    if (m_engine->currentStackFrame)
        return m_engine->currentStackFrame->v4Function;
    else
        return m_engine->globalCode;
}

void QV4Debugger::runJobUnpaused()
{
    QMutexLocker locker(&m_lock);
    if (m_runningJob)
        m_runningJob->run();
    m_jobIsRunning.wakeAll();
}

void QV4Debugger::pauseAndWait(PauseReason reason)
{
    if (m_runningJob)
        return;

    m_state = Paused;
    emit debuggerPaused(this, reason);

    while (true) {
        m_runningCondition.wait(&m_lock);
        if (m_runningJob) {
            m_runningJob->run();
            m_jobIsRunning.wakeAll();
        } else {
            break;
        }
    }

    m_state = Running;
}

bool QV4Debugger::reallyHitTheBreakPoint(const QString &filename, int linenr)
{
    const auto it = m_breakPoints.constFind(
                BreakPoint(QUrl(filename).fileName(), linenr));
    if (it == m_breakPoints.cend())
        return false;
    QString condition = it.value();
    if (condition.isEmpty())
        return true;

    Q_ASSERT(m_runningJob == nullptr);
    EvalJob evilJob(m_engine, condition);
    m_runningJob = &evilJob;
    m_runningJob->run();
    m_runningJob = nullptr;

    return evilJob.resultAsBoolean();
}

void QV4Debugger::runInEngine(QV4DebugJob *job)
{
    QMutexLocker locker(&m_lock);
    runInEngine_havingLock(job);
}

void QV4Debugger::runInEngine_havingLock(QV4DebugJob *job)
{
    Q_ASSERT(job);
    Q_ASSERT(m_runningJob == nullptr);

    m_runningJob = job;
    if (state() == Paused)
        m_runningCondition.wakeAll();
    else
        emit scheduleJob();
    m_jobIsRunning.wait(&m_lock);
    m_runningJob = nullptr;
}

QT_END_NAMESPACE

#include "moc_qv4debugger.cpp"

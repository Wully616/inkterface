#include "activity-detector.hpp"

#include <QCoreApplication>
#include <QEvent>

#ifdef INKTERFACE_HAVE_KIDLETIME
#include <KIdleTime>
#endif

ActivityDetector::ActivityDetector(QObject *parent)
    : QObject(parent)
    , m_application(QCoreApplication::instance())
{
    m_idleTimer.start();
    if (m_application) {
        m_application->installEventFilter(this);
    }
    idleMilliseconds();
}

ActivityDetector::~ActivityDetector()
{
    if (m_application) {
        m_application->removeEventFilter(this);
    }
}

qint64 ActivityDetector::idleMilliseconds() const
{
#ifdef INKTERFACE_HAVE_KIDLETIME
    if (auto *idleTime = KIdleTime::instance()) {
        const qint64 idle = idleTime->idleTime();
        if (idle >= 0) {
            setBackend(QStringLiteral("KIdleTime"));
            return idle;
        }
    }
#endif
    setBackend(QStringLiteral("application"));
    return m_idleTimer.elapsed();
}

QString ActivityDetector::backend() const
{
    return m_backend;
}

bool ActivityDetector::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
    case QEvent::Wheel:
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::ApplicationActivate:
    case QEvent::ApplicationDeactivate:
    case QEvent::WindowActivate:
    case QEvent::WindowDeactivate:
        m_idleTimer.restart();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void ActivityDetector::setBackend(const QString &backend) const
{
    if (m_backend == backend) {
        return;
    }
    m_backend = backend;
    emit const_cast<ActivityDetector *>(this)->backendChanged();
}

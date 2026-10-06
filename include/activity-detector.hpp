#ifndef ACTIVITY_DETECTOR_HPP
#define ACTIVITY_DETECTOR_HPP

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>

class QCoreApplication;
class QEvent;

class ActivityDetector : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString backend READ backend NOTIFY backendChanged)

  public:
    explicit ActivityDetector(QObject *parent = nullptr);
    ~ActivityDetector() override;

    qint64 idleMilliseconds() const;
    QString backend() const;
    bool eventFilter(QObject *watched, QEvent *event) override;

  signals:
    void backendChanged();

  private:
    void setBackend(const QString &backend) const;

    QElapsedTimer m_idleTimer;
    QPointer<QCoreApplication> m_application;
    mutable QString m_backend;
};

#endif // ACTIVITY_DETECTOR_HPP

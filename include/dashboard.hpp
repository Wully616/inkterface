#ifndef DASHBOARD_HPP
#define DASHBOARD_HPP

#include <memory>
#include <vector>

#include <QElapsedTimer>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPainter>
#include <QRectF>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "frame-transport.hpp"

class ActivityDetector;
class PanelState;

struct DashboardRenderContext {
    PanelState *state = nullptr;
    bool lcd5b = false;
    qint64 elapsedMilliseconds = 0;
    QColor cardBackground;
    QColor foreground;
    QColor muted;
    QColor accent;
};

class DashboardWidget
{
  public:
    virtual ~DashboardWidget() = default;
    virtual QString type() const = 0;
    virtual QString label() const = 0;
    virtual QVariantMap defaultSettings() const { return {}; }
    virtual void render(QPainter &painter, const QRectF &bounds, const QVariantMap &settings,
                        const DashboardRenderContext &context) const = 0;
};

class WidgetRegistry
{
  public:
    void add(std::unique_ptr<DashboardWidget> widget);
    const DashboardWidget *find(const QString &type) const;
    QVariantList descriptions() const;

  private:
    std::vector<std::unique_ptr<DashboardWidget>> m_widgets;
};
class DashboardRenderer
{
  public:
    DashboardRenderer(const WidgetRegistry &registry, PanelState *state);
    PanelFrame render(const QJsonArray &widgets, bool lcd5b,
                      const QJsonObject &background) const;

  private:
    const WidgetRegistry &m_registry;
    PanelState *m_state = nullptr;
    mutable QHash<QString, QImage> m_backgroundImages;
    QElapsedTimer m_clock;
};

class Dashboard : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString activeProfile READ activeProfile NOTIFY activeProfileChanged)
    Q_PROPERTY(bool idle READ isIdle NOTIFY idleChanged)
    Q_PROPERTY(int idleTimeoutSeconds READ idleTimeoutSeconds WRITE setIdleTimeoutSeconds
                   NOTIFY revisionChanged)
    Q_PROPERTY(bool backlightOffOnIdle READ backlightOffOnIdle WRITE setBacklightOffOnIdle
                   NOTIFY revisionChanged)
    Q_PROPERTY(QString activityBackend READ activityBackend NOTIFY activityBackendChanged)

  public:
    explicit Dashboard(PanelState *state, QObject *parent = nullptr);

    int revision() const { return m_revision; }
    QString activeProfile() const { return m_activeProfile; }
    bool isIdle() const { return m_idle; }
    int idleTimeoutSeconds() const;
    bool backlightOffOnIdle() const;
    QString activityBackend() const;

    PanelFrame renderFrame(bool lcd5b);

    Q_INVOKABLE QVariantList widgets(const QString &profile, bool lcd5b) const;
    Q_INVOKABLE QVariantList widgetTypes() const;
    Q_INVOKABLE QVariantMap background(const QString &profile, bool lcd5b) const;
    Q_INVOKABLE QString chooseBackgroundImage(bool lcd5b);
    Q_INVOKABLE QString chooseColor(const QString &initialColor);
    Q_INVOKABLE void setBackground(const QString &profile, bool lcd5b, const QString &key,
                                   const QVariant &value);
    Q_INVOKABLE void setBackgroundImage(const QString &profile, bool lcd5b,
                                        const QString &imagePath);
    Q_INVOKABLE void addWidget(const QString &profile, bool lcd5b, const QString &type);
    Q_INVOKABLE void removeWidget(const QString &profile, bool lcd5b, const QString &id);
    Q_INVOKABLE void setWidgetBounds(const QString &profile, bool lcd5b, const QString &id,
                                     double x, double y, double width, double height);
    Q_INVOKABLE void setWidgetSetting(const QString &profile, bool lcd5b, const QString &id,
                                      const QString &key, const QVariant &value);
    Q_INVOKABLE QVariantMap rule(const QString &condition) const;
    Q_INVOKABLE void setRuleEnabled(const QString &condition, bool enabled);
    Q_INVOKABLE void setRulePriority(const QString &condition, int priority);
    Q_INVOKABLE void setRuleProfile(const QString &condition, const QString &profile);

  public slots:
    void setIdleTimeoutSeconds(int seconds);
    void setBacklightOffOnIdle(bool enabled);

  signals:
    void revisionChanged();
    void activeProfileChanged();
    void idleChanged();
    void activityBackendChanged();

  private:
    void registerBuiltinWidgets();
    QJsonObject defaultConfiguration() const;
    void loadConfiguration();
    void refreshExternalConfiguration();
    void saveConfiguration();
    void updateRules();
    void updateActivity();
    void commitConfiguration();
    QJsonArray widgetsFor(const QString &profile, bool lcd5b) const;
    QJsonObject backgroundFor(const QString &profile, bool lcd5b) const;
    QJsonObject findRule(const QString &condition) const;
    bool configurationValid(const QJsonObject &configuration) const;

    PanelState *m_state = nullptr;
    ActivityDetector *m_activity = nullptr;
    QTimer m_refreshTimer;
    WidgetRegistry m_registry;
    DashboardRenderer m_renderer;
    QJsonObject m_configuration;
    QByteArray m_serializedConfiguration;
    QString m_activeProfile = QStringLiteral("normal");
    int m_revision = 0;
    bool m_idle = false;
};

#endif // DASHBOARD_HPP

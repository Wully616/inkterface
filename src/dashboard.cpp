#include "dashboard.hpp"

#include <cmath>
#include <limits>
#include <utility>

#include <QBuffer>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainterPath>
#include <QSettings>
#include <QUuid>

#include "activity-detector.hpp"
#include "panel-state.hpp"

using namespace Qt::Literals::StringLiterals;

namespace
{
constexpr auto DASHBOARD_SETTINGS_KEY = "dashboardConfig";
constexpr int DASHBOARD_VERSION = 1;
constexpr int LCD_WIDTH = 1024;
constexpr int LCD_HEIGHT = 600;
constexpr int EINK_WIDTH = 648;
constexpr int EINK_HEIGHT = 480;
constexpr qsizetype MAX_JPEG_BYTES = 512 * 1024;

QJsonObject makeWidget(const QString &type, double x, double y, double width, double height,
                       const QVariantMap &settings = {})
{
    return {{u"id"_s, QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {u"type"_s, type},
            {u"x"_s, x},
            {u"y"_s, y},
            {u"width"_s, width},
            {u"height"_s, height},
            {u"settings"_s, QJsonObject::fromVariantMap(settings)}};
}

QString variantKey(bool lcd5b)
{
    return lcd5b ? u"lcd5b"_s : u"eink"_s;
}

QRectF cardContents(QPainter &painter, const QRectF &bounds, const QString &title,
                    const DashboardRenderContext &context)
{
    const QColor background = context.lcd5b ? QColor(u"#202a36"_s) : QColor(Qt::white);
    const QColor foreground = context.lcd5b ? QColor(u"#f3f6fa"_s) : QColor(u"#111111"_s);
    const QColor muted = context.lcd5b ? QColor(u"#a8b3c2"_s) : QColor(u"#444444"_s);
    const QColor accent = context.lcd5b ? QColor(u"#36cfc9"_s) : QColor(u"#222222"_s);
    const qreal border = qBound<qreal>(1, bounds.width() / 220.0, 3);
    const qreal padding = qBound<qreal>(5, qMin(bounds.width(), bounds.height()) * 0.07, 18);
    const QRectF card = bounds.adjusted(border, border, -border, -border);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, context.lcd5b);
    painter.setPen(QPen(accent, border));
    painter.setBrush(background);
    painter.drawRoundedRect(card, context.lcd5b ? 7 : 1, context.lcd5b ? 7 : 1);

    const qreal headerHeight = qBound<qreal>(14, card.height() * 0.22, 30);
    QFont labelFont(u"IBM Plex Mono"_s, -1, QFont::Bold);
    labelFont.setPixelSize(qBound(9, qRound(headerHeight * 0.52), 20));
    painter.setFont(labelFont);
    painter.setPen(muted);
    painter.drawText(QRectF(card.left() + padding, card.top() + padding * 0.35,
                            card.width() - padding * 2, headerHeight),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     QFontMetrics(labelFont).elidedText(title.toUpper(), Qt::ElideRight,
                                                        qMax(0, qRound(card.width() - padding * 2))));
    painter.setPen(accent);
    painter.drawLine(QPointF(card.left() + padding, card.top() + padding + headerHeight),
                     QPointF(card.right() - padding, card.top() + padding + headerHeight));
    painter.restore();

    return card.adjusted(padding, padding + headerHeight, -padding, -padding);
}

class HostnameWidget final : public DashboardWidget
{
  public:
    QString type() const override { return u"hostname"_s; }
    QString label() const override { return u"Hostname"_s; }

    void render(QPainter &painter, const QRectF &bounds, const QVariantMap &,
                const DashboardRenderContext &context) const override
    {
        const QRectF content = cardContents(painter, bounds, label(), context);
        QFont font(u"IBM Plex Mono"_s, -1, QFont::Bold);
        font.setPixelSize(qBound(12, qRound(content.height() * 0.52), 48));
        painter.save();
        painter.setFont(font);
        painter.setPen(context.lcd5b ? QColor(u"#f3f6fa"_s) : QColor(u"#111111"_s));
        painter.drawText(content, Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(font).elidedText(context.state->hostName(), Qt::ElideRight,
                                                       qMax(0, qRound(content.width()))));
        painter.restore();
    }
};

class MetricWidget final : public DashboardWidget
{
  public:
    QString type() const override { return u"metric"_s; }
    QString label() const override { return u"System metric"_s; }
    QVariantMap defaultSettings() const override
    {
        return {{u"collector"_s, u"CPU Temperature"_s}, {u"style"_s, u"value"_s}};
    }

    void render(QPainter &painter, const QRectF &bounds, const QVariantMap &settings,
                const DashboardRenderContext &context) const override
    {
        const QString collector = settings.value(u"collector"_s).toString();
        const QRectF content = cardContents(painter, bounds, collector, context);
        const QString value = context.state->metricValue(collector);
        const bool sparkline = settings.value(u"style"_s).toString() == u"sparkline"_s;
        const int fieldIndex = settings.value(u"fieldIndex"_s, -1).toInt();
        const QRectF valueRect = sparkline ? content.adjusted(0, 0, 0, -content.height() * 0.38)
                                          : content;
        QFont valueFont(u"IBM Plex Mono"_s, -1, QFont::Bold);
        valueFont.setPixelSize(qBound(12, qRound(valueRect.height() * 0.58), 42));
        painter.save();
        painter.setFont(valueFont);
        painter.setPen(context.lcd5b ? QColor(u"#f3f6fa"_s) : QColor(u"#111111"_s));
        painter.drawText(valueRect, Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(valueFont).elidedText(value, Qt::ElideRight,
                                                            qMax(0, qRound(valueRect.width()))));

        if (sparkline && fieldIndex >= 0) {
            const auto fields = context.state->fields();
            if (fieldIndex < fields.size()) {
                const auto points = fields[fieldIndex]->points();
                if (points.size() > 1) {
                    const QRectF graph = content.adjusted(0, content.height() * 0.68, 0, 0);
                    const double xRange = fields[fieldIndex]->xMax() - fields[fieldIndex]->xMin();
                    const double yRange = fields[fieldIndex]->yMax() - fields[fieldIndex]->yMin();
                    QPainterPath path;
                    for (qsizetype i = 0; i < points.size(); ++i) {
                        const double x = xRange == 0 ? 0.5 :
                            (points[i].x() - fields[fieldIndex]->xMin()) / xRange;
                        const double y = yRange == 0 ? 0.5 :
                            (points[i].y() - fields[fieldIndex]->yMin()) / yRange;
                        const QPointF position(graph.left() + x * graph.width(),
                                               graph.bottom() - y * graph.height());
                        if (i == 0) {
                            path.moveTo(position);
                        } else {
                            path.lineTo(position);
                        }
                    }
                    painter.setPen(QPen(context.lcd5b ? QColor(u"#36cfc9"_s)
                                                        : QColor(u"#333333"_s), 2));
                    painter.drawPath(path);
                }
            }
        }
        painter.restore();
    }
};

class BoxArtWidget final : public DashboardWidget
{
  public:
    QString type() const override { return u"boxArt"_s; }
    QString label() const override { return u"Game box art"_s; }
    QVariantMap defaultSettings() const override { return {{u"fit"_s, u"contain"_s}}; }

    void render(QPainter &painter, const QRectF &bounds, const QVariantMap &settings,
                const DashboardRenderContext &context) const override
    {
        const QRectF content = cardContents(painter, bounds, label(), context);
        painter.save();
        painter.setClipRect(content);
        if (!context.artworkEnabled) {
            painter.setPen(context.lcd5b ? QColor(u"#a8b3c2"_s) : QColor(u"#555555"_s));
            painter.drawText(content, Qt::AlignCenter | Qt::TextWordWrap, u"BOX ART OFF"_s);
            painter.restore();
            return;
        }
        const QImage image = context.state->currentBoxArt();
        if (image.isNull()) {
            painter.setPen(context.lcd5b ? QColor(u"#a8b3c2"_s) : QColor(u"#555555"_s));
            painter.drawText(content, Qt::AlignCenter | Qt::TextWordWrap, u"BOX ART\nNOT AVAILABLE"_s);
            painter.restore();
            return;
        }
        const QString fit = settings.value(u"fit"_s, u"contain"_s).toString();
        if (fit == u"stretch"_s) {
            painter.drawImage(content, image);
        } else {
            const auto mode = fit == u"cover"_s ? Qt::KeepAspectRatioByExpanding
                                                  : Qt::KeepAspectRatio;
            const QImage scaled = image.scaled(content.size().toSize(), mode,
                                               Qt::SmoothTransformation);
            painter.drawImage(QRectF(content.center().x() - scaled.width() / 2.0,
                                     content.center().y() - scaled.height() / 2.0,
                                     scaled.width(), scaled.height()), scaled);
        }
        painter.restore();
    }
};

class GameTitleWidget final : public DashboardWidget
{
  public:
    QString type() const override { return u"gameTitle"_s; }
    QString label() const override { return u"Current game title"_s; }

    void render(QPainter &painter, const QRectF &bounds, const QVariantMap &,
                const DashboardRenderContext &context) const override
    {
        const QRectF content = cardContents(painter, bounds, label(), context);
        const QString title = context.state->currentGameTitle();
        QFont font(u"IBM Plex Mono"_s, -1, QFont::Bold);
        font.setPixelSize(qBound(12, qRound(content.height() * 0.48), 48));
        painter.save();
        painter.setFont(font);
        painter.setPen(context.lcd5b ? QColor(u"#f3f6fa"_s) : QColor(u"#111111"_s));
        painter.drawText(content, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap,
                         title.isEmpty() ? u"NO GAME RUNNING"_s : title);
        painter.restore();
    }
};

class PlaytimeWidget final : public DashboardWidget
{
  public:
    QString type() const override { return u"playtime"_s; }
    QString label() const override { return u"Playtime"_s; }

    void render(QPainter &painter, const QRectF &bounds, const QVariantMap &,
                const DashboardRenderContext &context) const override
    {
        const QRectF content = cardContents(painter, bounds, label(), context);
        const double minutes = context.state->currentGamePlaytimeMinutes();
        const QString value = minutes < 0 ? u"--"_s
            : minutes < 60 ? u"%1 MIN"_s.arg(QString::number(minutes, 'f', 0))
                           : u"%1 HRS"_s.arg(QString::number(minutes / 60.0, 'f', 1));
        QFont font(u"IBM Plex Mono"_s, -1, QFont::Bold);
        font.setPixelSize(qBound(12, qRound(content.height() * 0.56), 42));
        painter.save();
        painter.setFont(font);
        painter.setPen(context.lcd5b ? QColor(u"#f3f6fa"_s) : QColor(u"#111111"_s));
        painter.drawText(content, Qt::AlignVCenter | Qt::AlignLeft, value);
        painter.restore();
    }
};

class AchievementsWidget final : public DashboardWidget
{
  public:
    QString type() const override { return u"achievements"_s; }
    QString label() const override { return u"Achievement progress"_s; }

    void render(QPainter &painter, const QRectF &bounds, const QVariantMap &,
                const DashboardRenderContext &context) const override
    {
        const QRectF content = cardContents(painter, bounds, label(), context);
        const auto achievements = context.state->currentGameAchievements();
        const QColor foreground = context.lcd5b ? QColor(u"#f3f6fa"_s) : QColor(u"#111111"_s);
        const QColor accent = context.lcd5b ? QColor(u"#36cfc9"_s) : QColor(u"#222222"_s);
        painter.save();
        if (achievements.second <= 0) {
            painter.setPen(foreground);
            painter.drawText(content, Qt::AlignVCenter | Qt::AlignLeft, u"-- / --"_s);
            painter.restore();
            return;
        }
        const QString value = u"%1 / %2"_s.arg(QString::number(achievements.first),
                                                QString::number(achievements.second));
        QFont font(u"IBM Plex Mono"_s, -1, QFont::Bold);
        font.setPixelSize(qBound(11, qRound(content.height() * 0.38), 34));
        painter.setFont(font);
        painter.setPen(foreground);
        painter.drawText(QRectF(content.left(), content.top(), content.width(),
                                content.height() * 0.55), Qt::AlignVCenter | Qt::AlignLeft, value);
        const QRectF bar(content.left(), content.bottom() - qMax<qreal>(5, content.height() * 0.2),
                         content.width(), qMax<qreal>(5, content.height() * 0.2));
        painter.setPen(QPen(accent, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(bar);
        const int unlocked = qBound(0, achievements.first, achievements.second);
        painter.fillRect(bar.adjusted(2, 2, -bar.width() *
                                      (1.0 - static_cast<double>(unlocked) / achievements.second), -2),
                         accent);
        painter.restore();
    }
};

class ScreensaverWidget final : public DashboardWidget
{
  public:
    QString type() const override { return u"screensaver"_s; }
    QString label() const override { return u"Static screensaver"_s; }
    QVariantMap defaultSettings() const override { return {{u"text"_s, u"INKTERFACE"_s}}; }

    void render(QPainter &painter, const QRectF &bounds, const QVariantMap &settings,
                const DashboardRenderContext &context) const override
    {
        const QColor foreground = context.lcd5b ? QColor(u"#f3f6fa"_s) : QColor(u"#111111"_s);
        const QColor accent = context.lcd5b ? QColor(u"#36cfc9"_s) : QColor(u"#222222"_s);
        painter.save();
        painter.setPen(QPen(accent, context.lcd5b ? 3 : 2));
        painter.setBrush(context.lcd5b ? QColor(u"#101722"_s) : QColor(Qt::white));
        painter.drawRect(bounds.adjusted(2, 2, -2, -2));
        QFont titleFont(u"IBM Plex Mono"_s, -1, QFont::Bold);
        titleFont.setPixelSize(qBound(18, qRound(bounds.height() * 0.18), 78));
        painter.setFont(titleFont);
        painter.setPen(foreground);
        const QString title = settings.value(u"text"_s, u"INKTERFACE"_s).toString();
        painter.drawText(bounds.adjusted(12, 10, -12, -bounds.height() * 0.28),
                         Qt::AlignCenter | Qt::TextWordWrap, title);
        QFont subtitleFont(u"IBM Plex Mono"_s, -1, QFont::Medium);
        subtitleFont.setPixelSize(qBound(11, qRound(bounds.height() * 0.07), 28));
        painter.setFont(subtitleFont);
        painter.setPen(accent);
        painter.drawText(bounds.adjusted(12, bounds.height() * 0.62, -12, -12), Qt::AlignCenter,
                         u"SYSTEM IDLE"_s);
        painter.restore();
    }
};

QByteArray packMono(const QImage &image)
{
    const QImage mono = image.convertToFormat(QImage::Format_Grayscale8)
                            .convertToFormat(QImage::Format_Mono, Qt::DiffuseDither);
    const bool oneIsInk = qGray(mono.color(1)) < qGray(mono.color(0));
    const int inkIndex = oneIsInk ? 1 : 0;
    QByteArray bytes(image.width() * image.height() / 8, char(0));
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (mono.pixelIndex(x, y) == inkIndex) {
                const qsizetype offset = static_cast<qsizetype>(y) * image.width() + x;
                bytes[offset / 8] = static_cast<char>(
                    static_cast<unsigned char>(bytes.at(offset / 8)) | (1U << (7 - x % 8)));
            }
        }
    }
    return bytes;
}

QByteArray encodeJpeg(const QImage &image)
{
    for (const int quality : {84, 74, 64, 54}) {
        QByteArray jpeg;
        QBuffer buffer(&jpeg);
        if (buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "JPEG", quality) &&
            jpeg.size() <= MAX_JPEG_BYTES) {
            return jpeg;
        }
    }
    return {};
}

QJsonArray safeWidgets(const QJsonValue &value)
{
    return value.isArray() ? value.toArray() : QJsonArray{};
}

} // namespace

void WidgetRegistry::add(std::unique_ptr<DashboardWidget> widget)
{
    m_widgets.push_back(std::move(widget));
}

const DashboardWidget *WidgetRegistry::find(const QString &type) const
{
    for (const auto &widget : m_widgets) {
        if (widget->type() == type) {
            return widget.get();
        }
    }
    return nullptr;
}

QVariantList WidgetRegistry::descriptions() const
{
    QVariantList types;
    types.reserve(static_cast<qsizetype>(m_widgets.size()));
    for (const auto &widget : m_widgets) {
        types.append(QVariantMap{{u"type"_s, widget->type()}, {u"label"_s, widget->label()}});
    }
    return types;
}

DashboardRenderer::DashboardRenderer(const WidgetRegistry &registry, PanelState *state)
    : m_registry(registry)
    , m_state(state)
{
    m_clock.start();
}

PanelFrame DashboardRenderer::render(const QJsonArray &widgets, bool lcd5b,
                                     bool artworkEnabled) const
{
    const QSize size(lcd5b ? LCD_WIDTH : EINK_WIDTH, lcd5b ? LCD_HEIGHT : EINK_HEIGHT);
    QImage image(size, QImage::Format_RGB32);
    image.fill(lcd5b ? QColor(u"#101722"_s) : QColor(Qt::white));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const DashboardRenderContext context{m_state, lcd5b, artworkEnabled, m_clock.elapsed()};
    for (const auto &value : widgets) {
        const QJsonObject instance = value.toObject();
        const auto *widget = m_registry.find(instance.value(u"type"_s).toString());
        if (!widget) {
            continue;
        }
        const qreal x = qBound(0.0, instance.value(u"x"_s).toDouble(), 1.0);
        const qreal y = qBound(0.0, instance.value(u"y"_s).toDouble(), 1.0);
        const qreal width = qBound(0.0, instance.value(u"width"_s).toDouble(), 1.0 - x);
        const qreal height = qBound(0.0, instance.value(u"height"_s).toDouble(), 1.0 - y);
        widget->render(painter, QRectF(x * size.width(), y * size.height(),
                                       width * size.width(), height * size.height()),
                       instance.value(u"settings"_s).toObject().toVariantMap(), context);
    }
    painter.end();

    PanelFrame frame;
    frame.width = static_cast<quint16>(size.width());
    frame.height = static_cast<quint16>(size.height());
    if (lcd5b) {
        frame.encoding = PanelFrame::Encoding::Jpeg;
        frame.bytes = encodeJpeg(image);
    } else {
        frame.encoding = PanelFrame::Encoding::Monochrome1bpp;
        frame.bytes = packMono(image);
    }
    return frame;
}
Dashboard::Dashboard(PanelState *state, QObject *parent)
    : QObject(parent)
    , m_state(state)
    , m_activity(new ActivityDetector(this))
    , m_renderer(m_registry, state)
{
    registerBuiltinWidgets();
    loadConfiguration();
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        refreshExternalConfiguration();
        updateRules();
    });
    connect(m_activity, &ActivityDetector::backendChanged, this,
            &Dashboard::activityBackendChanged);
    connect(m_state, &PanelState::dataChanged, this, &Dashboard::updateRules);
    m_refreshTimer.setInterval(1000);
    m_refreshTimer.start();
    updateRules();
}

void Dashboard::registerBuiltinWidgets()
{
    m_registry.add(std::make_unique<HostnameWidget>());
    m_registry.add(std::make_unique<MetricWidget>());
    m_registry.add(std::make_unique<BoxArtWidget>());
    m_registry.add(std::make_unique<GameTitleWidget>());
    m_registry.add(std::make_unique<PlaytimeWidget>());
    m_registry.add(std::make_unique<AchievementsWidget>());
    m_registry.add(std::make_unique<ScreensaverWidget>());
}

QJsonObject Dashboard::defaultConfiguration() const
{
    QJsonObject profiles;
    QJsonArray normal;
    normal.append(makeWidget(u"hostname"_s, 0.04, 0.025, 0.92, 0.12));

    const auto fields = m_state->fields();
    for (qsizetype i = 0; i < fields.size(); ++i) {
        const int column = static_cast<int>(i % 3);
        const int row = static_cast<int>(i / 3);
        const QString collector = fields[i]->collector()
                                     ? fields[i]->collector()->displayName()
                                     : u"--"_s;
        QVariantMap settings{{u"collector"_s, collector},
                             {u"fieldIndex"_s, static_cast<int>(i)},
                             {u"style"_s, fields[i]->depth() > 0 ? u"sparkline"_s : u"value"_s}};
        normal.append(makeWidget(u"metric"_s, 0.04 + column * 0.31, 0.18 + row * 0.255,
                                 0.29, 0.225, settings));
    }
    profiles.insert(u"normal"_s, QJsonObject{{u"eink"_s, normal}, {u"lcd5b"_s, normal}});

    const auto makeGameWidgets = [this](bool lcd5b) {
        QJsonArray layout;
        const double artWidth = lcd5b ? 0.34 : 0.39;
        layout.append(makeWidget(u"boxArt"_s, 0.03, 0.04, artWidth, 0.92));
        const double detailX = lcd5b ? 0.41 : 0.47;
        const double detailWidth = 0.96 - detailX;
        layout.append(makeWidget(u"gameTitle"_s, detailX, 0.04, detailWidth, 0.27));
        layout.append(makeWidget(u"playtime"_s, detailX, 0.36, detailWidth * 0.48, 0.20));
        layout.append(makeWidget(u"achievements"_s, detailX + detailWidth * 0.52, 0.36,
                                 detailWidth * 0.48, 0.20));
        layout.append(makeWidget(u"metric"_s, detailX, 0.62, detailWidth * 0.48, 0.30,
                                 {{u"collector"_s, u"CPU"_s}, {u"style"_s, u"value"_s}}));
        layout.append(makeWidget(u"metric"_s, detailX + detailWidth * 0.52, 0.62,
                                 detailWidth * 0.48, 0.30,
                                 {{u"collector"_s, u"Memory"_s}, {u"style"_s, u"value"_s}}));
        return layout;
    };
    profiles.insert(u"inGame"_s,
                    QJsonObject{{u"eink"_s, makeGameWidgets(false)},
                                {u"lcd5b"_s, makeGameWidgets(true)}});

    const QJsonArray idle = {makeWidget(u"screensaver"_s, 0.05, 0.08, 0.90, 0.84)};
    profiles.insert(u"idle"_s, QJsonObject{{u"eink"_s, idle}, {u"lcd5b"_s, idle}});

    const QJsonArray rules = {
        QJsonObject{{u"condition"_s, u"gameRunning"_s}, {u"profile"_s, u"inGame"_s},
                    {u"enabled"_s, true}, {u"priority"_s, 100}},
        QJsonObject{{u"condition"_s, u"idle"_s}, {u"profile"_s, u"idle"_s},
                    {u"enabled"_s, true}, {u"priority"_s, 50}},
    };
    return {{u"version"_s, DASHBOARD_VERSION},
            {u"idleTimeoutSeconds"_s, 300},
            {u"backlightOffOnIdle"_s, true},
            {u"profiles"_s, profiles},
            {u"rules"_s, rules}};
}

bool Dashboard::configurationValid(const QJsonObject &configuration) const
{
    return configuration.value(u"version"_s).toInt() == DASHBOARD_VERSION &&
           configuration.value(u"profiles"_s).isObject() &&
           configuration.value(u"rules"_s).isArray();
}

void Dashboard::loadConfiguration()
{
    QSettings settings;
    settings.sync();
    const QByteArray saved = settings.value(QLatin1String(DASHBOARD_SETTINGS_KEY)).toString().toUtf8();
    const auto document = QJsonDocument::fromJson(saved);
    if (!saved.isEmpty() && document.isObject() && configurationValid(document.object())) {
        m_configuration = document.object();
        m_serializedConfiguration = saved;
        return;
    }
    m_configuration = defaultConfiguration();
    saveConfiguration();
}

void Dashboard::refreshExternalConfiguration()
{
    QSettings settings;
    settings.sync();
    const QByteArray saved = settings.value(QLatin1String(DASHBOARD_SETTINGS_KEY)).toString().toUtf8();
    if (saved.isEmpty() || saved == m_serializedConfiguration) {
        return;
    }
    const auto document = QJsonDocument::fromJson(saved);
    if (!document.isObject() || !configurationValid(document.object())) {
        return;
    }
    m_configuration = document.object();
    m_serializedConfiguration = saved;
    ++m_revision;
    emit revisionChanged();
    updateRules();
}

void Dashboard::saveConfiguration()
{
    const QByteArray saved = QJsonDocument(m_configuration).toJson(QJsonDocument::Compact);
    QSettings settings;
    settings.setValue(QLatin1String(DASHBOARD_SETTINGS_KEY), QString::fromUtf8(saved));
    settings.sync();
    m_serializedConfiguration = saved;
}

void Dashboard::commitConfiguration()
{
    saveConfiguration();
    ++m_revision;
    emit revisionChanged();
    updateRules();
}

void Dashboard::updateActivity()
{
    const bool idle = m_activity->idleMilliseconds() >=
                      static_cast<qint64>(idleTimeoutSeconds()) * 1000;
    if (idle != m_idle) {
        m_idle = idle;
        emit idleChanged();
    }
}

void Dashboard::updateRules()
{
    updateActivity();
    QString profile = u"normal"_s;
    int priority = std::numeric_limits<int>::min();
    const QJsonObject gameRule = findRule(u"gameRunning"_s);
    const QString gameProfile = gameRule.value(u"profile"_s).toString();
    if (m_state->gameRunning() && gameRule.value(u"enabled"_s).toBool() &&
        m_configuration.value(u"profiles"_s).toObject().contains(gameProfile)) {
        profile = gameRule.value(u"profile"_s).toString();
        priority = gameRule.value(u"priority"_s).toInt();
    }
    const QJsonObject idleRule = findRule(u"idle"_s);
    const QString idleProfile = idleRule.value(u"profile"_s).toString();
    if (m_idle && idleRule.value(u"enabled"_s).toBool() &&
        idleRule.value(u"priority"_s).toInt() > priority &&
        m_configuration.value(u"profiles"_s).toObject().contains(idleProfile)) {
        profile = idleRule.value(u"profile"_s).toString();
    }
    if (profile != m_activeProfile) {
        m_activeProfile = profile;
        emit activeProfileChanged();
    }
}

int Dashboard::idleTimeoutSeconds() const
{
    return qBound(30, m_configuration.value(u"idleTimeoutSeconds"_s).toInt(300), 86400);
}

bool Dashboard::backlightOffOnIdle() const
{
    return m_configuration.value(u"backlightOffOnIdle"_s).toBool(true);
}

QString Dashboard::activityBackend() const
{
    return m_activity->backend();
}

QJsonArray Dashboard::widgetsFor(const QString &profile, bool lcd5b) const
{
    const QJsonObject profiles = m_configuration.value(u"profiles"_s).toObject();
    const QJsonObject layout = profiles.value(profile).toObject();
    return safeWidgets(layout.value(variantKey(lcd5b)));
}

QVariantList Dashboard::widgets(const QString &profile, bool lcd5b) const
{
    QVariantList result;
    const QJsonArray instances = widgetsFor(profile, lcd5b);
    result.reserve(instances.size());
    for (const auto &value : instances) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject instance = value.toObject();
        const QString type = instance.value(u"type"_s).toString();
        const auto *widget = m_registry.find(type);
        result.append(QVariantMap{{u"id"_s, instance.value(u"id"_s).toString()},
                                  {u"type"_s, type},
                                  {u"label"_s, widget ? widget->label() : type},
                                  {u"x"_s, instance.value(u"x"_s).toDouble()},
                                  {u"y"_s, instance.value(u"y"_s).toDouble()},
                                  {u"width"_s, instance.value(u"width"_s).toDouble()},
                                  {u"height"_s, instance.value(u"height"_s).toDouble()},
                                  {u"settings"_s, instance.value(u"settings"_s).toObject().toVariantMap()}});
    }
    return result;
}

QVariantList Dashboard::widgetTypes() const
{
    return m_registry.descriptions();
}

void Dashboard::addWidget(const QString &profile, bool lcd5b, const QString &type)
{
    const auto *widget = m_registry.find(type);
    if (!widget || !m_configuration.value(u"profiles"_s).toObject().contains(profile)) {
        return;
    }
    QJsonObject profiles = m_configuration.value(u"profiles"_s).toObject();
    QJsonObject layout = profiles.value(profile).toObject();
    QJsonArray instances = widgetsFor(profile, lcd5b);
    const int index = instances.size();
    const int column = index % 3;
    const int row = index / 3;
    const double x = 0.04 + column * 0.31;
    const double y = qMin(0.76, 0.15 + row * 0.24);
    double width = 0.29;
    double height = 0.20;
    if (type == u"screensaver"_s || type == u"boxArt"_s) {
        width = 0.34;
        height = 0.38;
    }
    instances.append(makeWidget(type, x, y, width, height, widget->defaultSettings()));
    layout.insert(variantKey(lcd5b), instances);
    profiles.insert(profile, layout);
    m_configuration.insert(u"profiles"_s, profiles);
    commitConfiguration();
}

void Dashboard::removeWidget(const QString &profile, bool lcd5b, const QString &id)
{
    QJsonArray instances = widgetsFor(profile, lcd5b);
    QJsonArray updated;
    bool removed = false;
    for (const auto &value : instances) {
        if (value.toObject().value(u"id"_s).toString() == id) {
            removed = true;
        } else {
            updated.append(value);
        }
    }
    if (!removed) {
        return;
    }
    QJsonObject profiles = m_configuration.value(u"profiles"_s).toObject();
    QJsonObject layout = profiles.value(profile).toObject();
    layout.insert(variantKey(lcd5b), updated);
    profiles.insert(profile, layout);
    m_configuration.insert(u"profiles"_s, profiles);
    commitConfiguration();
}

void Dashboard::setWidgetBounds(const QString &profile, bool lcd5b, const QString &id, double x,
                                double y, double width, double height)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
        !std::isfinite(height)) {
        return;
    }
    x = qBound(0.0, x, 0.92);
    y = qBound(0.0, y, 0.92);
    width = qBound(0.08, width, 1.0 - x);
    height = qBound(0.08, height, 1.0 - y);
    QJsonArray instances = widgetsFor(profile, lcd5b);
    bool changed = false;
    for (qsizetype i = 0; i < instances.size(); ++i) {
        QJsonObject instance = instances[i].toObject();
        if (instance.value(u"id"_s).toString() != id) {
            continue;
        }
        const QJsonObject bounds{{u"x"_s, x}, {u"y"_s, y}, {u"width"_s, width},
                                 {u"height"_s, height}};
        changed = instance.value(u"x"_s).toDouble() != x ||
                  instance.value(u"y"_s).toDouble() != y ||
                  instance.value(u"width"_s).toDouble() != width ||
                  instance.value(u"height"_s).toDouble() != height;
        if (changed) {
            for (auto it = bounds.begin(); it != bounds.end(); ++it) {
                instance.insert(it.key(), it.value());
            }
            instances[i] = instance;
        }
        break;
    }
    if (!changed) {
        return;
    }
    QJsonObject profiles = m_configuration.value(u"profiles"_s).toObject();
    QJsonObject layout = profiles.value(profile).toObject();
    layout.insert(variantKey(lcd5b), instances);
    profiles.insert(profile, layout);
    m_configuration.insert(u"profiles"_s, profiles);
    commitConfiguration();
}

void Dashboard::setWidgetSetting(const QString &profile, bool lcd5b, const QString &id,
                                 const QString &key, const QVariant &value)
{
    QJsonArray instances = widgetsFor(profile, lcd5b);
    bool changed = false;
    for (qsizetype i = 0; i < instances.size(); ++i) {
        QJsonObject instance = instances[i].toObject();
        if (instance.value(u"id"_s).toString() != id) {
            continue;
        }
        const auto *widget = m_registry.find(instance.value(u"type"_s).toString());
        if (!widget || !widget->defaultSettings().contains(key)) {
            return;
        }
        QVariant setting = value;
        if (key == u"collector"_s) {
            bool found = false;
            for (const auto *collector : m_state->collectors()) {
                found |= collector->displayName() == value.toString();
            }
            if (!found) {
                return;
            }
            setting = value.toString();
        } else if (key == u"fit"_s) {
            const QString fit = value.toString();
            if (fit != u"contain"_s && fit != u"cover"_s && fit != u"stretch"_s) {
                return;
            }
            setting = fit;
        } else if (key == u"text"_s) {
            setting = value.toString().left(80);
        }
        QJsonObject settings = instance.value(u"settings"_s).toObject();
        if (settings.value(key).toVariant() == setting) {
            return;
        }
        if (key == u"collector"_s && settings.value(u"style"_s).toString() == u"sparkline"_s) {
            const auto fields = m_state->fields();
            qsizetype fieldIndex = -1;
            for (qsizetype field = 0; field < fields.size(); ++field) {
                if (fields[field]->depth() > 0 && fields[field]->collector() &&
                    fields[field]->collector()->displayName() == setting.toString()) {
                    fieldIndex = field;
                    break;
                }
            }
            if (fieldIndex < 0) {
                settings.insert(u"style"_s, u"value"_s);
                settings.remove(u"fieldIndex"_s);
            } else {
                settings.insert(u"fieldIndex"_s, static_cast<int>(fieldIndex));
            }
        }
        settings.insert(key, QJsonValue::fromVariant(setting));
        instance.insert(u"settings"_s, settings);
        instances[i] = instance;
        changed = true;
        break;
    }
    if (!changed) {
        return;
    }
    QJsonObject profiles = m_configuration.value(u"profiles"_s).toObject();
    QJsonObject layout = profiles.value(profile).toObject();
    layout.insert(variantKey(lcd5b), instances);
    profiles.insert(profile, layout);
    m_configuration.insert(u"profiles"_s, profiles);
    commitConfiguration();
}

QJsonObject Dashboard::findRule(const QString &condition) const
{
    const QJsonArray rules = m_configuration.value(u"rules"_s).toArray();
    for (const auto &value : rules) {
        const QJsonObject item = value.toObject();
        if (item.value(u"condition"_s).toString() == condition) {
            return item;
        }
    }
    return {};
}

QVariantMap Dashboard::rule(const QString &condition) const
{
    return findRule(condition).toVariantMap();
}

void Dashboard::setRuleEnabled(const QString &condition, bool enabled)
{
    QJsonArray rules = m_configuration.value(u"rules"_s).toArray();
    for (qsizetype i = 0; i < rules.size(); ++i) {
        QJsonObject item = rules[i].toObject();
        if (item.value(u"condition"_s).toString() != condition ||
            item.value(u"enabled"_s).toBool() == enabled) {
            continue;
        }
        item.insert(u"enabled"_s, enabled);
        rules[i] = item;
        m_configuration.insert(u"rules"_s, rules);
        commitConfiguration();
        return;
    }
}

void Dashboard::setRulePriority(const QString &condition, int priority)
{
    priority = qBound(0, priority, 100);
    QJsonArray rules = m_configuration.value(u"rules"_s).toArray();
    for (qsizetype i = 0; i < rules.size(); ++i) {
        QJsonObject item = rules[i].toObject();
        if (item.value(u"condition"_s).toString() != condition ||
            item.value(u"priority"_s).toInt() == priority) {
            continue;
        }
        item.insert(u"priority"_s, priority);
        rules[i] = item;
        m_configuration.insert(u"rules"_s, rules);
        commitConfiguration();
        return;
    }
}

void Dashboard::setRuleProfile(const QString &condition, const QString &profile)
{
    if (profile != u"normal"_s && profile != u"inGame"_s && profile != u"idle"_s) {
        return;
    }
    QJsonArray rules = m_configuration.value(u"rules"_s).toArray();
    for (qsizetype i = 0; i < rules.size(); ++i) {
        QJsonObject item = rules[i].toObject();
        if (item.value(u"condition"_s).toString() != condition ||
            item.value(u"profile"_s).toString() == profile) {
            continue;
        }
        item.insert(u"profile"_s, profile);
        rules[i] = item;
        m_configuration.insert(u"rules"_s, rules);
        commitConfiguration();
        return;
    }
}

void Dashboard::setIdleTimeoutSeconds(int seconds)
{
    seconds = qBound(30, seconds, 86400);
    if (idleTimeoutSeconds() == seconds) {
        return;
    }
    m_configuration.insert(u"idleTimeoutSeconds"_s, seconds);
    commitConfiguration();
}

void Dashboard::setBacklightOffOnIdle(bool enabled)
{
    if (backlightOffOnIdle() == enabled) {
        return;
    }
    m_configuration.insert(u"backlightOffOnIdle"_s, enabled);
    commitConfiguration();
}

PanelFrame Dashboard::renderFrame(bool lcd5b)
{
    refreshExternalConfiguration();
    updateRules();
    return m_renderer.render(widgetsFor(m_activeProfile, lcd5b), lcd5b,
                             m_state->artworkEnabled());
}

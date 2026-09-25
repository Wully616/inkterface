#ifndef ARTWORK_HPP
#define ARTWORK_HPP

#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>

#include "steam.hpp"

// The e-paper panels use the original 648x480 monochrome frame. The LCD-5B
// gets a separate full-resolution JPEG frame so its artwork stays in color.
#define ART_FRAME_WIDTH 648
#define ART_FRAME_HEIGHT 480
#define LCD_ART_FRAME_WIDTH 1024
#define LCD_ART_FRAME_HEIGHT 600

class Artwork : public QObject
{
    Q_OBJECT

  public:
    explicit Artwork(steam::Steam *steam, QObject *parent = nullptr);

    // dither an image to 1bpp and pack it row-major, MSB-first, 1 = ink;
    // the format Adafruit_GFX::drawBitmap() consumes on the panel
    static QByteArray packMono(const QImage &image);

  public slots:
    // compose a "now playing" frame for the app and emit frameReady() once
    // any box art has been resolved (local cache, then CDN, then none)
    void composeForApp(const steam::App &app, double playtimeMinutes, QPair<int, int> achievements);

  signals:
    void frameReady(QByteArray monoBits, quint16 monoWidth, quint16 monoHeight,
                    QByteArray colorJpeg, quint16 colorWidth, quint16 colorHeight);

  private slots:
    void boxArtReply();

  private:
    steam::Steam *m_steam = nullptr;
    QNetworkAccessManager *m_netman = nullptr;

    // details for the frame currently being composed
    steam::App m_app;
    double m_playtime = -1;
    QPair<int, int> m_achievements = {-1, -1};

    // box art is cached per appid so recompositions (e.g. when achievement
    // data arrives) don't refetch
    QString m_artAppid;
    QImage m_artImage;

    QImage loadLocalBoxArt(const QString &appid);
    void compose();
    QByteArray composeColorJpeg() const;
};

#endif /* ARTWORK_HPP */

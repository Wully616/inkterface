#ifndef ARTWORK_HPP
#define ARTWORK_HPP

#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>

#include "steam.hpp"


class Artwork : public QObject
{
    Q_OBJECT

  public:
    explicit Artwork(steam::Steam *steam, QObject *parent = nullptr);

    QImage image() const { return m_artImage; }

  public slots:
    void requestForApp(const steam::App &app);
    void clear();

  signals:
    void imageChanged();

  private:
    steam::Steam *m_steam = nullptr;
    QNetworkAccessManager *m_netman = nullptr;
    QString m_artAppid;
    QImage m_artImage;

    QImage loadLocalBoxArt(const QString &appid);
};


#endif /* ARTWORK_HPP */

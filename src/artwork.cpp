#include "artwork.hpp"

#include <QDebug>
#include <QDir>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

using namespace Qt::Literals::StringLiterals;

Artwork::Artwork(steam::Steam *steam, QObject *parent)
    : QObject(parent)
    , m_steam(steam)
    , m_netman(new QNetworkAccessManager(this))
{
}
void Artwork::clear()
{
    if (m_artAppid.isEmpty() && m_artImage.isNull()) {
        return;
    }
    m_artAppid.clear();
    m_artImage = QImage();
    emit imageChanged();
}


void Artwork::requestForApp(const steam::App &app)
{
    if (m_artAppid == app.appid) {
        return;
    }

    m_artAppid = app.appid;
    m_artImage = app.appid.isEmpty() ? QImage() : loadLocalBoxArt(app.appid);
    emit imageChanged();
    if (!m_artImage.isNull() || app.appid.isEmpty()) {
        return;
    }

    auto reply = m_netman->get(QNetworkRequest(
        u"https://cdn.cloudflare.steamstatic.com/steam/apps/%1/library_600x900.jpg"_s.arg(
            app.appid)));
    QTimer::singleShot(10000, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, appid = app.appid] {
        reply->deleteLater();
        if (reply->isOpen() && reply->error() == QNetworkReply::NoError) {
            QImage image;
            image.loadFromData(reply->readAll());
            if (m_artAppid == appid && !image.isNull()) {
                m_artImage = image;
                emit imageChanged();
            }
        } else {
            qDebug() << "box art fetch failed:" << reply->errorString();
        }
    });
}

QImage Artwork::loadLocalBoxArt(const QString &appid)
{
    // steam has cached art under a couple of different layouts over the years
    const QString cache = m_steam->steamDir() + u"/appcache/librarycache"_s;
    const QStringList candidates = {
        u"%1/%2/library_600x900.jpg"_s.arg(cache, appid),
        u"%1/%2_library_600x900.jpg"_s.arg(cache, appid),
        u"%1/%2/header.jpg"_s.arg(cache, appid),
        u"%1/%2_header.jpg"_s.arg(cache, appid),
    };
    for (const auto &path : candidates) {
        if (QFile::exists(path)) {
            QImage img(path);
            if (!img.isNull()) {
                qDebug() << "using local box art:" << path;
                return img;
            }
        }
    }
    // newer steam builds hash-name the files inside the appid folder, take
    // the largest portrait-ish image we can find
    QDir d(u"%1/%2"_s.arg(cache, appid));
    QImage best;
    const auto entries = d.entryList({u"*.jpg"_s, u"*.png"_s}, QDir::Files);
    for (const auto &entry : entries) {
        QImage img(d.absoluteFilePath(entry));
        if (img.isNull() || img.width() > img.height()) {
            continue;
        }
        if (img.width() * img.height() > best.width() * best.height()) {
            best = img;
        }
    }
    if (!best.isNull()) {
        qDebug() << "using local box art from librarycache folder for" << appid;
    }
    return best;
}

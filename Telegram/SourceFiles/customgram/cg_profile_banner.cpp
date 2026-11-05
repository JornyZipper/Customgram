#include "customgram/cg_profile_banner.h"
#include "customgram/cg_profile_customization.h"

#include <QtCore/QEvent>
#include <QtCore/QFileInfo>
#include <QtGui/QImageReader>
#include <QtGui/QMovie>
#include <QtGui/QPainter>
#include <QtWidgets/QWidget>

namespace Customgram {

ProfileBannerLayer::ProfileBannerLayer(QWidget *host)
: QObject(host)
, _host(host)
, _poll(this) {
    host->installEventFilter(this);

    QObject::connect(&_poll, &QTimer::timeout,
        this, [this] { refresh(); });

    _poll.start(350);
    refresh();
}

bool ProfileBannerLayer::eventFilter(
        QObject *watched,
        QEvent *event) {
    if (watched == _host && event->type() == QEvent::Resize) {
        _host->update();
    }
    return QObject::eventFilter(watched, event);
}

void ProfileBannerLayer::refresh() {
    const auto rev = ProfileAppearanceRevision();
    if (rev == _revision) return;

    _revision = rev;

    const auto state = GetProfileAppearance();

    _bannerHeight = state.bannerHeight;
    _darkening = state.bannerDarkening;

    delete _movie;
    _movie = nullptr;
    _still = {};

    const bool enabled =
        state.bannerEnabled
        && QFileInfo::exists(state.bannerPath);

    if (!enabled) {
        if (_host) _host->update();
        return;
    }

    if (state.bannerPath.endsWith(
            QStringLiteral(".gif"),
            Qt::CaseInsensitive)) {

        _movie = new QMovie(state.bannerPath, QByteArray(), this);
        _movie->setScaledSize(QSize(800, 300));
        _movie->setCacheMode(QMovie::CacheNone);

        if (!_movie->isValid()) {
            delete _movie;
            _movie = nullptr;
        } else {
            QObject::connect(
                _movie,
                &QMovie::frameChanged,
                this,
                [this](int) {
                    if (_host) _host->update();
                });

            _movie->start();
        }
    } else {
        QImageReader reader(state.bannerPath);
        reader.setScaledSize(QSize(800, 300));
        _still = reader.read();
    }

    if (_host) _host->update();
}

void ProfileBannerLayer::paint(QPainter &p) const {
    const auto image = _movie
        ? _movie->currentImage()
        : _still;

    if (image.isNull() || !_host) return;

    const auto view = QRectF(
        0,
        0,
        _host->width(),
        _bannerHeight);

    if (view.isEmpty()) return;

    p.save();
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    const auto scale = qMax(
        view.width() / image.width(),
        view.height() / image.height());

    const auto crop = QRectF(
        (image.width() - view.width() / scale) / 2.,
        (image.height() - view.height() / scale) / 2.,
        view.width() / scale,
        view.height() / scale);

    p.drawImage(view, image, crop);

    if (_darkening > 0) {
        p.fillRect(
            view,
            QColor(0, 0, 0, (_darkening * 255) / 100));
    }

    p.restore();
}

} // namespace Customgram
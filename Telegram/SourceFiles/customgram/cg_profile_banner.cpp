#include "customgram/cg_profile_banner.h"
#include "customgram/cg_profile_customization.h"

#include <QtCore/QEvent>
#include <QtCore/QFileInfo>
#include <QtGui/QImageReader>
#include <QtGui/QMovie>
#include <QtGui/QPainter>

namespace Customgram {

ProfileBannerLayer::ProfileBannerLayer(QWidget *host)
: QWidget(host)
, _host(host)
, _poll(this) {
    setObjectName(QStringLiteral("cg135_profile_banner"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    host->installEventFilter(this);
    QObject::connect(&_poll, &QTimer::timeout, this, [this] { refresh(); });
    _poll.start(350);
    refresh();
    lower(); // under all existing TopBar child widgets (avatars, labels, buttons).
}

bool ProfileBannerLayer::eventFilter(QObject *watched, QEvent *event) {
    if (watched == _host && event->type() == QEvent::Resize) syncGeometry();
    return QWidget::eventFilter(watched, event);
}

void ProfileBannerLayer::syncGeometry() {
    if (_host) setGeometry(0, 0, _host->width(), _bannerHeight);
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
    const bool enabled = state.bannerEnabled && QFileInfo::exists(state.bannerPath);
    setVisible(enabled);
    if (!enabled) return;
    if (state.bannerPath.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) {
        _movie = new QMovie(state.bannerPath, QByteArray(), this);
        _movie->setScaledSize(QSize(800, 300));
        _movie->setCacheMode(QMovie::CacheNone);
        if (!_movie->isValid()) { setVisible(false); return; }
        QObject::connect(_movie, &QMovie::frameChanged, this, [this](int) { update(); });
        _movie->start();
    } else {
        QImageReader reader(state.bannerPath);
        reader.setScaledSize(QSize(800, 300));
        _still = reader.read();
        if (_still.isNull()) { setVisible(false); return; }
    }
    syncGeometry();
    lower();
    update();
}

void ProfileBannerLayer::paintEvent(QPaintEvent *) {
    const auto image = _movie ? _movie->currentImage() : _still;
    if (image.isNull()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto view = QRectF(rect());
    if (view.isEmpty()) return;
    // Cover crop, centered, independent of the image aspect ratio.
    const auto scale = qMax(view.width() / image.width(), view.height() / image.height());
    const auto crop = QRectF(
        (image.width() - view.width() / scale) / 2.,
        (image.height() - view.height() / scale) / 2.,
        view.width() / scale,
        view.height() / scale);
    p.drawImage(view, image, crop);
    if (_darkening > 0) p.fillRect(rect(), QColor(0, 0, 0, (_darkening * 255) / 100));
}

} // namespace Customgram

#include "customgram/cg_avatar_frame_renderer.h"
#include "customgram/cg_profile_customization.h"

#include <QtCore/QEvent>
#include <QtCore/QFileInfo>
#include <QtGui/QConicalGradient>
#include <QtGui/QImageReader>
#include <QtGui/QMovie>
#include <QtGui/QPainter>
#include <QtGui/QPen>

namespace Customgram {

AvatarFrameLayer::AvatarFrameLayer(QWidget *avatarButton)
: QWidget(avatarButton)
, _avatar(avatarButton)
, _poll(this) {
    setObjectName(QStringLiteral("cg135_avatar_frame"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_TranslucentBackground);
    avatarButton->installEventFilter(this);
    syncGeometry();
    _clock.start();
    QObject::connect(&_poll, &QTimer::timeout, this, [this] {
        refresh();
        const auto v = GetProfileAppearance();
        if (v.frameStyle == FrameStyle::Animated) update();
    });
    _poll.start(42);
    refresh();
    raise();
}

bool AvatarFrameLayer::eventFilter(QObject *watched, QEvent *event) {
    if (watched == _avatar && event->type() == QEvent::Resize) syncGeometry();
    return QWidget::eventFilter(watched, event);
}

void AvatarFrameLayer::syncGeometry() { if (_avatar) setGeometry(_avatar->rect()); }

void AvatarFrameLayer::refresh() {
    const auto revision = ProfileAppearanceRevision();
    if (revision == _revision) return;
    _revision = revision;
    const auto v = GetProfileAppearance();
    delete _movie;
    _movie = nullptr;
    _still = {};
    if (v.frameStyle == FrameStyle::None) { hide(); return; }
    if (v.frameStyle == FrameStyle::Custom) {
        if (!QFileInfo::exists(v.framePath)) { hide(); return; }
        if (v.framePath.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) {
            _movie = new QMovie(v.framePath, QByteArray(), this);
            _movie->setScaledSize(QSize(160, 160));
            _movie->setCacheMode(QMovie::CacheNone);
            _movie->setSpeed(v.frameSpeed);
            if (!_movie->isValid()) { hide(); return; }
            QObject::connect(_movie, &QMovie::frameChanged, this, [this](int) { update(); });
            _movie->start();
        } else {
            QImageReader reader(v.framePath);
            reader.setScaledSize(QSize(160, 160));
            _still = reader.read();
            if (_still.isNull()) { hide(); return; }
        }
    }
    show();
    raise();
    update();
}

void AvatarFrameLayer::paintEvent(QPaintEvent *) {
    const auto v = GetProfileAppearance();
    if (v.frameStyle == FrameStyle::None) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(v.frameOpacity / 100.);
    const auto margin = (100. - v.frameScale) * qMin(width(), height()) / 200.
        + v.frameOffset;
    const auto circle = QRectF(rect()).adjusted(margin, margin, -margin, -margin);
    if (circle.width() < 4 || circle.height() < 4) return;
    if (v.frameStyle == FrameStyle::Custom) {
        const auto image = _movie ? _movie->currentImage() : _still;
        if (!image.isNull()) p.drawImage(circle, image);
        return;
    }
    const auto thickness = static_cast<qreal>(v.frameThickness);
    auto stroke = circle.adjusted(thickness / 2., thickness / 2., -thickness / 2., -thickness / 2.);
    p.setBrush(Qt::NoBrush);
    QPen pen;
    pen.setWidthF(thickness);
    if (v.frameStyle == FrameStyle::Neon) {
        pen.setColor(QColor(0, 212, 255));
    } else if (v.frameStyle == FrameStyle::Gold) {
        pen.setColor(QColor(255, 191, 64));
    } else {
        const auto angle = (v.frameStyle == FrameStyle::Animated)
            ? (_clock.elapsed() * v.frameSpeed / 13000.) : 0.;
        QConicalGradient gradient(stroke.center(), angle);
        gradient.setColorAt(0., QColor(0, 209, 255));
        gradient.setColorAt(.25, QColor(140, 74, 255));
        gradient.setColorAt(.50, QColor(255, 72, 157));
        gradient.setColorAt(.75, QColor(0, 225, 195));
        gradient.setColorAt(1., QColor(0, 209, 255));
        pen.setBrush(gradient);
    }
    p.setPen(pen);
    p.drawEllipse(stroke);
}

} // namespace Customgram

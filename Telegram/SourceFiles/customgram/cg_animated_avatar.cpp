#include "customgram/cg_animated_avatar.h"
#include "customgram/cg_profile_customization.h"

#include <QtCore/QEvent>
#include <QtCore/QFileInfo>
#include <QtGui/QMovie>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

namespace Customgram {

AnimatedAvatarLayer::AnimatedAvatarLayer(QWidget *avatarButton)
: QWidget(avatarButton)
, _avatar(avatarButton)
, _poll(this) {
    setObjectName(QStringLiteral("cg135_animated_avatar"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    avatarButton->installEventFilter(this);
    syncGeometry();
    QObject::connect(&_poll, &QTimer::timeout, this, [this] { refresh(); });
    _poll.start(300);
    refresh();
    raise();
}

bool AnimatedAvatarLayer::eventFilter(QObject *watched, QEvent *event) {
    if (watched == _avatar && event->type() == QEvent::Resize) syncGeometry();
    return QWidget::eventFilter(watched, event);
}

void AnimatedAvatarLayer::syncGeometry() {
    if (_avatar) setGeometry(_avatar->rect());
}

void AnimatedAvatarLayer::refresh() {
    const auto rev = ProfileAppearanceRevision();
    if (rev == _revision) return;
    _revision = rev;
    const auto state = GetProfileAppearance();
    delete _movie;
    _movie = nullptr;
    const auto enabled = state.avatarEnabled && QFileInfo::exists(state.avatarPath);
    setVisible(enabled);
    if (!enabled) return;
    _movie = new QMovie(state.avatarPath, QByteArray(), this);
    _movie->setCacheMode(QMovie::CacheNone);
    _movie->setScaledSize(QSize(160, 160));
    if (!_movie->isValid()) {
        setVisible(false);
        return;
    }
    QObject::connect(_movie, &QMovie::frameChanged, this, [this](int) { update(); });
    _movie->start();
    update();
}

void AnimatedAvatarLayer::paintEvent(QPaintEvent *) {
    if (!_movie || !_movie->isValid()) return;
    const auto frame = _movie->currentImage();
    if (frame.isNull()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto bounds = QRectF(rect()).adjusted(1, 1, -1, -1);
    QPainterPath clip;
    clip.addEllipse(bounds);
    p.setClipPath(clip);
    p.drawImage(bounds, frame);
}

} // namespace Customgram

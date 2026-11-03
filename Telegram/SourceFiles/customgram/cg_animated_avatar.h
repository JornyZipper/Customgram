#pragma once

#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>
class QMovie;

namespace Customgram {

// Draws the local GIF over an existing Telegram avatar button.
// Mount ONLY on widgets representing the signed-in account, never other peers.
class AnimatedAvatarLayer final : public QWidget {
public:
    explicit AnimatedAvatarLayer(QWidget *avatarButton);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
private:
    void refresh();
    void syncGeometry();
    QPointer<QWidget> _avatar;
    QMovie *_movie = nullptr;
    QTimer _poll;
    quint64 _revision = 0;
};

} // namespace Customgram

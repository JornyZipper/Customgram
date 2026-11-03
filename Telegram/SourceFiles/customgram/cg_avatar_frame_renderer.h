#pragma once

#include <QtCore/QElapsedTimer>
#include <QtGui/QImage>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>
class QMovie;

namespace Customgram {

class AvatarFrameLayer final : public QWidget {
public:
    explicit AvatarFrameLayer(QWidget *avatarButton);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
private:
    void refresh();
    void syncGeometry();
    QPointer<QWidget> _avatar;
    QMovie *_movie = nullptr;
    QImage _still;
    QTimer _poll;
    QElapsedTimer _clock;
    quint64 _revision = 0;
};

} // namespace Customgram

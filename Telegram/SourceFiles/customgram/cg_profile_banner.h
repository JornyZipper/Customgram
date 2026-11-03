#pragma once

#include <QtGui/QImage>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtWidgets/QWidget>
class QMovie;

namespace Customgram {

// Transparent-to-input header image. Lowered beneath native Telegram controls.
// Attach to a profile TopBar only when displaying your own profile.
class ProfileBannerLayer final : public QWidget {
public:
    explicit ProfileBannerLayer(QWidget *profileHeader);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
private:
    void refresh();
    void syncGeometry();
    QPointer<QWidget> _host;
    QMovie *_movie = nullptr;
    QImage _still;
    QTimer _poll;
    quint64 _revision = 0;
    int _darkening = 0;
    int _bannerHeight = 135;
};

} // namespace Customgram

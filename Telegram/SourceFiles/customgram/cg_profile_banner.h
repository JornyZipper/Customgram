#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtGui/QImage>

class QPainter;
class QMovie;
class QWidget;

namespace Customgram {

    class ProfileBannerLayer final : public QObject {
    public:
        explicit ProfileBannerLayer(QWidget *host);

        void paint(QPainter &p) const;

    protected:
        bool eventFilter(QObject *watched, QEvent *event) override;

    private:
        void refresh();

        QPointer<QWidget> _host;
        QMovie *_movie = nullptr;
        QImage _still;
        QTimer _poll;
        quint64 _revision = 0;
        int _darkening = 0;
        int _bannerHeight = 135;
    };

} // namespace Customgram
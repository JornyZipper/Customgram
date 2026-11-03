#pragma once
// Customgram local-only avatar rings: built-in effects and custom PNG/GIF.
// Qt Widgets only, no Q_OBJECT and no Telegram server calls.

#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QObject>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtGui/QConicalGradient>
#include <QtGui/QImageReader>
#include <QtGui/QMovie>
#include <QtGui/QPainter>
#include <QtWidgets/QWidget>
#include <algorithm>

namespace Customgram {

// 0=off, 1=cyan, 2=gold, 3=gradient, 4=animated gradient, 5=custom PNG/GIF.
inline QString ProfileFrameSettingsDirectory() {
    const auto location = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);
    const auto directory = location.isEmpty()
        ? (QDir::homePath() + QStringLiteral("/Customgram"))
        : location;
    QDir().mkpath(directory);
    return directory;
}

inline QString ProfileFrameSettingsPath() {
    return ProfileFrameSettingsDirectory() + QStringLiteral("/customgram-frame.ini");
}

inline int &ProfileFrameStyleStorage() {
    static int value = [] {
        QSettings settings(ProfileFrameSettingsPath(), QSettings::IniFormat);
        return std::clamp(settings.value(QStringLiteral("Profile/frame"), 0).toInt(), 0, 5);
    }();
    return value;
}

inline QString &ProfileFrameFileStorage() {
    static QString value = [] {
        QSettings settings(ProfileFrameSettingsPath(), QSettings::IniFormat);
        return settings.value(QStringLiteral("Profile/frame_file")).toString();
    }();
    return value;
}

inline unsigned &ProfileFrameRevisionStorage() {
    static unsigned value = 0;
    return value;
}

inline int ProfileFrameStyle() {
    return ProfileFrameStyleStorage();
}

inline QString ProfileFrameFilePath() {
    return ProfileFrameFileStorage();
}

inline bool HasCustomProfileFrame() {
    return QFileInfo::exists(ProfileFrameFilePath());
}

inline void SetProfileFrameStyle(int value) {
    value = std::clamp(value, 0, 5);
    ProfileFrameStyleStorage() = value;
    ++ProfileFrameRevisionStorage();
    QSettings settings(ProfileFrameSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Profile/frame"), value);
    settings.sync();
}

// Imports by COPYING the selected image into app-local settings. Does not
// overwrite/alter the original file; does not upload it anywhere.
inline bool ImportCustomProfileFrame(const QString &source, QString *error) {
    const QFileInfo info(source);
    const auto extension = info.suffix().toLower();
    const bool isPng = (extension == QStringLiteral("png"));
    const bool isGif = (extension == QStringLiteral("gif"));
    const qint64 maxBytes = isGif ? 15LL * 1024 * 1024 : 12LL * 1024 * 1024;
    const auto fail = [&](const QString &reason) {
        if (error) *error = reason;
        return false;
    };
    if ((!isPng && !isGif) || !info.isFile() || !info.isReadable()) {
        return fail(QStringLiteral("Select a readable PNG or GIF file."));
    }
    if (info.size() <= 0 || info.size() > maxBytes) {
        return fail(QStringLiteral("File is too large (PNG 12 MB, GIF 15 MB maximum)."));
    }
    QImageReader reader(source);
    const auto size = reader.size();
    if (!size.isValid() || size.width() != size.height()
        || size.width() < 64 || size.width() > 2048) {
        return fail(QStringLiteral("Frame must be square (64 to 2048 px per side)."));
    }
    const auto detected = reader.format().toLower();
    if ((isPng && detected != QByteArray("png"))
        || (isGif && detected != QByteArray("gif"))) {
        return fail(QStringLiteral("File contents do not match its PNG/GIF extension."));
    }
    const auto preview = reader.read();
    if (preview.isNull() || preview.width() != size.width()
        || preview.height() != size.height()) {
        return fail(QStringLiteral("Failed to decode frame image."));
    }
    const auto middle = preview.pixelColor(preview.width() / 2, preview.height() / 2);
    if (middle.alpha() > 40) {
        return fail(QStringLiteral("The image center must be transparent, so it does not cover your avatar."));
    }
    if (isGif) {
        QMovie movie(source);
        if (!movie.isValid()) {
            return fail(QStringLiteral("Qt cannot play this GIF on this computer."));
        }
    }
    const auto destination = ProfileFrameSettingsDirectory()
        + QStringLiteral("/frame-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces)
        + (isGif ? QStringLiteral(".gif") : QStringLiteral(".png"));
    if (!QFile::copy(source, destination)) {
        return fail(QStringLiteral("Cannot save the frame into app-local data."));
    }
    ProfileFrameFileStorage() = destination;
    ProfileFrameStyleStorage() = 5;
    ++ProfileFrameRevisionStorage();
    QSettings settings(ProfileFrameSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("Profile/frame_file"), destination);
    settings.setValue(QStringLiteral("Profile/frame"), 5);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        return fail(QStringLiteral("Cannot write frame settings."));
    }
    return true;
}

class ProfileFrameOverlay final : public QWidget {
public:
    explicit ProfileFrameOverlay(QWidget *avatar)
    : QWidget(avatar)
    , _timer(this) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setGeometry(avatar->rect());
        avatar->installEventFilter(this);
        _elapsed.start();
        QObject::connect(&_timer, &QTimer::timeout, this, [this] {
            const auto style = ProfileFrameStyle();
            const auto revision = ProfileFrameRevisionStorage();
            if (style != _lastStyle || revision != _lastRevision) {
                _lastStyle = style;
                _lastRevision = revision;
                LoadCustomImageIfNeeded();
                _timer.setInterval(style == 4 ? 42 : 200);
                update();
            } else if (style == 4) {
                update();
            }
        });
        _timer.start(200);
        show();
        raise();
        // Render the saved style immediately rather than waiting for first timer tick.
        _lastStyle = ProfileFrameStyle();
        _lastRevision = ProfileFrameRevisionStorage();
        LoadCustomImageIfNeeded();
        _timer.setInterval(_lastStyle == 4 ? 42 : 200);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched == parentWidget()
            && (event->type() == QEvent::Resize
                || event->type() == QEvent::Show)) {
            setGeometry(parentWidget()->rect());
            raise();
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override {
        const auto selected = ProfileFrameStyle();
        if (!selected || width() < 14 || height() < 14) return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        if (selected == 5) {
            const auto image = _movie ? _movie->currentImage() : _staticFrame;
            if (!image.isNull()) painter.drawImage(QRectF(rect()), image);
            return;
        }

        const auto ellipse = QRectF(rect()).adjusted(2.2, 2.2, -2.2, -2.2);
        painter.setBrush(Qt::NoBrush);
        if (selected == 1) {
            painter.setPen(QPen(QColor(31, 204, 255), 3.0));
        } else if (selected == 2) {
            painter.setPen(QPen(QColor(246, 191, 66), 3.0));
        } else {
            QConicalGradient gradient(
                ellipse.center(),
                selected == 4 ? _elapsed.elapsed() * 0.12 : 0.0);
            gradient.setColorAt(0.00, QColor(40, 217, 255));
            gradient.setColorAt(0.25, QColor(180, 78, 255));
            gradient.setColorAt(0.50, QColor(255, 93, 200));
            gradient.setColorAt(0.75, QColor(128, 95, 255));
            gradient.setColorAt(1.00, QColor(40, 217, 255));
            painter.setPen(QPen(QBrush(gradient), 3.4));
        }
        painter.drawEllipse(ellipse);
    }

private:
    void LoadCustomImageIfNeeded() {
        if (_movie) {
            _movie->stop();
            _movie->deleteLater();
            _movie = nullptr;
        }
        _staticFrame = QImage();
        if (ProfileFrameStyle() != 5) return;
        const auto path = ProfileFrameFilePath();
        if (!QFileInfo::exists(path)) return;
        if (path.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) {
            auto movie = new QMovie(path, QByteArray(), this);
            if (movie->isValid()) {
                movie->setCacheMode(QMovie::CacheNone);
                QObject::connect(movie, &QMovie::frameChanged,
                    this, [this] (int) { update(); });
                _movie = movie;
                movie->start();
            } else {
                movie->deleteLater();
            }
        } else {
            QImageReader reader(path);
            _staticFrame = reader.read();
        }
    }

    QTimer _timer;
    QElapsedTimer _elapsed;
    int _lastStyle = -1;
    unsigned _lastRevision = 0;
    QImage _staticFrame;
    QMovie *_movie = nullptr;
};

} // namespace Customgram

#include "customgram/cg_profile_custom_box.h"
#include "customgram/cg_profile_customization.h"

#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "styles/style_layers.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QPointer>
#include <QtCore/QPropertyAnimation>
#include <QtCore/QEasingCurve>
#include <QtCore/QSignalBlocker>
#include <QtCore/QTimer>
#include <QtGui/QConicalGradient>
#include <QtGui/QImageReader>
#include <QtGui/QMovie>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QGraphicsOpacityEffect>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSlider>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QSizePolicy>

#include <algorithm>

namespace Customgram {
namespace {

// Widget is owned by a Telegram Ui::GenericBox row. No floating Qt window.
// Keep the panel compact; the long Frames page scrolls inside the box.
constexpr auto kPreviewHeight = 214;
constexpr auto kMaximumPageHeight = 266;

const char kUiStyle[] = R"CGSTYLE(
QWidget#cgProfileRoot { background: transparent; }
QFrame#cgPreviewCard, QFrame#cgPageCard { background: #182633; border: 1px solid #2c4053; border-radius: 12px; }
QLabel { color: #eaf1f7; background: transparent; border: none; }
QLabel#cgMuted { color: #91a8bc; font-size: 11px; }
QLabel#cgHeading { font-size: 14px; font-weight: 600; }
QLabel#cgValue { color: #75c9ff; font-weight: 600; }
QPushButton { background: #273b4e; border: 1px solid #365168; border-radius: 8px;
              padding: 7px 10px; color: #eaf1f7; font-size: 12px; }
QPushButton:hover { background: #34546d; }
QPushButton:pressed { background: #193b56; }
QPushButton#cgTab { background: transparent; border: 0px; border-bottom: 2px solid transparent;
                    border-radius: 0px; padding: 9px 3px; font-weight: 600; }
QPushButton#cgTab:checked { color: #62c5ff; border-bottom: 2px solid #34aaf5; }
QPushButton#cgImport { background: #1676ab; border: 0px; }
QPushButton#cgImport:hover { background: #218bc3; }
QPushButton#cgReset { background: transparent; color: #e7b2a6; border: 1px solid #765254; }
QPushButton#cgReset:hover { background: #49383e; }
QPushButton#cgConfirm { background: #934747; border: 0px; }
QPushButton#cgConfirm:hover { background: #af5454; }
QFrame#cgConfirmPanel { background: #28333f; border: 1px solid #674a4a; border-radius: 9px; }
QScrollArea#cgPageScroll { border: none; background: transparent; }
QScrollArea#cgPageScroll > QWidget > QWidget { background: transparent; }
QScrollBar:vertical { background: #172633; width: 7px; margin: 1px; }
QScrollBar::handle:vertical { background: #486379; min-height: 22px; border-radius: 3px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QCheckBox { color: #eaf1f7; background: transparent; spacing: 9px; }
QCheckBox::indicator { width: 17px; height: 17px; }
QComboBox { background: #273b4e; color: #eaf1f7; border: 1px solid #39556b;
            border-radius: 6px; min-height: 27px; padding: 3px 8px; }
QComboBox QAbstractItemView { background: #213344; color: #eaf1f7;
                              selection-background-color: #246c9f; }
QSlider::groove:horizontal { height: 5px; border-radius: 3px; background: #34485c; }
QSlider::sub-page:horizontal { border-radius: 3px; background: #30aaff; }
QSlider::handle:horizontal { background: #e6f6ff; width: 13px; margin: -5px 0;
                             border-radius: 7px; }
)CGSTYLE";

class AppearancePreview final : public QWidget {
public:
    explicit AppearancePreview(QWidget *parent) : QWidget(parent) {
        setFixedHeight(kPreviewHeight);
        _rotation.start();
        _timer = new QTimer(this);
        _timer->setInterval(70);
        QObject::connect(_timer, &QTimer::timeout, this, [this] {
            const auto state = GetProfileAppearance();
            if (state.frameStyle == FrameStyle::Animated) update();
        });
        _timer->start();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        syncAssets();
        const auto state = GetProfileAppearance();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        // The preview follows the geometry of a compact Telegram profile header:
        // banner, overlapping userpic, display name and secondary status.
        // The labels are intentionally generic; this editor does not access
        // Telegram account data or upload media.
        const auto padding = 5.;
        const auto outer = QRectF(padding, 2., width() - 2. * padding, height() - 5.);
        if (outer.width() <= 150.) return;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(18, 31, 44));
        p.drawRoundedRect(outer, 12., 12.);

        const auto bannerHeight = std::clamp(state.bannerHeight * .58, 84., 126.);
        const auto banner = QRectF(outer.left() + 2., outer.top() + 2.,
            outer.width() - 4., bannerHeight);
        p.save();
        QPainterPath bannerClip;
        bannerClip.addRoundedRect(banner, 10., 10.);
        p.setClipPath(bannerClip);
        const auto bannerImage = movieImage(_bannerMovie, _bannerStill);
        if (state.bannerEnabled && !bannerImage.isNull()) {
            const auto source = QRectF(bannerImage.rect());
            const auto ratio = std::max(banner.width() / source.width(), banner.height() / source.height());
            const auto target = QRectF(
                banner.center().x() - source.width() * ratio / 2.,
                banner.center().y() - source.height() * ratio / 2.,
                source.width() * ratio, source.height() * ratio);
            p.drawImage(target, bannerImage);
            p.fillRect(banner, QColor(0, 0, 0,
                std::clamp(state.bannerDarkening, 0, 100) * 255 / 100));
        } else {
            QLinearGradient background(banner.topLeft(), banner.bottomRight());
            background.setColorAt(0., QColor(15, 91, 142));
            background.setColorAt(1., QColor(70, 47, 123));
            p.fillRect(banner, background);
            p.setPen(QColor(255, 255, 255, 22));
            for (auto i = 0; i < 4; ++i) {
                p.drawEllipse(QPointF(banner.right() - 8. - i * 26.,
                    banner.top() + 22. + i * 12.), 21. + i * 7., 21. + i * 7.);
            }
        }
        p.restore();

        const auto avatarSize = 106.;
        const auto circle = QRectF(outer.left() + 20.,
            banner.bottom() - 52., avatarSize, avatarSize);
        p.setPen(QPen(QColor(18, 31, 44), 5.));
        p.setBrush(QColor(18, 31, 44));
        p.drawEllipse(circle.adjusted(-3., -3., 3., 3.));
        p.save();
        QPainterPath circleClip;
        circleClip.addEllipse(circle);
        p.setClipPath(circleClip);
        const auto avatarImage = movieImage(_avatarMovie, _avatarStill);
        if (state.avatarEnabled && !avatarImage.isNull()) {
            p.drawImage(circle, avatarImage);
        } else {
            QLinearGradient avatarGradient(circle.topLeft(), circle.bottomRight());
            avatarGradient.setColorAt(0., QColor(61, 170, 245));
            avatarGradient.setColorAt(1., QColor(68, 96, 211));
            p.fillRect(circle, avatarGradient);
            p.setPen(QColor(255, 255, 255, 235));
            auto f = font();
            f.setBold(true);
            f.setPixelSize(34);
            p.setFont(f);
            p.drawText(circle, Qt::AlignCenter, QStringLiteral("C"));
        }
        p.restore();
        drawFrame(p, state, circle);

        // Profile-style name and status next to the avatar.
        auto nameFont = font();
        nameFont.setBold(true);
        nameFont.setPixelSize(16);
        p.setFont(nameFont);
        p.setPen(QColor(236, 246, 253));
        const auto labelLeft = circle.right() + 12.;
        const auto labelWidth = std::max(0., outer.right() - labelLeft - 6.);
        p.drawText(QRectF(labelLeft, banner.bottom() + 12., labelWidth, 28.),
            Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Мой профиль"));
        auto subFont = font();
        subFont.setPixelSize(11);
        p.setFont(subFont);
        p.setPen(QColor(109, 202, 255));
        p.drawText(QRectF(labelLeft, banner.bottom() + 39., labelWidth, 20.),
            Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("Локальный просмотр"));
        p.setPen(QColor(145, 171, 189));
        p.drawText(QRectF(outer.left() + 12., outer.bottom() - 22., outer.width() - 24., 18.),
            Qt::AlignCenter, QStringLiteral("Видишь только ты · Telegram не изменяется"));
    }

private:
    static QImage still(const QString &path) {
        if (path.isEmpty()) return {};
        QImageReader reader(path);
        // Avoid keeping huge images in the live preview.
        const auto sz = reader.size();
        if (sz.isValid() && (sz.width() > 800 || sz.height() > 800)) {
            reader.setScaledSize(sz.scaled(800, 800, Qt::KeepAspectRatio));
        }
        return reader.read();
    }
    static QImage movieImage(QMovie *movie, const QImage &fallback) {
        return movie ? movie->currentImage() : fallback;
    }
    void load(const QString &path, QString &previous, QMovie *&movie, QImage &fallback) {
        if (path == previous) return;
        previous = path;
        delete movie;
        movie = nullptr;
        fallback = {};
        if (path.isEmpty() || !QFileInfo::exists(path)) return;
        if (path.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)) {
            movie = new QMovie(path, QByteArray(), this);
            movie->setCacheMode(QMovie::CacheNone);
            movie->setScaledSize(QSize(260, 260));
            if (movie->isValid()) {
                QObject::connect(movie, &QMovie::frameChanged, this, [this](int) { update(); });
                movie->start();
            } else {
                delete movie;
                movie = nullptr;
            }
        } else {
            fallback = still(path);
        }
    }
    void syncAssets() {
        const auto value = GetProfileAppearance();
        load(value.avatarPath, _avatarPath, _avatarMovie, _avatarStill);
        load(value.bannerPath, _bannerPath, _bannerMovie, _bannerStill);
        load(value.framePath, _framePath, _frameMovie, _frameStill);
    }
    void drawFrame(QPainter &p, const ProfileAppearance &state, QRectF circle) {
        if (state.frameStyle == FrameStyle::None) return;
        const auto margin = (100. - state.frameScale) * circle.width() / 200.
            + state.frameOffset;
        circle.adjust(margin, margin, -margin, -margin);
        if (circle.width() < 4.) return;
        p.save();
        p.setOpacity(state.frameOpacity / 100.);
        if (state.frameStyle == FrameStyle::Custom) {
            const auto image = movieImage(_frameMovie, _frameStill);
            if (!image.isNull()) p.drawImage(circle, image);
            p.restore();
            return;
        }
        QPen pen;
        pen.setWidthF(state.frameThickness);
        if (state.frameStyle == FrameStyle::Neon) {
            pen.setColor(QColor(35, 213, 255));
        } else if (state.frameStyle == FrameStyle::Gold) {
            pen.setColor(QColor(255, 198, 74));
        } else {
            const auto angle = (state.frameStyle == FrameStyle::Animated)
                ? _rotation.elapsed() * state.frameSpeed / 13000. : 0.;
            QConicalGradient g(circle.center(), angle);
            g.setColorAt(0., QColor(33, 210, 255));
            g.setColorAt(.25, QColor(158, 76, 252));
            g.setColorAt(.5, QColor(255, 75, 151));
            g.setColorAt(.75, QColor(0, 227, 195));
            g.setColorAt(1., QColor(33, 210, 255));
            pen.setBrush(g);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(pen);
        p.drawEllipse(circle.adjusted(state.frameThickness / 2., state.frameThickness / 2.,
            -state.frameThickness / 2., -state.frameThickness / 2.));
        p.restore();
    }

    QTimer *_timer = nullptr;
    QElapsedTimer _rotation;
    QString _avatarPath, _bannerPath, _framePath;
    QMovie *_avatarMovie = nullptr, *_bannerMovie = nullptr, *_frameMovie = nullptr;
    QImage _avatarStill, _bannerStill, _frameStill;
};

QLabel *MakeLabel(const QString &text, QWidget *parent, const char *name = nullptr) {
    auto *label = new QLabel(text, parent);
    if (name) label->setObjectName(QString::fromLatin1(name));
    label->setWordWrap(true);
    return label;
}

void SaveBoolean(bool enabled, void (*update)(ProfileAppearance &, bool)) {
    auto state = GetProfileAppearance();
    update(state, enabled);
    SetProfileAppearance(state);
}

struct SliderControl {
    QWidget *widget = nullptr;
    QSlider *slider = nullptr;
    QLabel *number = nullptr;
    void reset(int value) const {
        const QSignalBlocker blocker(slider);
        slider->setValue(value);
        number->setText(QString::number(value));
    }
};

SliderControl MakeSlider(const QString &title, int value, int min, int max,
    QWidget *parent, AppearancePreview *preview,
    void (*update)(ProfileAppearance &, int)) {
    auto *holder = new QWidget(parent);
    auto *row = new QHBoxLayout(holder);
    row->setContentsMargins(0, 2, 0, 2);
    row->setSpacing(8);
    auto *name = MakeLabel(title, holder);
    name->setMinimumWidth(104);
    auto *slider = new QSlider(Qt::Horizontal, holder);
    slider->setRange(min, max);
    slider->setValue(value);
    auto *number = MakeLabel(QString::number(value), holder, "cgValue");
    number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    number->setMinimumWidth(28);
    row->addWidget(name);
    row->addWidget(slider, 1);
    row->addWidget(number);
    QObject::connect(slider, &QSlider::valueChanged, holder,
        [number, preview, update](int v) {
            number->setText(QString::number(v));
            auto appearance = GetProfileAppearance();
            update(appearance, v);
            SetProfileAppearance(appearance);
            preview->update();
        });
    return { holder, slider, number };
}

QString UserFacingImportError(const QString &error) {
    if (error == QStringLiteral("Avatar and frame must be square."))
        return QStringLiteral("Аватарка и рамка должны быть квадратными (например, 512×512).");
    if (error == QStringLiteral("Banner must be landscape (width >= height)."))
        return QStringLiteral("Баннер должен быть горизонтальным: ширина не меньше высоты.");
    if (error == QStringLiteral("Avatar must be GIF."))
        return QStringLiteral("Для анимированной аватарки выбери GIF.");
    if (error == QStringLiteral("File cannot be read."))
        return QStringLiteral("Не удалось прочитать выбранный файл.");
    if (error == QStringLiteral("Dimensions must be between 64 and 2048 pixels."))
        return QStringLiteral("Размер изображения должен быть от 64 до 2048 пикселей.");
    if (error == QStringLiteral("Image is too large (GIF 8 MB / images 12 MB)."))
        return QStringLiteral("Файл слишком большой: GIF до 8 МБ, PNG/JPG до 12 МБ.");
    if (error == QStringLiteral("The middle of a frame must be transparent."))
        return QStringLiteral("У рамки центр должен быть прозрачным.");
    return error;
}

QPushButton *MakeImport(AssetKind kind, const QString &text,
    QWidget *parent, QLabel *status, AppearancePreview *preview,
    QComboBox *frameStyle = nullptr, QCheckBox *check = nullptr) {
    auto *button = new QPushButton(text, parent);
    button->setObjectName(QStringLiteral("cgImport"));
    QObject::connect(button, &QPushButton::clicked, parent,
        [kind, parent, status, preview, frameStyle, check] {
            const auto filter = (kind == AssetKind::Avatar)
                ? QStringLiteral("GIF (*.gif)")
                : (kind == AssetKind::Frame)
                ? QStringLiteral("PNG/GIF (*.png *.gif)")
                : QStringLiteral("PNG/JPG/GIF (*.png *.jpg *.jpeg *.gif)");
            const auto path = QFileDialog::getOpenFileName(parent,
                QStringLiteral("Выбрать файл Customgram"), QString(), filter);
            if (path.isEmpty()) return;
            QString error;
            if (!ImportProfileAsset(kind, path, &error)) {
                QMessageBox::warning(parent, QStringLiteral("Customgram"), UserFacingImportError(error));
                return;
            }
            status->setText(QFileInfo(path).fileName());
            if (frameStyle && kind == AssetKind::Frame) {
                frameStyle->setCurrentIndex(static_cast<int>(FrameStyle::Custom));
            }
            if (check && kind != AssetKind::Frame) {
                check->setChecked(true);
            }
            preview->update();
        });
    return button;
}

QWidget *MakeCard(QWidget *parent, QVBoxLayout *&layout) {
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("cgPageCard"));
    layout = new QVBoxLayout(card);
    layout->setContentsMargins(15, 12, 15, 12);
    layout->setSpacing(9);
    return card;
}

} // namespace

void SetupProfileCustomizationBox(Ui::GenericBox *box) {
    if (!box) return;
    box->setTitle(rpl::single(QStringLiteral("Customgram · Профиль")));
    const auto state = GetProfileAppearance();

    // Telegram owns the row and paints the surrounding box. This remains inside
    // its existing layered UI even though the controls are standard Qt widgets.
    auto *stack = box->addRow(object_ptr<Ui::VerticalLayout>(box), st::boxPadding);
    auto *host = stack->add(object_ptr<Ui::RpWidget>(stack));
    host->setObjectName(QStringLiteral("cgProfileRoot"));
    host->setFixedHeight(520); // Temporary layout height until first size-hint pass.
    // Height will be calculated from the active tab after all controls exist.
    host->setStyleSheet(QString::fromUtf8(kUiStyle));

    auto *root = new QVBoxLayout(host);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(7);

    auto *previewCard = new QFrame(host);
    previewCard->setObjectName(QStringLiteral("cgPreviewCard"));
    auto *previewLayout = new QVBoxLayout(previewCard);
    previewLayout->setContentsMargins(8, 6, 8, 6);
    auto *preview = new AppearancePreview(previewCard);
    previewLayout->addWidget(preview);
    root->addWidget(previewCard);

    auto *tabsRow = new QWidget(host);
    tabsRow->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *tabsLayout = new QHBoxLayout(tabsRow);
    tabsLayout->setContentsMargins(0, 0, 0, 0);
    tabsLayout->setSpacing(4);
    root->addWidget(tabsRow);
    auto *pages = new QStackedWidget(host);
    auto *fadeEffect = new QGraphicsOpacityEffect(pages);
    fadeEffect->setOpacity(1.);
    pages->setGraphicsEffect(fadeEffect);
    auto *fadeAnimation = new QPropertyAnimation(fadeEffect, "opacity", pages);
    fadeAnimation->setDuration(185);
    fadeAnimation->setEasingCurve(QEasingCurve::OutCubic);
    // Page heights differ: avatar needs less room than the five frame sliders.
    // The scroll view on the Frames tab prevents the box from growing indefinitely.
    auto resizePanel = [host, root, pages](int pageHeight) {
        pages->setFixedHeight(pageHeight);
        root->invalidate();
        root->activate();
        host->setFixedHeight(root->sizeHint().height());
        host->updateGeometry();
    };
    // Set after creating the pages, so the callback can use their size hints.

    // Animated avatar page.
    QVBoxLayout *avatarLayout;
    auto *avatarCard = MakeCard(pages, avatarLayout);
    avatarLayout->addWidget(MakeLabel(QStringLiteral("Локальная GIF-аватарка"), avatarCard, "cgHeading"));
    auto *avatarCheck = new QCheckBox(QStringLiteral("Включить GIF-аватарку"), avatarCard);
    avatarCheck->setChecked(state.avatarEnabled);
    avatarLayout->addWidget(avatarCheck);
    auto *avatarName = MakeLabel(QFileInfo(state.avatarPath).fileName(), avatarCard, "cgMuted");
    avatarLayout->addWidget(MakeImport(AssetKind::Avatar, QStringLiteral("Выбрать GIF"),
        avatarCard, avatarName, preview, nullptr, avatarCheck));
    avatarLayout->addWidget(avatarName);
    avatarLayout->addWidget(MakeLabel(
        QStringLiteral("GIF до 8 МБ, квадратный, 64–2048 пикселей. Изменения видны только тебе."),
        avatarCard, "cgMuted"));
    QObject::connect(avatarCheck, &QCheckBox::toggled, host, [preview](bool v) {
        SaveBoolean(v, [](ProfileAppearance &a, bool enabled) { a.avatarEnabled = enabled; });
        preview->update();
    });
    auto *avatarScroll = new QScrollArea(pages);
    avatarScroll->setObjectName(QStringLiteral("cgPageScroll"));
    avatarScroll->setFrameShape(QFrame::NoFrame);
    avatarScroll->setWidgetResizable(true);
    avatarScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    avatarScroll->setWidget(avatarCard);
    pages->addWidget(avatarScroll);

    // Banner page.
    QVBoxLayout *bannerLayout;
    auto *bannerCard = MakeCard(pages, bannerLayout);
    bannerLayout->addWidget(MakeLabel(QStringLiteral("Локальный баннер"), bannerCard, "cgHeading"));
    auto *bannerCheck = new QCheckBox(QStringLiteral("Показывать баннер"), bannerCard);
    bannerCheck->setChecked(state.bannerEnabled);
    bannerLayout->addWidget(bannerCheck);
    auto *bannerName = MakeLabel(QFileInfo(state.bannerPath).fileName(), bannerCard, "cgMuted");
    bannerLayout->addWidget(MakeImport(AssetKind::Banner, QStringLiteral("Выбрать PNG / JPG / GIF"),
        bannerCard, bannerName, preview, nullptr, bannerCheck));
    bannerLayout->addWidget(bannerName);
    const auto bannerHeight = MakeSlider(QStringLiteral("Высота"), state.bannerHeight, 70, 240,
        bannerCard, preview, [](ProfileAppearance &a, int v) { a.bannerHeight = v; });
    bannerLayout->addWidget(bannerHeight.widget);
    const auto bannerDarkening = MakeSlider(QStringLiteral("Затемнение, %"), state.bannerDarkening, 0, 75,
        bannerCard, preview, [](ProfileAppearance &a, int v) { a.bannerDarkening = v; });
    bannerLayout->addWidget(bannerDarkening.widget);
    bannerLayout->addWidget(MakeLabel(
        QStringLiteral("Горизонтальное изображение, PNG/JPG до 12 МБ или GIF до 8 МБ."),
        bannerCard, "cgMuted"));
    QObject::connect(bannerCheck, &QCheckBox::toggled, host, [preview](bool v) {
        SaveBoolean(v, [](ProfileAppearance &a, bool enabled) { a.bannerEnabled = enabled; });
        preview->update();
    });
    auto *bannerScroll = new QScrollArea(pages);
    bannerScroll->setObjectName(QStringLiteral("cgPageScroll"));
    bannerScroll->setFrameShape(QFrame::NoFrame);
    bannerScroll->setWidgetResizable(true);
    bannerScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    bannerScroll->setWidget(bannerCard);
    pages->addWidget(bannerScroll);

    // Frame page.
    QVBoxLayout *frameLayout;
    auto *frameCard = MakeCard(pages, frameLayout);
    frameLayout->addWidget(MakeLabel(QStringLiteral("Редактор рамки"), frameCard, "cgHeading"));
    auto *frameStyle = new QComboBox(frameCard);
    frameStyle->addItems({ QStringLiteral("Выключено"), QStringLiteral("Неон"),
        QStringLiteral("Золото"), QStringLiteral("Градиент"),
        QStringLiteral("Анимированный градиент"), QStringLiteral("Своя PNG/GIF") });
    frameStyle->setCurrentIndex(static_cast<int>(state.frameStyle));
    frameLayout->addWidget(frameStyle);
    auto *frameName = MakeLabel(QFileInfo(state.framePath).fileName(), frameCard, "cgMuted");
    frameLayout->addWidget(MakeImport(AssetKind::Frame,
        QStringLiteral("Загрузить свою рамку"), frameCard, frameName, preview, frameStyle));
    frameLayout->addWidget(frameName);
    const auto thickness = MakeSlider(QStringLiteral("Толщина"), state.frameThickness, 1, 16,
        frameCard, preview, [](ProfileAppearance &a, int v) { a.frameThickness = v; });
    frameLayout->addWidget(thickness.widget);
    const auto scale = MakeSlider(QStringLiteral("Масштаб, %"), state.frameScale, 85, 140,
        frameCard, preview, [](ProfileAppearance &a, int v) { a.frameScale = v; });
    frameLayout->addWidget(scale.widget);
    const auto opacity = MakeSlider(QStringLiteral("Прозрачность"), state.frameOpacity, 10, 100,
        frameCard, preview, [](ProfileAppearance &a, int v) { a.frameOpacity = v; });
    frameLayout->addWidget(opacity.widget);
    const auto speed = MakeSlider(QStringLiteral("Скорость, %"), state.frameSpeed, 25, 300,
        frameCard, preview, [](ProfileAppearance &a, int v) { a.frameSpeed = v; });
    frameLayout->addWidget(speed.widget);
    const auto offset = MakeSlider(QStringLiteral("Отступ"), state.frameOffset, -10, 20,
        frameCard, preview, [](ProfileAppearance &a, int v) { a.frameOffset = v; });
    frameLayout->addWidget(offset.widget);
    QObject::connect(frameStyle, qOverload<int>(&QComboBox::currentIndexChanged),
        host, [preview](int selected) {
            auto a = GetProfileAppearance();
            a.frameStyle = static_cast<FrameStyle>(selected);
            SetProfileAppearance(a);
            preview->update();
        });
    auto *frameScroll = new QScrollArea(pages);
    frameScroll->setObjectName(QStringLiteral("cgPageScroll"));
    frameScroll->setFrameShape(QFrame::NoFrame);
    frameScroll->setWidgetResizable(true);
    frameScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    frameScroll->setWidget(frameCard);
    pages->addWidget(frameScroll);

    root->addWidget(pages);

    auto *actions = new QWidget(host);
    auto *actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    actionsLayout->setSpacing(8);
    auto *hint = MakeLabel(QStringLiteral("Оформление хранится только на этом устройстве."), actions, "cgMuted");
    auto *reset = new QPushButton(QStringLiteral("Сбросить"), actions);
    reset->setObjectName(QStringLiteral("cgReset"));
    actionsLayout->addWidget(hint, 1);
    actionsLayout->addWidget(reset);
    root->addWidget(actions);

    // Reset confirmation lives inside the Telegram layer, not in a Windows dialog.
    auto *confirmation = new QFrame(host);
    confirmation->setObjectName(QStringLiteral("cgConfirmPanel"));
    auto *confirmLayout = new QHBoxLayout(confirmation);
    confirmLayout->setContentsMargins(10, 6, 10, 6);
    confirmLayout->setSpacing(6);
    auto *confirmText = MakeLabel(QStringLiteral("Сбросить локальное оформление?"), confirmation, "cgMuted");
    auto *cancelReset = new QPushButton(QStringLiteral("Отмена"), confirmation);
    auto *acceptReset = new QPushButton(QStringLiteral("Сбросить"), confirmation);
    acceptReset->setObjectName(QStringLiteral("cgConfirm"));
    confirmLayout->addWidget(confirmText, 1);
    confirmLayout->addWidget(cancelReset);
    confirmLayout->addWidget(acceptReset);
    confirmation->hide();
    root->addWidget(confirmation);

    const auto pageHeight = [avatarCard, bannerCard, frameCard](int index) {
        const auto natural = (index == 0) ? avatarCard->sizeHint().height()
            : (index == 1) ? bannerCard->sizeHint().height()
            : frameCard->sizeHint().height();
        return std::clamp(natural + 2, 150, kMaximumPageHeight);
    };
    const auto refreshHeight = [pages, pageHeight, resizePanel]() {
        resizePanel(pageHeight(pages->currentIndex()));
    };
    const auto newTab = [&](const QString &label, int index) {
        auto *button = new QPushButton(label, tabsRow);
        button->setObjectName(QStringLiteral("cgTab"));
        button->setCheckable(true);
        button->setChecked(index == 0);
        tabsLayout->addWidget(button, 1);
        QObject::connect(button, &QPushButton::clicked, host,
            [button, tabsRow, pages, fadeEffect, fadeAnimation, index, resizePanel, pageHeight] {
                if (pages->currentIndex() != index) {
                    fadeAnimation->stop();
                    pages->setCurrentIndex(index);
                    resizePanel(pageHeight(index));
                    fadeEffect->setOpacity(.25);
                    fadeAnimation->setStartValue(.25);
                    fadeAnimation->setEndValue(1.);
                    fadeAnimation->start();
                }
                for (const auto other : tabsRow->findChildren<QPushButton*>()) {
                    other->setChecked(other == button);
                }
            });
    };
    newTab(QStringLiteral("Аватар"), 0);
    newTab(QStringLiteral("Баннер"), 1);
    newTab(QStringLiteral("Рамки"), 2);

    QObject::connect(reset, &QPushButton::clicked, host, [confirmation, refreshHeight] {
        confirmation->show();
        refreshHeight();
    });
    QObject::connect(cancelReset, &QPushButton::clicked, host, [confirmation, refreshHeight] {
        confirmation->hide();
        refreshHeight();
    });
    QObject::connect(acceptReset, &QPushButton::clicked, host,
        [=] {
            const ProfileAppearance defaults;
            // Block signal callbacks: intermediate UI values must not overwrite
            // the clean default state while resetting the visible controls.
            {
                const QSignalBlocker avatarGuard(avatarCheck);
                const QSignalBlocker bannerGuard(bannerCheck);
                const QSignalBlocker frameGuard(frameStyle);
                avatarCheck->setChecked(defaults.avatarEnabled);
                bannerCheck->setChecked(defaults.bannerEnabled);
                frameStyle->setCurrentIndex(static_cast<int>(defaults.frameStyle));
                bannerHeight.reset(defaults.bannerHeight);
                bannerDarkening.reset(defaults.bannerDarkening);
                thickness.reset(defaults.frameThickness);
                scale.reset(defaults.frameScale);
                opacity.reset(defaults.frameOpacity);
                speed.reset(defaults.frameSpeed);
                offset.reset(defaults.frameOffset);
                avatarName->clear();
                bannerName->clear();
                frameName->clear();
            }
            // Clearing paths does not delete any imported files on disk.
            SetProfileAppearance(defaults);
            preview->update();
            confirmation->hide();
            refreshHeight();
        });

    pages->setCurrentIndex(0);
    // Post-layout pass provides accurate widget heights before the first display.
    QTimer::singleShot(0, host, [refreshHeight] { refreshHeight(); });
    box->addButton(tr::lng_close(), [box] { box->closeBox(); });
}

} // namespace Customgram

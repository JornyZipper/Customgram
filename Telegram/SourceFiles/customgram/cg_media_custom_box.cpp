#include "customgram/cg_media_custom_box.h"

#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "styles/style_layers.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QProcess>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtCore/QUuid>
#include <QtGui/QDesktopServices>
#include <QtGui/QImage>
#include <QtGui/QImageReader>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>
#include <QtCore/QUrl>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <memory>

namespace Customgram {
namespace {

const char kStyle[] = R"CGMEDIA(
QWidget#cgMediaRoot { background: transparent; }
QFrame#cgMediaCard { background: #192938; border: 1px solid #2d4559; border-radius: 11px; }
QLabel { background: transparent; color: #ebf4fc; border: none; }
QLabel#cgMuted { color: #91aac0; font-size: 11px; }
QLabel#cgHeading { color: #f2f8ff; font-size: 14px; font-weight: bold; }
QLabel#cgStatus { color: #73cafa; font-size: 11px; }
QPushButton { color: #eef6fc; background: #293e52; border: 1px solid #37536b;
              border-radius: 7px; padding: 7px 9px; }
QPushButton:hover { background: #37536c; }
QPushButton:disabled { color: #71879a; background: #223242; }
QPushButton#cgPrimary { background: #167bb8; border: 0px; font-weight: bold; }
QPushButton#cgPrimary:hover { background: #2294cf; }
QPushButton#cgTab { background: transparent; border: 0px; border-bottom: 2px solid transparent;
                     border-radius: 0; padding: 9px 1px; font-weight: bold; }
QPushButton#cgTab:checked { color: #62c5ff; border-bottom-color: #39aff0; }
QCheckBox { color: #eef6fc; spacing: 7px; }
QSpinBox, QDoubleSpinBox, QComboBox { color: #f1f7fc; background: #263c50;
   border: 1px solid #39566c; border-radius: 6px; padding: 4px; }
QScrollArea { border: 0px; background: transparent; }
QScrollBar:vertical { background: #192938; width: 7px; }
QScrollBar::handle:vertical { background: #3b637f; border-radius: 3px; }
)CGMEDIA";

struct MediaState {
    QString videoPath;
    QString ffmpegPath;
    QStringList stickerPaths;
    QLabel *videoPreview = nullptr;
    QLabel *videoStatus = nullptr;
    QLabel *stickersStatus = nullptr;
    QGridLayout *stickerGrid = nullptr;
    QWidget *stickerHost = nullptr;
    QDoubleSpinBox *start = nullptr;
    QDoubleSpinBox *duration = nullptr;
    QComboBox *size = nullptr;
    QCheckBox *compress = nullptr;
    QCheckBox *quality = nullptr;
    QPushButton *videoExport = nullptr;
    QPushButton *gifExport = nullptr;
    QPushButton *stickerExport = nullptr;
};

QLabel *MakeLabel(const QString &s, QWidget *parent, const char *id = nullptr) {
    auto *label = new QLabel(s, parent);
    label->setWordWrap(true);
    if (id) label->setObjectName(QString::fromLatin1(id));
    return label;
}

QFrame *Card(QWidget *parent, QVBoxLayout *&column) {
    auto *frame = new QFrame(parent);
    frame->setObjectName(QStringLiteral("cgMediaCard"));
    column = new QVBoxLayout(frame);
    column->setContentsMargins(13, 12, 13, 12);
    column->setSpacing(9);
    return frame;
}

QPushButton *Button(const QString &title, QWidget *parent, bool primary = false) {
    auto *button = new QPushButton(title, parent);
    if (primary) button->setObjectName(QStringLiteral("cgPrimary"));
    return button;
}

QString FindFfmpeg() {
    const auto dir = QCoreApplication::applicationDirPath();
    const auto candidates = QStringList{
        QDir(dir).filePath(QStringLiteral("modules/ffmpeg.exe")),
        QDir(dir).filePath(QStringLiteral("ffmpeg.exe")),
        QStringLiteral("ffmpeg.exe")
    };
    for (const auto &path : candidates) {
        if (path == QStringLiteral("ffmpeg.exe")) return path; // PATH fallback
        if (QFileInfo::exists(path)) return path;
    }
    return {};
}

QString MakeTemporaryPreviewPath() {
    const auto temp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    return QDir(temp).filePath(QStringLiteral("cg-media-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QStringLiteral(".png"));
}

QPixmap RoundPreview(const QImage &src, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    if (src.isNull()) return pixmap;
    const auto scaled = src.scaled(size, size, Qt::KeepAspectRatioByExpanding,
        Qt::SmoothTransformation);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath mask;
    mask.addEllipse(QRectF(1, 1, size - 2, size - 2));
    p.setClipPath(mask);
    const auto left = (scaled.width() - size) / 2;
    const auto top = (scaled.height() - size) / 2;
    p.drawImage(QPointF(0, 0), scaled.copy(left, top, size, size));
    return pixmap;
}

void RunFfmpeg(QWidget *owner, const QStringList &arguments,
    QLabel *status, QPushButton *button, const QString &output,
    const QString &successText, const QString &ffmpegPath) {
    if (button) button->setEnabled(false);
    status->setText(QStringLiteral("Обработка... Не закрывай редактор."));
    auto *process = new QProcess(owner);
    process->setProgram(ffmpegPath);
    process->setArguments(arguments);
    QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), owner,
        [process, status, button, output, successText](int exitCode, QProcess::ExitStatus exitStatus) {
            if (button) button->setEnabled(true);
            const auto success = (exitStatus == QProcess::NormalExit && exitCode == 0
                && QFileInfo::exists(output) && QFileInfo(output).size() > 0);
            if (success) {
                status->setText(successText + QStringLiteral("\n") + output);
            } else {
                const auto detail = QString::fromUtf8(process->readAllStandardError()).right(500);
                status->setText(QStringLiteral("Ошибка FFmpeg. Проверь исходный файл.\n") + detail);
            }
            process->deleteLater();
        });
    QObject::connect(process, &QProcess::errorOccurred, owner,
        [status, button, process](QProcess::ProcessError err) {
            if (err == QProcess::FailedToStart) {
                if (button) button->setEnabled(true);
                status->setText(QStringLiteral("FFmpeg не найден. Установи ffmpeg.exe в dist\\modules."));
            }
        });
    process->start();
}

void LoadPreview(QWidget *owner, const std::shared_ptr<MediaState> &state) {
    state->videoPreview->setText(QStringLiteral("Загрузка кадра..."));
    state->videoPreview->setPixmap(QPixmap());
    const auto file = MakeTemporaryPreviewPath();
    auto *process = new QProcess(owner);
    process->setProgram(state->ffmpegPath);
    process->setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"),
        QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-ss"), QStringLiteral("1"),
        QStringLiteral("-i"), state->videoPath, QStringLiteral("-frames:v"),
        QStringLiteral("1"), QStringLiteral("-vf"), QStringLiteral("scale=512:-2"), file});
    QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), owner, [process, state, file](int code, QProcess::ExitStatus) {
        if (code == 0) {
            const QImage img(file);
            if (!img.isNull()) {
                state->videoPreview->setPixmap(RoundPreview(img, 142));
                state->videoPreview->setText({});
            } else {
                state->videoPreview->setText(QStringLiteral("Нет предпросмотра"));
            }
        } else {
            state->videoPreview->setText(QStringLiteral("Не удалось получить кадр"));
        }
        QFile::remove(file);
        process->deleteLater();
    });
    QObject::connect(process, &QProcess::errorOccurred, owner,
        [process, state, file](QProcess::ProcessError err) {
            if (err == QProcess::FailedToStart) {
                state->videoPreview->setText(QStringLiteral("Требуется FFmpeg"));
                QFile::remove(file);
            }
        });
    process->start();
}

QImage ReadSticker(const QString &path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const auto dimensions = reader.size();
    // Refuse pathological allocations from untrusted image headers.
    if (dimensions.isValid() && (dimensions.width() > 8192 || dimensions.height() > 8192)) {
        return {};
    }
    const auto image = reader.read();
    if (image.isNull()) return {};
    return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

QImage PrepareSticker(const QImage &img) {
    if (img.isNull()) return {};
    QImage output(512, 512, QImage::Format_ARGB32_Premultiplied);
    output.fill(Qt::transparent);
    const auto fit = img.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&output);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage((512 - fit.width()) / 2, (512 - fit.height()) / 2, fit);
    return output;
}

void RefreshStickers(const std::shared_ptr<MediaState> &state) {
    // Destroy previous previews; layout items are removed before rebuilding.
    while (auto *item = state->stickerGrid->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (int i = 0; i < state->stickerPaths.size(); ++i) {
        const auto path = state->stickerPaths.at(i);
        auto *thumbnail = new QLabel(state->stickerHost);
        thumbnail->setFixedSize(72, 72);
        thumbnail->setAlignment(Qt::AlignCenter);
        const auto image = ReadSticker(path);
        if (image.isNull()) {
            thumbnail->setText(QStringLiteral("Ошибка"));
        } else {
            thumbnail->setPixmap(QPixmap::fromImage(image).scaled(68, 68,
                Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
        thumbnail->setToolTip(QFileInfo(path).fileName());
        state->stickerGrid->addWidget(thumbnail, i / 4, i % 4);
    }
    state->stickersStatus->setText(QStringLiteral("Добавлено изображений: %1 / 40")
        .arg(state->stickerPaths.size()));
    state->stickerExport->setEnabled(!state->stickerPaths.empty());
}

void ExportStickers(QWidget *parent, const std::shared_ptr<MediaState> &state) {
    if (state->stickerPaths.isEmpty()) return;
    const auto selected = QFileDialog::getExistingDirectory(parent,
        QStringLiteral("Выбери папку для PNG-стикеров"));
    if (selected.isEmpty()) return;
    const auto target = QDir(selected).filePath(QStringLiteral("Customgram-Stickers-"))
        + QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    if (!QDir().mkpath(target)) {
        QMessageBox::warning(parent, QStringLiteral("Customgram"),
            QStringLiteral("Не удалось создать папку экспорта."));
        return;
    }
    QJsonArray entries;
    int exported = 0;
    for (const auto &source : state->stickerPaths) {
        const auto img = PrepareSticker(ReadSticker(source));
        if (img.isNull()) continue;
        const auto filename = QStringLiteral("sticker_%1.png")
            .arg(exported + 1, 3, 10, QLatin1Char('0'));
        if (!img.save(QDir(target).filePath(filename), "PNG")) continue;
        QJsonObject entry;
        entry.insert(QStringLiteral("file"), filename);
        entry.insert(QStringLiteral("source"), QFileInfo(source).fileName());
        entries.append(entry);
        ++exported;
    }
    QJsonObject manifest;
    manifest.insert(QStringLiteral("type"), QStringLiteral("local-customgram-sticker-export"));
    manifest.insert(QStringLiteral("format"), QStringLiteral("PNG 512x512"));
    manifest.insert(QStringLiteral("stickers"), entries);
    QFile json(QDir(target).filePath(QStringLiteral("manifest.json")));
    if (json.open(QIODevice::WriteOnly)) {
        json.write(QJsonDocument(manifest).toJson(QJsonDocument::Indented));
        json.close();
    }
    if (!exported) {
        state->stickersStatus->setText(QStringLiteral("Экспорт не удался: изображения не прочитаны."));
        return;
    }
    state->stickersStatus->setText(QStringLiteral("Готово: %1 PNG-стикеров в %2")
        .arg(exported).arg(target));
    QDesktopServices::openUrl(QUrl::fromLocalFile(target));
}

} // namespace

void SetupMediaBox(Ui::GenericBox *box) {
    if (!box) return;
    box->setTitle(rpl::single(QStringLiteral("Customgram · Медиа")));
    auto *column = box->addRow(object_ptr<Ui::VerticalLayout>(box), st::boxPadding);
    auto *host = column->add(object_ptr<Ui::RpWidget>(column));
    host->setObjectName(QStringLiteral("cgMediaRoot"));
    host->setFixedHeight(540);
    host->setStyleSheet(QString::fromUtf8(kStyle));
    auto *root = new QVBoxLayout(host);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    const auto state = std::make_shared<MediaState>();
    state->ffmpegPath = FindFfmpeg();
    root->addWidget(MakeLabel(QStringLiteral(
        "Видео в кружочки, экспорт GIF и подготовка стикеров на этом устройстве."), host, "cgMuted"));

    auto *tabHost = new QWidget(host);
    auto *tabs = new QHBoxLayout(tabHost);
    tabs->setContentsMargins(0, 0, 0, 0);
    tabs->setSpacing(4);
    auto *pages = new QStackedWidget(host);
    auto *videoTab = Button(QStringLiteral("Кружки"), tabHost);
    auto *stickerTab = Button(QStringLiteral("Стикеры"), tabHost);
    videoTab->setObjectName(QStringLiteral("cgTab"));
    stickerTab->setObjectName(QStringLiteral("cgTab"));
    videoTab->setCheckable(true);
    stickerTab->setCheckable(true);
    videoTab->setChecked(true);
    tabs->addWidget(videoTab, 1);
    tabs->addWidget(stickerTab, 1);
    root->addWidget(tabHost);
    QObject::connect(videoTab, &QPushButton::clicked, host, [pages, videoTab, stickerTab] {
        pages->setCurrentIndex(0);
        videoTab->setChecked(true);
        stickerTab->setChecked(false);
    });
    QObject::connect(stickerTab, &QPushButton::clicked, host, [pages, videoTab, stickerTab] {
        pages->setCurrentIndex(1);
        videoTab->setChecked(false);
        stickerTab->setChecked(true);
    });

    // VIDEO PAGE
    auto *videoArea = new QScrollArea(pages);
    videoArea->setWidgetResizable(true);
    videoArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *videoBody = new QWidget(videoArea);
    auto *videoLayout = new QVBoxLayout(videoBody);
    videoLayout->setContentsMargins(0, 2, 0, 2);
    videoLayout->setSpacing(9);
    QVBoxLayout *videoCardLayout;
    auto *videoCard = Card(videoBody, videoCardLayout);
    videoCardLayout->addWidget(MakeLabel(QStringLiteral("Видео в кружочки"), videoCard, "cgHeading"));
    videoCardLayout->addWidget(MakeLabel(QStringLiteral(
        "Подготовь квадратный MP4 с нужным кадрированием. Для отправки как настоящий Telegram-кружок потребуется функция отправки video note."),
        videoCard, "cgMuted"));
    state->videoPreview = MakeLabel(QStringLiteral("Выбери видео"), videoCard);
    state->videoPreview->setFixedHeight(148);
    state->videoPreview->setAlignment(Qt::AlignCenter);
    videoCardLayout->addWidget(state->videoPreview);
    auto *chooseVideo = Button(QStringLiteral("Выбрать видео (MP4 / MOV / WEBM)"), videoCard, true);
    videoCardLayout->addWidget(chooseVideo);
    state->videoStatus = MakeLabel(QStringLiteral("Видео ещё не выбрано"), videoCard, "cgStatus");
    videoCardLayout->addWidget(state->videoStatus);

    auto *trimRow = new QWidget(videoCard);
    auto *trim = new QHBoxLayout(trimRow);
    trim->setContentsMargins(0, 0, 0, 0);
    trim->setSpacing(7);
    state->start = new QDoubleSpinBox(trimRow);
    state->start->setDecimals(1);
    state->start->setRange(0., 36000.);
    state->start->setSuffix(QStringLiteral(" с"));
    state->duration = new QDoubleSpinBox(trimRow);
    state->duration->setDecimals(1);
    state->duration->setRange(1., 60.);
    state->duration->setValue(15.);
    state->duration->setSuffix(QStringLiteral(" с"));
    trim->addWidget(MakeLabel(QStringLiteral("Начало"), trimRow));
    trim->addWidget(state->start, 1);
    trim->addWidget(MakeLabel(QStringLiteral("Длина"), trimRow));
    trim->addWidget(state->duration, 1);
    videoCardLayout->addWidget(trimRow);

    auto *resRow = new QWidget(videoCard);
    auto *res = new QHBoxLayout(resRow);
    res->setContentsMargins(0, 0, 0, 0);
    state->size = new QComboBox(resRow);
    state->size->addItems({QStringLiteral("320 × 320"), QStringLiteral("512 × 512")});
    state->size->setCurrentIndex(1);
    res->addWidget(MakeLabel(QStringLiteral("Разрешение"), resRow));
    res->addWidget(state->size, 1);
    videoCardLayout->addWidget(resRow);
    state->compress = new QCheckBox(QStringLiteral("Сжимать видео (меньше размер файла)"), videoCard);
    videoCardLayout->addWidget(state->compress);
    state->videoExport = Button(QStringLiteral("Экспортировать квадратный MP4"), videoCard, true);
    state->videoExport->setEnabled(false);
    state->gifExport = Button(QStringLiteral("Экспортировать GIF"), videoCard);
    state->gifExport->setEnabled(false);
    videoCardLayout->addWidget(state->videoExport);
    videoCardLayout->addWidget(state->gifExport);
    videoLayout->addWidget(videoCard);
    videoLayout->addStretch();
    videoArea->setWidget(videoBody);
    pages->addWidget(videoArea);

    QObject::connect(chooseVideo, &QPushButton::clicked, host, [host, state] {
        const auto path = QFileDialog::getOpenFileName(host, QStringLiteral("Выбрать видео"), {},
            QStringLiteral("Видео (*.mp4 *.mov *.mkv *.webm *.avi);;Все файлы (*)"));
        if (path.isEmpty()) return;
        state->videoPath = path;
        state->videoStatus->setText(QFileInfo(path).fileName());
        state->videoExport->setEnabled(true);
        state->gifExport->setEnabled(true);
        LoadPreview(host, state);
    });
    QObject::connect(state->videoExport, &QPushButton::clicked, host, [host, state] {
        if (state->videoPath.isEmpty()) return;
        const auto output = QFileDialog::getSaveFileName(host,
            QStringLiteral("Сохранить MP4"), QStringLiteral("Customgram-circle.mp4"),
            QStringLiteral("MP4 (*.mp4)"));
        if (output.isEmpty()) return;
        const auto target = output.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive)
            ? output : output + QStringLiteral(".mp4");
        const auto size = state->size->currentIndex() == 0 ? 320 : 512;
        // Crop to centered square, then encode a standard H.264 MP4. Telegram
        // requires its own VideoNote media type for the round chat bubble.
        const auto filter = QStringLiteral("crop='min(iw,ih)':'min(iw,ih)',scale=%1:%1,setsar=1,format=yuv420p")
            .arg(size);
        const auto args = QStringList{
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-ss"), QString::number(state->start->value(), 'f', 1),
            QStringLiteral("-i"), state->videoPath, QStringLiteral("-t"),
            QString::number(state->duration->value(), 'f', 1),
            QStringLiteral("-vf"), filter,
            QStringLiteral("-c:v"), QStringLiteral("libx264"), QStringLiteral("-preset"),
            QStringLiteral("medium"), QStringLiteral("-crf"),
            state->compress->isChecked() ? QStringLiteral("29") : QStringLiteral("21"),
            QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"),
            QStringLiteral("96k"), QStringLiteral("-movflags"), QStringLiteral("+faststart"), target
        };
        RunFfmpeg(host, args, state->videoStatus, state->videoExport, target,
            QStringLiteral("MP4 готов!"), state->ffmpegPath);
    });
    QObject::connect(state->gifExport, &QPushButton::clicked, host, [host, state] {
        if (state->videoPath.isEmpty()) return;
        const auto output = QFileDialog::getSaveFileName(host,
            QStringLiteral("Сохранить GIF"), QStringLiteral("Customgram.gif"),
            QStringLiteral("GIF (*.gif)"));
        if (output.isEmpty()) return;
        const auto target = output.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive)
            ? output : output + QStringLiteral(".gif");
        // Palette-based GIF for much better output than ffmpeg's default palette.
        const auto filter = QStringLiteral(
            "fps=12,scale=320:-1:flags=lanczos,split[a][b];[a]palettegen[p];[b][p]paletteuse");
        const auto args = QStringList{
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-y"), QStringLiteral("-ss"), QString::number(state->start->value(), 'f', 1),
            QStringLiteral("-i"), state->videoPath, QStringLiteral("-t"),
            QString::number(state->duration->value(), 'f', 1),
            QStringLiteral("-filter_complex"), filter, QStringLiteral("-an"), target
        };
        RunFfmpeg(host, args, state->videoStatus, state->gifExport, target,
            QStringLiteral("GIF готов!"), state->ffmpegPath);
    });

    // STICKER PAGE
    auto *stickerArea = new QScrollArea(pages);
    stickerArea->setWidgetResizable(true);
    stickerArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *stickerBody = new QWidget(stickerArea);
    auto *stickerLayout = new QVBoxLayout(stickerBody);
    stickerLayout->setContentsMargins(0, 2, 0, 2);
    stickerLayout->setSpacing(9);
    QVBoxLayout *stickerCardLayout;
    auto *stickerCard = Card(stickerBody, stickerCardLayout);
    stickerCardLayout->addWidget(MakeLabel(QStringLiteral("Локальный набор стикеров"),
        stickerCard, "cgHeading"));
    stickerCardLayout->addWidget(MakeLabel(QStringLiteral(
        "Выбери PNG/JPG/WEBP. Изображения будут подготовлены на прозрачном холсте 512×512. GIF импортируется только первым кадром."),
        stickerCard, "cgMuted"));
    auto *addSticker = Button(QStringLiteral("Добавить файлы"), stickerCard, true);
    auto *clearSticker = Button(QStringLiteral("Очистить список"), stickerCard);
    stickerCardLayout->addWidget(addSticker);
    stickerCardLayout->addWidget(clearSticker);
    state->stickerHost = new QWidget(stickerCard);
    state->stickerGrid = new QGridLayout(state->stickerHost);
    state->stickerGrid->setContentsMargins(0, 0, 0, 0);
    state->stickerGrid->setSpacing(5);
    stickerCardLayout->addWidget(state->stickerHost);
    state->stickersStatus = MakeLabel(QStringLiteral("Добавлено изображений: 0 / 40"),
        stickerCard, "cgStatus");
    stickerCardLayout->addWidget(state->stickersStatus);
    state->stickerExport = Button(QStringLiteral("Экспортировать PNG-набор"), stickerCard, true);
    state->stickerExport->setEnabled(false);
    stickerCardLayout->addWidget(state->stickerExport);
    stickerCardLayout->addWidget(MakeLabel(QStringLiteral(
        "Это файлы для будущего стикерпака, а не опубликованный набор в Telegram. Для публикации используй официальный редактор стикеров Telegram."),
        stickerCard, "cgMuted"));
    stickerLayout->addWidget(stickerCard);
    stickerLayout->addStretch();
    stickerArea->setWidget(stickerBody);
    pages->addWidget(stickerArea);
    QObject::connect(addSticker, &QPushButton::clicked, host, [host, state] {
        const auto files = QFileDialog::getOpenFileNames(host,
            QStringLiteral("Изображения для стикерпаков"), {},
            QStringLiteral("Картинки (*.png *.jpg *.jpeg *.webp *.gif)"));
        for (const auto &file : files) {
            if (state->stickerPaths.size() >= 40) break;
            if (!state->stickerPaths.contains(file) && !ReadSticker(file).isNull()) {
                state->stickerPaths.push_back(file);
            }
        }
        RefreshStickers(state);
    });
    QObject::connect(clearSticker, &QPushButton::clicked, host, [state] {
        state->stickerPaths.clear();
        RefreshStickers(state);
    });
    QObject::connect(state->stickerExport, &QPushButton::clicked, host,
        [host, state] { ExportStickers(host, state); });

    root->addWidget(pages, 1);
    root->addWidget(MakeLabel(QStringLiteral(
        "Обработка файлов локальная. Для видео нужен ffmpeg.exe в папке dist\\modules или в PATH."),
        host, "cgMuted"));
    box->addButton(tr::lng_close(), [box] { box->closeBox(); });
}

} // namespace Customgram

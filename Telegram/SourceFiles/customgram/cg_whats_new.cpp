#include "customgram/cg_whats_new.h"

#include "ui/layers/generic_box.h"
#include "ui/rp_widget.h"
#include "ui/vertical_list.h"
#include "window/window_controller.h"
#include "lang/lang_keys.h"
#include "styles/style_layers.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>

#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QTextBrowser>
#include <QtWidgets/QVBoxLayout>

namespace Customgram {
namespace {

QString SettingsPath() {
    auto root = QStandardPaths::writableLocation(
        QStandardPaths::AppLocalDataLocation);

    if (root.isEmpty()) {
        root = QDir::homePath() + QStringLiteral("/Customgram");
    }

    QDir().mkpath(root);
    return root + QStringLiteral("/customgram.ini");
}

QString PendingPath() {
    return QCoreApplication::applicationDirPath()
        + QStringLiteral("/whats-new.pending");
}

void SetupWhatsNewBox(
        Ui::GenericBox *box,
        const QString &version,
        const QString &name,
        const QString &body) {

    if (!box) return;

    box->setTitle(rpl::single(
        QStringLiteral("Customgram V%1 — What's New?").arg(version)));

    auto *column = box->addRow(
        object_ptr<Ui::VerticalLayout>(box),
        st::boxPadding);

    auto *host = column->add(
        object_ptr<Ui::RpWidget>(column));

    host->setFixedHeight(430);

    auto *root = new QVBoxLayout(host);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    auto *heading = new QLabel(
        name.isEmpty() ? QStringLiteral("Что нового?")
                       : name,
        host);

    heading->setStyleSheet(
        "color: #eaf1f7; font-size: 17px; font-weight: 600;");

    root->addWidget(heading);

    auto *description = new QTextBrowser(host);
    description->setFrameShape(QFrame::NoFrame);
    description->setOpenLinks(false);
    description->setOpenExternalLinks(false);
    description->setMarkdown(
        body.isEmpty()
            ? QStringLiteral("Описание обновления отсутствует.")
            : body);

    description->setStyleSheet(
        "QTextBrowser {"
        "background: #182633;"
        "color: #eaf1f7;"
        "border: 1px solid #2c4053;"
        "border-radius: 10px;"
        "padding: 9px;"
        "}");

    root->addWidget(description, 1);

    box->addButton(tr::lng_close(), [box] {
        box->closeBox();
    });
}

} // namespace

bool WhatsNewEnabled() {
    QSettings settings(SettingsPath(), QSettings::IniFormat);

    return settings.value(
        QStringLiteral("ui/showWhatsNew"), true).toBool();
}

void SetWhatsNewEnabled(bool enabled) {
    QSettings settings(SettingsPath(), QSettings::IniFormat);

    settings.setValue(
        QStringLiteral("ui/showWhatsNew"), enabled);

    settings.sync();
}

void ShowPendingWhatsNew(Window::Controller *controller) {
    if (!controller) return;

    QFile file(PendingPath());

    if (!file.exists()) return;

    if (!WhatsNewEnabled()) {
        file.remove();
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) return;

    const auto document = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!document.isObject()) {
        file.remove();
        return;
    }

    const auto data = document.object();

    const auto version = data.value(
        QStringLiteral("version")).toString().trimmed();

    if (version.isEmpty()) {
        file.remove();
        return;
    }

    const auto name = data.value(
        QStringLiteral("name")).toString();

    const auto body = data.value(
        QStringLiteral("body")).toString();

    controller->show(Box([=](not_null<Ui::GenericBox*> box) {
        SetupWhatsNewBox(box, version, name, body);
    }));

    file.remove();
}

} // namespace Customgram
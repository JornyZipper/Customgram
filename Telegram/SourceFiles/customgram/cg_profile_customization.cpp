#include "customgram/cg_profile_customization.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QSettings>
#include <QtCore/QStandardPaths>
#include <QtCore/QUuid>
#include <QtGui/QImageReader>
#include <QtGui/QMovie>
#include <algorithm>

namespace Customgram {
namespace {
QString SettingsFile() {
    return ProfileAppearanceDirectory() + QStringLiteral("/profile.ini");
}

ProfileAppearance ReadSettings() {
    QSettings ini(SettingsFile(), QSettings::IniFormat);
    ProfileAppearance v;
    v.avatarEnabled = ini.value(QStringLiteral("avatar/enabled"), false).toBool();
    v.avatarPath = ini.value(QStringLiteral("avatar/file")).toString();
    v.bannerEnabled = ini.value(QStringLiteral("banner/enabled"), false).toBool();
    v.bannerPath = ini.value(QStringLiteral("banner/file")).toString();
    v.bannerHeight = std::clamp(ini.value(QStringLiteral("banner/height"), 135).toInt(), 70, 240);
    v.bannerDarkening = std::clamp(ini.value(QStringLiteral("banner/darkening"), 15).toInt(), 0, 75);
    v.frameStyle = static_cast<FrameStyle>(std::clamp(ini.value(QStringLiteral("frame/style"), 0).toInt(), 0, 5));
    v.framePath = ini.value(QStringLiteral("frame/file")).toString();
    v.frameThickness = std::clamp(ini.value(QStringLiteral("frame/thickness"), 4).toInt(), 1, 16);
    v.frameScale = std::clamp(ini.value(QStringLiteral("frame/scale"), 100).toInt(), 85, 140);
    v.frameOpacity = std::clamp(ini.value(QStringLiteral("frame/opacity"), 100).toInt(), 10, 100);
    v.frameSpeed = std::clamp(ini.value(QStringLiteral("frame/speed"), 100).toInt(), 25, 300);
    v.frameOffset = std::clamp(ini.value(QStringLiteral("frame/offset"), 0).toInt(), -10, 20);
    return v;
}

struct Storage {
    ProfileAppearance value = ReadSettings();
    quint64 revision = 1;
};
Storage &Cached() { static Storage value; return value; }

void Save(const ProfileAppearance &v) {
    QSettings ini(SettingsFile(), QSettings::IniFormat);
    ini.setValue(QStringLiteral("avatar/enabled"), v.avatarEnabled);
    ini.setValue(QStringLiteral("avatar/file"), v.avatarPath);
    ini.setValue(QStringLiteral("banner/enabled"), v.bannerEnabled);
    ini.setValue(QStringLiteral("banner/file"), v.bannerPath);
    ini.setValue(QStringLiteral("banner/height"), v.bannerHeight);
    ini.setValue(QStringLiteral("banner/darkening"), v.bannerDarkening);
    ini.setValue(QStringLiteral("frame/style"), static_cast<int>(v.frameStyle));
    ini.setValue(QStringLiteral("frame/file"), v.framePath);
    ini.setValue(QStringLiteral("frame/thickness"), v.frameThickness);
    ini.setValue(QStringLiteral("frame/scale"), v.frameScale);
    ini.setValue(QStringLiteral("frame/opacity"), v.frameOpacity);
    ini.setValue(QStringLiteral("frame/speed"), v.frameSpeed);
    ini.setValue(QStringLiteral("frame/offset"), v.frameOffset);
    ini.sync();
}

QString TypeName(AssetKind kind) {
    switch (kind) {
    case AssetKind::Avatar: return QStringLiteral("avatar");
    case AssetKind::Banner: return QStringLiteral("banner");
    case AssetKind::Frame: return QStringLiteral("frame");
    }
    return QStringLiteral("image");
}
} // namespace

QString ProfileAppearanceDirectory() {
    auto root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (root.isEmpty()) root = QDir::homePath() + QStringLiteral("/Customgram");
    const auto location = root + QStringLiteral("/customgram-profile");
    QDir().mkpath(location);
    return location;
}

ProfileAppearance GetProfileAppearance() { return Cached().value; }
quint64 ProfileAppearanceRevision() { return Cached().revision; }

void SetProfileAppearance(const ProfileAppearance &next) {
    auto safe = next;
    safe.bannerHeight = std::clamp(safe.bannerHeight, 70, 240);
    safe.bannerDarkening = std::clamp(safe.bannerDarkening, 0, 75);
    safe.frameStyle = static_cast<FrameStyle>(std::clamp(static_cast<int>(safe.frameStyle), 0, 5));
    safe.frameThickness = std::clamp(safe.frameThickness, 1, 16);
    safe.frameScale = std::clamp(safe.frameScale, 85, 140);
    safe.frameOpacity = std::clamp(safe.frameOpacity, 10, 100);
    safe.frameSpeed = std::clamp(safe.frameSpeed, 25, 300);
    safe.frameOffset = std::clamp(safe.frameOffset, -10, 20);
    Save(safe);
    Cached().value = safe;
    ++Cached().revision;
}

bool ImportProfileAsset(AssetKind kind, const QString &source, QString *error) {
    const auto fail = [&](const QString &msg) {
        if (error) *error = msg;
        return false;
    };
    const QFileInfo f(source);
    const auto suffix = f.suffix().toLower();
    const auto gif = suffix == QStringLiteral("gif");
    const auto png = suffix == QStringLiteral("png");
    const auto jpg = suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg");
    if (!f.isFile() || !f.isReadable()) return fail(QStringLiteral("File cannot be read."));
    if (kind == AssetKind::Avatar && !gif) return fail(QStringLiteral("Avatar must be GIF."));
    if (kind == AssetKind::Frame && !(png || gif)) return fail(QStringLiteral("Frame must be PNG or GIF."));
    if (kind == AssetKind::Banner && !(png || jpg || gif)) return fail(QStringLiteral("Banner must be PNG, JPG or GIF."));
    const auto limit = gif ? 8LL * 1024 * 1024 : 12LL * 1024 * 1024;
    if (f.size() <= 0 || f.size() > limit) return fail(QStringLiteral("Image is too large (GIF 8 MB / images 12 MB)."));
    QImageReader reader(source);
    const auto fmt = reader.format().toLower();
    if ((gif && fmt != "gif") || (png && fmt != "png")
        || (jpg && fmt != "jpeg" && fmt != "jpg")) {
        return fail(QStringLiteral("File extension does not match its actual format."));
    }
    const auto size = reader.size();
    if (!size.isValid() || size.width() > 2048 || size.height() > 2048
        || size.width() < 64 || size.height() < 64) {
        return fail(QStringLiteral("Dimensions must be between 64 and 2048 pixels."));
    }
    if (kind != AssetKind::Banner && size.width() != size.height()) {
        return fail(QStringLiteral("Avatar and frame must be square."));
    }
    if (kind == AssetKind::Banner && size.width() < size.height()) {
        return fail(QStringLiteral("Banner must be landscape (width >= height)."));
    }
    const auto image = reader.read();
    if (image.isNull()) return fail(QStringLiteral("Failed to decode image."));
    if (kind == AssetKind::Frame
        && image.pixelColor(image.width() / 2, image.height() / 2).alpha() > 40) {
        return fail(QStringLiteral("The middle of a frame must be transparent."));
    }
    if (gif) {
        QMovie movie(source);
        if (!movie.isValid()) return fail(QStringLiteral("GIF decoder is unavailable."));
    }
    const auto dest = ProfileAppearanceDirectory() + QStringLiteral("/") + TypeName(kind)
        + QStringLiteral("-") + QUuid::createUuid().toString(QUuid::WithoutBraces)
        + QStringLiteral(".") + suffix;
    if (!QFile::copy(source, dest)) return fail(QStringLiteral("Could not copy image to Customgram app data."));
    auto s = GetProfileAppearance();
    switch (kind) {
    case AssetKind::Avatar: s.avatarPath = dest; s.avatarEnabled = true; break;
    case AssetKind::Banner: s.bannerPath = dest; s.bannerEnabled = true; break;
    case AssetKind::Frame: s.framePath = dest; s.frameStyle = FrameStyle::Custom; break;
    }
    SetProfileAppearance(s);
    return true;
}

} // namespace Customgram

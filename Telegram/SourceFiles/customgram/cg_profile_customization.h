#pragma once

// Customgram V13.0.5: local-only appearance preferences (no Telegram API calls).
#include <QtCore/QString>

namespace Customgram {

enum class AssetKind { Avatar, Banner, Frame };

enum class FrameStyle {
    None = 0,
    Neon = 1,
    Gold = 2,
    Gradient = 3,
    Animated = 4,
    Custom = 5
};

struct ProfileAppearance {
    bool avatarEnabled = false;
    QString avatarPath;
    bool bannerEnabled = false;
    QString bannerPath;
    int bannerHeight = 135; // 70..240 logical px
    int bannerDarkening = 15; // 0..75 percent
    FrameStyle frameStyle = FrameStyle::None;
    QString framePath;
    int frameThickness = 4; // 1..16 logical px
    int frameScale = 100; // 85..140 percent
    int frameOpacity = 100; // 10..100 percent
    int frameSpeed = 100; // 25..300 percent
    int frameOffset = 0; // -10..20 logical px
};

// All functions called on the GUI thread.
ProfileAppearance GetProfileAppearance();
void SetProfileAppearance(const ProfileAppearance &value);
quint64 ProfileAppearanceRevision();
QString ProfileAppearanceDirectory();

// Imports a media file into a private local app directory; updates saved path
// only after validation/copy succeeded. Avatar: GIF only; banner: PNG/JPG/GIF;
// frame: PNG/GIF with transparent center. Never uploads files to Telegram.
bool ImportProfileAsset(AssetKind kind, const QString &source, QString *error);

} // namespace Customgram

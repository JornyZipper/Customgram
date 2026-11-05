#pragma once

namespace Window {
class Controller;
}

namespace Customgram {

bool WhatsNewEnabled();
void SetWhatsNewEnabled(bool enabled);

void ShowPendingWhatsNew(Window::Controller *controller);

} // namespace Customgram
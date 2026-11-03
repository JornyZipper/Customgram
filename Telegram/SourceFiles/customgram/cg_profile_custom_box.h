#pragma once

namespace Ui { class GenericBox; }

namespace Customgram {
// Native Telegram overlay page. Does not create a standalone QDialog.
void SetupProfileCustomizationBox(Ui::GenericBox *box);
} // namespace Customgram

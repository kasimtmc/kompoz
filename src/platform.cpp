#include <kompoz/host.hpp>
#include <kompoz/platform.hpp>

namespace kompoz {

namespace {
ComposeHost* currentHost() { return &detail::Composer::require().host(); }
} // namespace

void UriHandler::openUri(const std::string& uri) const { host_->openUri(uri); }
UriHandler uriHandler() { return UriHandler(currentHost()); }

void SoftwareKeyboardController::show() const { host_->showSoftwareKeyboard(); }
void SoftwareKeyboardController::hide() const { host_->hideSoftwareKeyboard(); }
SoftwareKeyboardController softwareKeyboardController() { return SoftwareKeyboardController(currentHost()); }

void HapticFeedback::performHapticFeedback(HapticFeedbackType type) const { host_->performHapticFeedback(type); }
HapticFeedback hapticFeedback() { return HapticFeedback(currentHost()); }

void ClipboardManager::setText(const std::string& text) const { host_->setClipboardText(text); }
std::string ClipboardManager::getText() const { return host_->clipboardText(); }
ClipboardManager clipboardManager() { return ClipboardManager(currentHost()); }

Clipboard::Ready<std::optional<ClipEntry>> Clipboard::getClipEntry() const {
    std::string text = host_->clipboardText();
    return {text.empty() ? std::nullopt : std::optional(ClipEntry::withPlainText(std::move(text)))};
}
Clipboard::Done Clipboard::setClipEntry(std::optional<ClipEntry> entry) const {
    host_->setClipboardText(entry ? entry->text : std::string());
    return {};
}
Clipboard clipboard() { return Clipboard(currentHost()); }

void AutofillManager::commit() const {
    if (host_) host_->autofillDone(true);
}
void AutofillManager::cancel() const {
    if (host_) host_->autofillDone(false);
}
AutofillManager autofillManager() { return AutofillManager(currentHost()); }

} // namespace kompoz

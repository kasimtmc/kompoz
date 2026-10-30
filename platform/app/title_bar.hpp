// The title bar Kompoz draws at the top of a window the platform leaves undecorated
// (Metrics::titleBar): the app's title in the middle, the window's buttons on the sides the
// desktop puts them, dragged to move the window, double-clicked to maximize it, the window
// manager's menu on the secondary button. In the system's colors (its dark theme, its accent),
// not the app's: it stands in for the system's.
#pragma once

#include "platform.hpp"

#include <functional>
#include <optional>
#include <string>

namespace kompoz::platform {

// The window's content: `bar` above `content` (none: `content` alone, the whole window, as it
// would be without it). `act` does what the bar's buttons and gestures ask, at (x, y) in dp.
void WindowFrame(const std::optional<TitleBar>& bar, const std::string& title, const std::function<void(WindowAction, Offset)>& act,
                 const Content& content);

} // namespace kompoz::platform

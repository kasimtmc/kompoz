#pragma once

// Whether Linux has a microphone and a camera, for mediaQuery (hasMicrophone, hasCamera): a capture
// device ALSA lists (/proc/asound/pcm), a V4L2 device that captures video (/dev/video*). Neither
// opens a stream, so nothing asks the user and nothing shows the camera's light.

namespace kompoz::platform {

bool hasMicrophone();
bool hasCamera();

} // namespace kompoz::platform

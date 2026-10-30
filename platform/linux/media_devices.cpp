#include "media_devices.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace kompoz::platform {

bool hasMicrophone() {
    std::ifstream pcm("/proc/asound/pcm");  // "00-00: ALC269VB Analog : ALC269VB Analog : playback 1 : capture 1"
    for (std::string line; std::getline(pcm, line);)
        if (line.find(": capture ") != std::string::npos) return true;
    return false;
}

bool hasCamera() {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", ec)) {
        if (!entry.path().filename().string().starts_with("video")) continue;
        int fd = open(entry.path().c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        v4l2_capability caps{};
        bool captures = ioctl(fd, VIDIOC_QUERYCAP, &caps) == 0 &&
                        ((caps.capabilities & V4L2_CAP_DEVICE_CAPS) ? caps.device_caps : caps.capabilities) & V4L2_CAP_VIDEO_CAPTURE;
        close(fd);
        if (captures) return true;
    }
    return false;
}

} // namespace kompoz::platform

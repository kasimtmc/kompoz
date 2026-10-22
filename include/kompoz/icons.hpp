#pragma once

// A starter set of Material icons (Filled, 24x24), as in androidx.compose.material.icons:
//   Icon(Icons::Filled::Add, "Ekle");
// Path data from Google's Material Design icons, Apache License 2.0.

#include "graphics.hpp"

namespace kompoz::Icons {

namespace detail {
// A Material icon as Compose's materialIcon and materialPath make one: 24 x 24, one path filled
// black.
inline ImageVector materialIcon(std::string name, std::string_view pathData, bool autoMirror = false) {
    return ImageVector::Builder(std::move(name), 24, 24, 24, 24, std::nullopt, BlendMode::SrcIn, autoMirror)
        .addPath({.pathData = addPathNodes(pathData), .fill = Brush::solid(colors::Black), .strokeLineWidth = 1, .strokeLineJoin = StrokeJoin::Bevel,
                  .strokeLineMiter = 1})
        .build();
}
// (The paths of the icons that come mirrored too.)
// clang-format off
inline constexpr std::string_view ArrowBackPath = "M20,11H7.83l5.59,-5.59L12,4l-8,8 8,8 1.41,-1.41L7.83,13H20v-2z";
inline constexpr std::string_view ArrowForwardPath = "M12,4l-1.41,1.41L16.17,11H4v2h12.17l-5.58,5.59L12,20l8,-8z";
inline constexpr std::string_view KeyboardArrowLeftPath = "M15.41,16.59L10.83,12l4.58,-4.59L14,6l-6,6 6,6 1.41,-1.41z";
inline constexpr std::string_view KeyboardArrowRightPath = "M8.59,16.59L13.17,12 8.59,7.41 10,6l6,6 -6,6 -1.41,-1.41z";
inline constexpr std::string_view ListPath = "M3,13h2v-2H3v2zm0,4h2v-2H3v2zm0,-8h2V7H3v2zm4,4h14v-2H7v2zm0,4h14v-2H7v2zM7,7v2h14V7H7z";
inline constexpr std::string_view SendPath = "M2.01,21L23,12 2.01,3 2,10l15,2 -15,2z";
// clang-format on
} // namespace detail

namespace Filled {
// clang-format off
inline const ImageVector AccountCircle = detail::materialIcon("Filled.AccountCircle", "M12,2C6.48,2 2,6.48 2,12s4.48,10 10,10 10,-4.48 10,-10S17.52,2 12,2zm0,3c1.66,0 3,1.34 3,3s-1.34,3 -3,3 -3,-1.34 -3,-3 1.34,-3 3,-3zm0,14.2c-2.5,0 -4.71,-1.28 -6,-3.22 0.03,-1.99 4,-3.08 6,-3.08 1.99,0 5.97,1.09 6,3.08 -1.29,1.94 -3.5,3.22 -6,3.22z");
inline const ImageVector Add = detail::materialIcon("Filled.Add", "M19,13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z");
inline const ImageVector ArrowBack = detail::materialIcon("Filled.ArrowBack", detail::ArrowBackPath);
inline const ImageVector ArrowDropDown = detail::materialIcon("Filled.ArrowDropDown", "M7,10l5,5 5,-5z");
inline const ImageVector ArrowForward = detail::materialIcon("Filled.ArrowForward", detail::ArrowForwardPath);
inline const ImageVector Check = detail::materialIcon("Filled.Check", "M9,16.17L4.83,12l-1.42,1.41L9,19 21,7l-1.41,-1.41z");
inline const ImageVector CheckCircle = detail::materialIcon("Filled.CheckCircle", "M12,2C6.48,2 2,6.48 2,12s4.48,10 10,10 10,-4.48 10,-10S17.52,2 12,2zm-2,15l-5,-5 1.41,-1.41L10,14.17l7.59,-7.59L19,8l-9,9z");
inline const ImageVector Close = detail::materialIcon("Filled.Close", "M19,6.41L17.59,5 12,10.59 6.41,5 5,6.41 10.59,12 5,17.59 6.41,19 12,13.41 17.59,19 19,17.59 13.41,12z");
inline const ImageVector DarkMode = detail::materialIcon("Filled.DarkMode", "M12,3c-4.97,0 -9,4.03 -9,9s4.03,9 9,9s9,-4.03 9,-9c0,-0.46 -0.04,-0.92 -0.1,-1.36c-0.98,1.37 -2.58,2.26 -4.4,2.26c-2.98,0 -5.4,-2.42 -5.4,-5.4c0,-1.81 0.89,-3.42 2.26,-4.4C12.92,3.04 12.46,3 12,3L12,3z");
inline const ImageVector DateRange = detail::materialIcon("Filled.DateRange", "M9,11H7v2h2v-2zm4,0h-2v2h2v-2zm4,0h-2v2h2v-2zm2,-7h-1V2h-2v2H8V2H6v2H5c-1.11,0 -1.99,0.9 -1.99,2L3,20c0,1.1 0.89,2 2,2h14c1.1,0 2,-0.9 2,-2V6c0,-1.1 -0.9,-2 -2,-2zm0,16H5V9h14v11z");
inline const ImageVector Delete = detail::materialIcon("Filled.Delete", "M6,19c0,1.1 0.9,2 2,2h8c1.1,0 2,-0.9 2,-2V7H6v12zM19,4h-3.5l-1,-1h-5l-1,1H5v2h14V4z");
inline const ImageVector Edit = detail::materialIcon("Filled.Edit", "M3,17.25V21h3.75L17.81,9.94l-3.75,-3.75L3,17.25zM20.71,7.04c0.39,-0.39 0.39,-1.02 0,-1.41l-2.34,-2.34c-0.39,-0.39 -1.02,-0.39 -1.41,0l-1.83,1.83 3.75,3.75 1.83,-1.83z");
inline const ImageVector Email = detail::materialIcon("Filled.Email", "M20,4H4c-1.1,0 -1.99,0.9 -1.99,2L2,18c0,1.1 0.9,2 2,2h16c1.1,0 2,-0.9 2,-2V6c0,-1.1 -0.9,-2 -2,-2zm0,4l-8,5 -8,-5V6l8,5 8,-5v2z");
inline const ImageVector Favorite = detail::materialIcon("Filled.Favorite", "M12,21.35l-1.45,-1.32C5.4,15.36 2,12.28 2,8.5 2,5.42 4.42,3 7.5,3c1.74,0 3.41,0.81 4.5,2.09C13.09,3.81 14.76,3 16.5,3 19.58,3 22,5.42 22,8.5c0,3.78 -3.4,6.86 -8.55,11.54L12,21.35z");
inline const ImageVector FavoriteBorder = detail::materialIcon("Filled.FavoriteBorder", "M16.5,3c-1.74,0 -3.41,0.81 -4.5,2.09C10.91,3.81 9.24,3 7.5,3 4.42,3 2,5.42 2,8.5c0,3.78 3.4,6.86 8.55,11.54L12,21.35l1.45,-1.32C18.6,15.36 22,12.28 22,8.5 22,5.42 19.58,3 16.5,3zM12.1,18.55l-0.1,0.1 -0.1,-0.1C7.14,14.24 4,11.39 4,8.5 4,6.5 5.5,5 7.5,5c1.54,0 3.04,0.99 3.57,2.36h1.87C13.46,5.99 14.96,5 16.5,5c2,0 3.5,1.5 3.5,3.5 0,2.89 -3.14,5.74 -7.9,10.05z");
inline const ImageVector Home = detail::materialIcon("Filled.Home", "M10,20v-6h4v6h5v-8h3L12,3 2,12h3v8z");
inline const ImageVector Info = detail::materialIcon("Filled.Info", "M12,2C6.48,2 2,6.48 2,12s4.48,10 10,10 10,-4.48 10,-10S17.52,2 12,2zm1,15h-2v-6h2v6zm0,-8h-2V7h2v2z");
inline const ImageVector Keyboard = detail::materialIcon("Filled.Keyboard", "M20,5H4c-1.1,0 -1.99,0.9 -1.99,2L2,17c0,1.1 0.9,2 2,2h16c1.1,0 2,-0.9 2,-2V7c0,-1.1 -0.9,-2 -2,-2zM11,8h2v2h-2V8zM11,11h2v2h-2v-2zM8,8h2v2H8V8zM8,11h2v2H8v-2zM7,13H5v-2h2v2zM7,10H5V8h2v2zM16,17H8v-2h8v2zM16,13h-2v-2h2v2zM16,10h-2V8h2v2zM19,13h-2v-2h2v2zM19,10h-2V8h2v2z");
inline const ImageVector KeyboardArrowDown = detail::materialIcon("Filled.KeyboardArrowDown", "M7.41,8.59L12,13.17l4.59,-4.58L18,10l-6,6 -6,-6 1.41,-1.41z");
inline const ImageVector KeyboardArrowLeft = detail::materialIcon("Filled.KeyboardArrowLeft", detail::KeyboardArrowLeftPath);
inline const ImageVector KeyboardArrowRight = detail::materialIcon("Filled.KeyboardArrowRight", detail::KeyboardArrowRightPath);
inline const ImageVector KeyboardArrowUp = detail::materialIcon("Filled.KeyboardArrowUp", "M7.41,15.41L12,10.83l4.59,4.58L18,14l-6,-6 -6,6z");
inline const ImageVector List = detail::materialIcon("Filled.List", detail::ListPath);
inline const ImageVector Lock = detail::materialIcon("Filled.Lock", "M18,8h-1V6c0,-2.76 -2.24,-5 -5,-5S7,3.24 7,6v2H6c-1.1,0 -2,0.9 -2,2v10c0,1.1 0.9,2 2,2h12c1.1,0 2,-0.9 2,-2V10c0,-1.1 -0.9,-2 -2,-2zm-6,9c-1.1,0 -2,-0.9 -2,-2s0.9,-2 2,-2 2,0.9 2,2 -0.9,2 -2,2zm3.1,-9H8.9V6c0,-1.71 1.39,-3.1 3.1,-3.1 1.71,0 3.1,1.39 3.1,3.1v2z");
inline const ImageVector Menu = detail::materialIcon("Filled.Menu", "M3,18h18v-2H3v2zm0,-5h18v-2H3v2zm0,-7v2h18V6H3z");
inline const ImageVector MoreVert = detail::materialIcon("Filled.MoreVert", "M12,8c1.1,0 2,-0.9 2,-2s-0.9,-2 -2,-2 -2,0.9 -2,2 0.9,2 2,2zm0,2c-1.1,0 -2,0.9 -2,2s0.9,2 2,2 2,-0.9 2,-2 -0.9,-2 -2,-2zm0,6c-1.1,0 -2,0.9 -2,2s0.9,2 2,2 2,-0.9 2,-2 -0.9,-2 -2,-2z");
inline const ImageVector Notifications = detail::materialIcon("Filled.Notifications", "M12,22c1.1,0 2,-0.9 2,-2h-4c0,1.1 0.89,2 2,2zm6,-6v-5c0,-3.07 -1.64,-5.64 -4.5,-6.32V4c0,-0.83 -0.67,-1.5 -1.5,-1.5s-1.5,0.67 -1.5,1.5v0.68C7.63,5.36 6,7.92 6,11v5l-2,2v1h16v-1l-2,-2z");
inline const ImageVector Pause = detail::materialIcon("Filled.Pause", "M6,19h4V5H6v14zm8,-14v14h4V5h-4z");
inline const ImageVector Person = detail::materialIcon("Filled.Person", "M12,12c2.21,0 4,-1.79 4,-4s-1.79,-4 -4,-4 -4,1.79 -4,4 1.79,4 4,4zm0,2c-2.67,0 -8,1.34 -8,4v2h16v-2c0,-2.66 -5.33,-4 -8,-4z");
inline const ImageVector Place = detail::materialIcon("Filled.Place", "M12,2C8.13,2 5,5.13 5,9c0,5.25 7,13 7,13s7,-7.75 7,-13c0,-3.87 -3.13,-7 -7,-7zm0,9.5c-1.38,0 -2.5,-1.12 -2.5,-2.5s1.12,-2.5 2.5,-2.5 2.5,1.12 2.5,2.5 -1.12,2.5 -2.5,2.5z");
inline const ImageVector PlayArrow = detail::materialIcon("Filled.PlayArrow", "M8,5v14l11,-7z");
inline const ImageVector Refresh = detail::materialIcon("Filled.Refresh", "M17.65,6.35C16.2,4.9 14.21,4 12,4c-4.42,0 -7.99,3.58 -7.99,8s3.57,8 7.99,8c3.73,0 6.84,-2.55 7.73,-6h-2.08c-0.82,2.33 -3.04,4 -5.65,4 -3.31,0 -6,-2.69 -6,-6s2.69,-6 6,-6c1.66,0 3.14,0.69 4.22,1.78L13,11h7V4l-2.35,2.35z");
inline const ImageVector Remove = detail::materialIcon("Filled.Remove", "M19,13H5v-2h14v2z");
inline const ImageVector Schedule = detail::materialIcon("Filled.Schedule", "M11.99,2C6.47,2 2,6.48 2,12s4.47,10 9.99,10C17.52,22 22,17.52 22,12S17.52,2 11.99,2zM12,20c-4.42,0 -8,-3.58 -8,-8s3.58,-8 8,-8 8,3.58 8,8 -3.58,8 -8,8zM12.5,7H11v6l5.25,3.15 0.75,-1.23 -4.5,-2.67z");
inline const ImageVector Search = detail::materialIcon("Filled.Search", "M15.5,14h-0.79l-0.28,-0.27C15.41,12.59 16,11.11 16,9.5 16,5.91 13.09,3 9.5,3S3,5.91 3,9.5 5.91,16 9.5,16c1.61,0 3.09,-0.59 4.23,-1.57l0.27,0.28v0.79l5,4.99L20.49,19l-4.99,-5zM9.5,14C7.01,14 5,11.99 5,9.5S7.01,5 9.5,5 14,7.01 14,9.5 11.99,14 9.5,14z");
inline const ImageVector Send = detail::materialIcon("Filled.Send", detail::SendPath);
inline const ImageVector Settings = detail::materialIcon("Filled.Settings", "M19.14,12.94c0.04,-0.3 0.06,-0.61 0.06,-0.94c0,-0.32 -0.02,-0.64 -0.07,-0.94l2.03,-1.58c0.18,-0.14 0.23,-0.41 0.12,-0.61l-1.92,-3.32c-0.12,-0.22 -0.37,-0.29 -0.59,-0.22l-2.39,0.96c-0.5,-0.38 -1.03,-0.7 -1.62,-0.94L14.4,2.81c-0.04,-0.24 -0.24,-0.41 -0.48,-0.41h-3.84c-0.24,0 -0.43,0.17 -0.47,0.41L9.25,5.35C8.66,5.59 8.12,5.92 7.63,6.29L5.24,5.33c-0.22,-0.08 -0.47,0 -0.59,0.22L2.74,8.87C2.62,9.08 2.66,9.34 2.86,9.48l2.03,1.58C4.84,11.36 4.8,11.69 4.8,12s0.02,0.64 0.07,0.94l-2.03,1.58c-0.18,0.14 -0.23,0.41 -0.12,0.61l1.92,3.32c0.12,0.22 0.37,0.29 0.59,0.22l2.39,-0.96c0.5,0.38 1.03,0.7 1.62,0.94l0.36,2.54c0.05,0.24 0.24,0.41 0.48,0.41h3.84c0.24,0 0.44,-0.17 0.47,-0.41l0.36,-2.54c0.59,-0.24 1.13,-0.56 1.62,-0.94l2.39,0.96c0.22,0.08 0.47,0 0.59,-0.22l1.92,-3.32c0.12,-0.22 0.07,-0.47 -0.12,-0.61L19.14,12.94zM12,15.6c-1.98,0 -3.6,-1.62 -3.6,-3.6s1.62,-3.6 3.6,-3.6s3.6,1.62 3.6,3.6S13.98,15.6 12,15.6z");
inline const ImageVector Share = detail::materialIcon("Filled.Share", "M18,16.08c-0.76,0 -1.44,0.3 -1.96,0.77L8.91,12.7c0.05,-0.23 0.09,-0.46 0.09,-0.7s-0.04,-0.47 -0.09,-0.7l7.05,-4.11c0.54,0.5 1.25,0.81 2.04,0.81 1.66,0 3,-1.34 3,-3s-1.34,-3 -3,-3 -3,1.34 -3,3c0,0.24 0.04,0.47 0.09,0.7L8.04,9.81C7.5,9.31 6.79,9 6,9c-1.66,0 -3,1.34 -3,3s1.34,3 3,3c0.79,0 1.5,-0.31 2.04,-0.81l7.12,4.16c-0.05,0.21 -0.08,0.43 -0.08,0.65 0,1.61 1.31,2.92 2.92,2.92 1.61,0 2.92,-1.31 2.92,-2.92s-1.31,-2.92 -2.92,-2.92z");
inline const ImageVector Star = detail::materialIcon("Filled.Star", "M12,17.27L18.18,21l-1.64,-7.03L22,9.24l-7.19,-0.61L12,2 9.19,8.63 2,9.24l5.46,4.73L5.82,21z");
inline const ImageVector Visibility = detail::materialIcon("Filled.Visibility", "M12,4.5C7,4.5 2.73,7.61 1,12c1.73,4.39 6,7.5 11,7.5s9.27,-3.11 11,-7.5c-1.73,-4.39 -6,-7.5 -11,-7.5zM12,17c-2.76,0 -5,-2.24 -5,-5s2.24,-5 5,-5 5,2.24 5,5 -2.24,5 -5,5zm0,-8c-1.66,0 -3,1.34 -3,3s1.34,3 3,3 3,-1.34 3,-3 -1.34,-3 -3,-3z");
// clang-format on
} // namespace Filled

namespace Default = Filled;

// Icons that point somewhere, drawn mirrored right to left (Compose's Icons.AutoMirrored): a
// back arrow points to the start, a send arrow to the end.
namespace AutoMirrored::Filled {
inline const ImageVector ArrowBack = detail::materialIcon("AutoMirrored.Filled.ArrowBack", detail::ArrowBackPath, true);
inline const ImageVector ArrowForward = detail::materialIcon("AutoMirrored.Filled.ArrowForward", detail::ArrowForwardPath, true);
inline const ImageVector KeyboardArrowLeft = detail::materialIcon("AutoMirrored.Filled.KeyboardArrowLeft", detail::KeyboardArrowLeftPath, true);
inline const ImageVector KeyboardArrowRight = detail::materialIcon("AutoMirrored.Filled.KeyboardArrowRight", detail::KeyboardArrowRightPath, true);
inline const ImageVector List = detail::materialIcon("AutoMirrored.Filled.List", detail::ListPath, true);
inline const ImageVector Send = detail::materialIcon("AutoMirrored.Filled.Send", detail::SendPath, true);
} // namespace AutoMirrored::Filled

} // namespace kompoz::Icons

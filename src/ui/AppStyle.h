#pragma once

#include <QString>

namespace AppStyle {

// ── Color constants ─────────────────────────────────────────────────────────
inline constexpr const char* ColorAccent        = "#76B900";
inline constexpr const char* ColorAccentHover   = "#8fd400";
inline constexpr const char* ColorBgBase        = "#1a1a1a";
inline constexpr const char* ColorBgCard        = "#262626";
inline constexpr const char* ColorBgInput       = "#1e1e1e";
inline constexpr const char* ColorBgElevated    = "#2a2a2a";
inline constexpr const char* ColorBgButton      = "#2e2e2e";
inline constexpr const char* ColorBgButtonHover = "#383838";
inline constexpr const char* ColorBorder        = "#4a4a4a";
inline constexpr const char* ColorBorderLight   = "#555";
inline constexpr const char* ColorTextPrimary   = "#e0e0e0";
inline constexpr const char* ColorTextSecondary = "#d0d0d0";
inline constexpr const char* ColorTextMuted     = "#999";
inline constexpr const char* ColorBadgeLinux    = "#e8710a";
inline constexpr const char* ColorBadgeWindows  = "#1565c0";
inline constexpr const char* ColorDanger        = "#c0392b";
inline constexpr const char* ColorDangerButton  = "#f44336";
inline constexpr const char* ColorSuccessButton = "#4CAF50";

// ── Dynamic style builders ──────────────────────────────────────────────────

inline QString platformBadgeStyle(bool isLinux) {
    return QString(
        "font-size: 11px; font-weight: bold; padding: 4px 8px; "
        "border-radius: 4px; background-color: %1; color: white;")
        .arg(isLinux ? ColorBadgeLinux : ColorBadgeWindows);
}

inline QString playButtonStyle() {
    return QString(
        "QPushButton { background-color: %1; color: white; padding: 10px 20px; "
        "font-weight: bold; border-radius: 6px; border: none; }"
        "QPushButton:hover { background-color: %2; }")
        .arg(ColorAccent, ColorAccentHover);
}

inline QString playButtonRunningStyle() {
    return QString(
        "QPushButton { background-color: %1; color: white; padding: 10px 20px; "
        "font-weight: bold; border-radius: 6px; border: none; }")
        .arg(ColorDanger);
}

inline QString secondaryButtonStyle() {
    return QString(
        "QPushButton { background-color: %1; color: %2; padding: 10px 20px; "
        "border: 1px solid %3; border-radius: 6px; }"
        "QPushButton:hover { background-color: %4; border: 1px solid %5; }")
        .arg(ColorBgButton, ColorTextPrimary, ColorBorder, ColorBgButtonHover, ColorAccent);
}

inline QString comboPopupStyle() {
    return QString(
        "QAbstractItemView {"
        "  background-color: %1;"
        "  border: 1px solid %2;"
        "  color: %3;"
        "  selection-background-color: %4;"
        "  selection-color: #fff;"
        "  outline: 0;"
        "}")
        .arg(ColorBgElevated, ColorBorderLight, ColorTextPrimary, ColorAccent);
}

inline QString tooltipStyle() {
    return QString(
        "background-color: %1;"
        "color: %2;"
        "border: 1px solid %3;"
        "padding: 4px 8px;"
        "font-size: 12px;")
        .arg(ColorBgElevated, ColorTextPrimary, ColorBorderLight);
}

} // namespace AppStyle

#pragma once

// ============================================================================
// PanelUIController.h
// Platform-independent interface for the panel's native UI
// (PanelUIWin.cpp on Windows, PanelUIMac.mm on macOS).
// ============================================================================

#include "AIPanel.h"
#include "DockConfig.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct PanelThemeColors
{
    uint32_t backgroundRGB     = 0x535353; // Illustrator Medium Dark
    uint32_t surfaceRGB        = 0x454545;
    uint32_t surfaceHoverRGB   = 0x5E5E5E;
    uint32_t surfacePressedRGB = 0x383838;
    uint32_t borderRGB         = 0x3D3D3D;
    uint32_t textPrimaryRGB    = 0xEAEAEA;
    uint32_t textMutedRGB      = 0xAEAEAE;
    uint32_t accentRGB         = 0x2680EB; // Adobe Spectrum blue
    bool     isDark            = true;
};

struct PanelCallbacks
{
    std::function<void(size_t scriptIndex)> onRunScript;  // index into DockModel::scripts
    std::function<void()>                   onRefresh;
    std::function<void()>                   onOpenFolder;
};

class PanelUIController
{
public:
    virtual ~PanelUIController() = default;

    virtual bool Create(
        AIPanelPlatformWindow parentWindow,
        const PanelThemeColors& theme,
        const PanelCallbacks& callbacks) = 0;

    virtual void Destroy() = 0;

    // Rebuilds the buttons from a freshly loaded dock folder. Main thread only.
    virtual void SetModel(const DockModel& model) = 0;

    virtual void UpdateTheme(const PanelThemeColors& theme) = 0;

    // Safe to call from any thread.
    virtual void SetStatusText(const std::string& utf8Status, bool isError = false) = 0;

    virtual void Resize(int width, int height) = 0;

    static std::shared_ptr<PanelUIController> CreatePlatformController();
};

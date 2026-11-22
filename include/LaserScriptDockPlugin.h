#pragma once

// ============================================================================
// LaserScriptDockPlugin.h
// The Illustrator plugin: Window menu item, dockable panel, running scripts.
// ============================================================================

#include "IllustratorSDK.h"
#include "Plugin.hpp"
#include "AIPanel.h"
#include "AIMenu.h"
#include "AINotifier.h"
#include "PanelUIController.h"
#include "DockConfig.h"

#include <atomic>
#include <memory>
#include <string>

Plugin* AllocatePlugin(SPPluginRef pluginRef);
void FixupReload(Plugin* plugin);

class LaserScriptDockPlugin : public Plugin
{
public:
    explicit LaserScriptDockPlugin(SPPluginRef pluginRef);
    virtual ~LaserScriptDockPlugin();

    ASErr StartupPlugin(SPInterfaceMessage* message) override;
    ASErr ShutdownPlugin(SPInterfaceMessage* message) override;
    ASErr GoMenuItem(AIMenuMessage* message) override;
    ASErr Notify(AINotifierMessage* message) override;

    void OnPanelVisibilityChanged(AIBoolean isVisible);
    void OnPanelSizeChanged();
    void OnPanelClosed();
    void OnFlyoutMenuItemSelected(ai::uint32 itemID);

private:
    ASErr CreateFlyoutMenu();
    ASErr CreateDockablePanel();
    ASErr RegisterWindowMenu();
    ASErr RegisterNotifiers();

    void RefreshScripts();
    void RunScript(size_t index);
    void OpenDockFolder();
    void OpenSettingsFile();
    void ShutdownUI();

    PanelThemeColors QueryIllustratorTheme() const;
    void SyncMenuCheckmark();

    AIPanelRef            fPanel = nullptr;
    AIPanelFlyoutMenuRef  fFlyoutMenu = nullptr;
    AIMenuItemHandle      fWindowMenuItem = nullptr;
    AINotifierHandle      fShutdownNotifier = nullptr;

    std::shared_ptr<PanelUIController> fUIController;
    DockModel                          fModel;

    // Shared with the script worker so it never touches a deleted plugin.
    std::shared_ptr<std::atomic<bool>> fBusy = std::make_shared<std::atomic<bool>>(false);
    std::string                        fRunningName;
};

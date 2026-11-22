#pragma once

// ============================================================================
// LaserScriptDockID.h
// Names, identifiers and version of LaserScriptDock.aip
// ============================================================================

#define kLaserScriptDockPluginName      "LaserScriptDock"
#define kLaserScriptDockPluginVersion   "2.0.0"

// Dockable panel
#define kLaserScriptDockPanelID         "com.laserscriptdock.panel"
#define kLaserScriptDockPanelTitle      "Laser Script Dock"

// Window menu item
#define kLaserScriptDockMenuName        "Laser Script Dock Menu Item"
#define kLaserScriptDockMenuTitle       "Laser Script Dock"

// Notifiers
#define kLaserScriptDockShutdownNotifier "LaserScriptDock Shutdown Notifier"

// Folder (inside Documents) that holds the scripts and dock.json
#define kLaserScriptDockFolderName      "Laser Script Dock"

// Folder next to the plugin with the default scripts that are copied into
// Documents/Laser Script Dock the first time the panel starts.
#define kLaserScriptDockSeedFolderName  "Laser Script Dock defaults"

// Panel flyout (≡) menu items
enum class FlyoutMenuId : int
{
    Refresh           = 2001,
    OpenScriptsFolder = 2002,
    EditSettings      = 2003,
    AboutPlugin       = 2004
};

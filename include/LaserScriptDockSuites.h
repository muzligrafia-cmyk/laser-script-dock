#pragma once

// ============================================================================
// LaserScriptDockSuites.h
// Adobe Illustrator SDK Suite declarations
// ============================================================================

#include "IllustratorSDK.h"
#include "AIPanel.h"
#include "AIMenu.h"
#include "AIContext.h"
#include "AINotifier.h"
#include "AIUITheme.h"
#include "AIUser.h"
#include "AIUnicodeString.h"
#include "SPPlugs.h"
#include "Suites.hpp"

extern "C" {
    extern AIUnicodeStringSuite*     sAIUnicodeString;
    extern AIMenuSuite*              sAIMenu;
    extern AIPanelSuite*             sAIPanel;
    extern AIPanelFlyoutMenuSuite*   sAIPanelFlyoutMenu;
    extern AIAppContextSuite*        sAIAppContext;
    extern AINotifierSuite*          sAINotifier;
    extern AIUIThemeSuite*           sAIUITheme;
    extern AIUserSuite*              sAIUser;
    extern SPPluginsSuite*           sSPPlugins;
}

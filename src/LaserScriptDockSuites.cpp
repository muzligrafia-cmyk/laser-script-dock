// ============================================================================
// LaserScriptDockSuites.cpp
// Adobe Illustrator SDK Suite acquisition table
// ============================================================================

#include "LaserScriptDockSuites.h"

extern "C" {
    AIUnicodeStringSuite*     sAIUnicodeString   = nullptr;
    AIMenuSuite*              sAIMenu            = nullptr;
    AIPanelSuite*             sAIPanel           = nullptr;
    AIPanelFlyoutMenuSuite*   sAIPanelFlyoutMenu = nullptr;
    AIUIThemeSuite*           sAIUITheme         = nullptr;
    SPBlocksSuite*            sSPBlocks          = nullptr;
}

ImportSuite gImportSuites[] = {
    kAIUnicodeStringSuite,    kAIUnicodeStringSuiteVersion,    &sAIUnicodeString,
    kAIMenuSuite,             kAIMenuSuiteVersion,             &sAIMenu,
    kAIPanelSuite,            kAIPanelSuiteVersion,            &sAIPanel,
    kAIPanelFlyoutMenuSuite,  kAIPanelFlyoutMenuSuiteVersion,  &sAIPanelFlyoutMenu,
    kAIAppContextSuite,       kAIAppContextSuiteVersion,       &sAIAppContext,
    kAINotifierSuite,         kAINotifierSuiteVersion,         &sAINotifier,
    kAIUIThemeSuite,          kAIUIThemeSuiteVersion,          &sAIUITheme,
    kAIUserSuite,             kAIUserSuiteVersion,             &sAIUser,
    kSPPluginsSuite,          kSPPluginsSuiteVersion,          &sSPPlugins,
    kSPBlocksSuite,           kSPBlocksSuiteVersion,           &sSPBlocks,
    nullptr, 0, nullptr
};

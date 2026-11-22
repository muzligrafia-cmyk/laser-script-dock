// ============================================================================
// LaserScriptDockPlugin.cpp
// Plugin lifecycle, Window menu item, dockable panel, flyout menu, theme,
// and running the scripts listed in Documents/Laser Script Dock.
//
// NOTE: every Illustrator SDK call in this file is written exactly the way it
// compiled against the Illustrator 2027 SDK in v1. Change them carefully.
// ============================================================================

#include "LaserScriptDockPlugin.h"
#include "LaserScriptDockID.h"
#include "LaserScriptDockSuites.h"
#include "ScriptExecutor.h"

#include <cmath>
#include <cstring>
#include <filesystem>

Plugin* AllocatePlugin(SPPluginRef pluginRef)
{
    return new LaserScriptDockPlugin(pluginRef);
}

void FixupReload(Plugin* plugin)
{
    // No SDK-specific vtable fixup needed.
    (void)plugin;
}

// ----------------------------------------------------------------------------
// AIPanel callbacks -> plugin
// ----------------------------------------------------------------------------

namespace {

LaserScriptDockPlugin* PluginFromPanel(AIPanelRef panel)
{
    AIPanelUserData userData = nullptr;
    if (sAIPanel && sAIPanel->GetUserData(panel, userData) == kNoErr && userData)
        return static_cast<LaserScriptDockPlugin*>(userData);
    return nullptr;
}

void PanelVisibilityChangedProc(AIPanelRef panel, AIBoolean isVisible)
{
    if (auto* p = PluginFromPanel(panel))
        p->OnPanelVisibilityChanged(isVisible);
}

void PanelSizeChangedProc(AIPanelRef panel)
{
    if (auto* p = PluginFromPanel(panel))
        p->OnPanelSizeChanged();
}

void PanelClosedProc(AIPanelRef panel)
{
    if (auto* p = PluginFromPanel(panel))
        p->OnPanelClosed();
}

void PanelFlyoutMenuProc(AIPanelRef panel, ai::uint32 itemID)
{
    if (auto* p = PluginFromPanel(panel))
        p->OnFlyoutMenuItemSelected(itemID);
}

} // namespace

// ----------------------------------------------------------------------------
// Lifecycle
// ----------------------------------------------------------------------------

LaserScriptDockPlugin::LaserScriptDockPlugin(SPPluginRef pluginRef)
    : Plugin(pluginRef)
{
    strncpy(fPluginName, kLaserScriptDockPluginName, kMaxStringLength);
}

LaserScriptDockPlugin::~LaserScriptDockPlugin() = default;

ASErr LaserScriptDockPlugin::StartupPlugin(SPInterfaceMessage* message)
{
    ASErr err = Plugin::StartupPlugin(message);
    if (err != kNoErr)
        return err;

    err = CreateFlyoutMenu();
    if (err != kNoErr)
        return err;

    err = CreateDockablePanel();
    if (err != kNoErr)
        return err;

    err = RegisterWindowMenu();
    if (err != kNoErr)
        return err;

    err = RegisterNotifiers();
    if (err != kNoErr)
        return err;

    RefreshScripts();
    SyncMenuCheckmark();
    return kNoErr;
}

ASErr LaserScriptDockPlugin::ShutdownPlugin(SPInterfaceMessage* message)
{
    ShutdownUI();

    if (fPanel && sAIPanel)
    {
        sAIPanel->Destroy(fPanel);
        fPanel = nullptr;
    }

    if (fFlyoutMenu && sAIPanelFlyoutMenu)
    {
        sAIPanelFlyoutMenu->Destroy(fFlyoutMenu);
        fFlyoutMenu = nullptr;
    }

    return Plugin::ShutdownPlugin(message);
}

void LaserScriptDockPlugin::ShutdownUI()
{
    if (fUIController)
    {
        fUIController->Destroy();
        fUIController.reset();
    }
}

// ----------------------------------------------------------------------------
// Panel, flyout menu, Window menu, notifiers
// ----------------------------------------------------------------------------

ASErr LaserScriptDockPlugin::CreateFlyoutMenu()
{
    if (!sAIPanelFlyoutMenu)
        return kNoErr;

    ASErr err = sAIPanelFlyoutMenu->Create(fFlyoutMenu);
    if (err != kNoErr)
        return err;

    sAIPanelFlyoutMenu->AppendItem(fFlyoutMenu, static_cast<ai::uint32>(FlyoutMenuId::Refresh),
                                   ai::UnicodeString("Refresh Scripts"));
    sAIPanelFlyoutMenu->AppendItem(fFlyoutMenu, static_cast<ai::uint32>(FlyoutMenuId::OpenScriptsFolder),
                                   ai::UnicodeString("Open Scripts Folder"));
    sAIPanelFlyoutMenu->AppendItem(fFlyoutMenu, static_cast<ai::uint32>(FlyoutMenuId::EditSettings),
                                   ai::UnicodeString("Edit Button Names (dock.json)"));
    sAIPanelFlyoutMenu->AppendItem(fFlyoutMenu, static_cast<ai::uint32>(FlyoutMenuId::AboutPlugin),
                                   ai::UnicodeString("About Laser Script Dock"));
    return kNoErr;
}

ASErr LaserScriptDockPlugin::CreateDockablePanel()
{
    if (!sAIPanel)
        return kCantHappenErr;

    const AISize minSize       = { 220, 300 };
    const AISize preferredSize = { 290, 600 };
    const AISize maxSize       = { 640, 1600 };

    ASErr err = sAIPanel->Create(
        fPluginRef,
        ai::UnicodeString(kLaserScriptDockPanelID),
        ai::UnicodeString(kLaserScriptDockPanelTitle),
        1,     // host layout state count
        minSize,
        true,  // resizable
        fFlyoutMenu,
        this,
        fPanel);

    if (err != kNoErr || !fPanel)
        return err;

    sAIPanel->SetUserData(fPanel, this);
    sAIPanel->SetSizes(fPanel, minSize, preferredSize, preferredSize, maxSize);
    sAIPanel->SetVisibilityChangedNotifyProc(fPanel, PanelVisibilityChangedProc);
    sAIPanel->SetSizeChangedNotifyProc(fPanel, PanelSizeChangedProc);
    sAIPanel->SetClosedNotifyProc(fPanel, PanelClosedProc);

    if (fFlyoutMenu)
    {
        sAIPanel->SetFlyoutMenu(fPanel, fFlyoutMenu);
        sAIPanel->SetFlyoutMenuPreVisibilityProc(fPanel, nullptr);
        sAIPanel->SetFlyoutMenuProc(fPanel, PanelFlyoutMenuProc);
    }

    AIPanelPlatformWindow platformWindow = nullptr;
    err = sAIPanel->GetPlatformWindow(fPanel, platformWindow);
    if (err != kNoErr || !platformWindow)
        return err;

    fUIController = PanelUIController::CreatePlatformController();
    if (!fUIController)
        return kCantHappenErr;

    PanelCallbacks callbacks;
    callbacks.onRunScript  = [this](size_t index) { RunScript(index); };
    callbacks.onRefresh    = [this]() { RefreshScripts(); };
    callbacks.onOpenFolder = [this]() { OpenDockFolder(); };

    if (!fUIController->Create(platformWindow, QueryIllustratorTheme(), callbacks))
        return kCantHappenErr;

    return kNoErr;
}

ASErr LaserScriptDockPlugin::RegisterWindowMenu()
{
    if (!sAIMenu)
        return kCantHappenErr;

    AIPlatformAddMenuItemDataUS menuData {
        kWindowUtilsMenuGroup,
        ai::UnicodeString(kLaserScriptDockMenuTitle)
    };

    AIMenuItemOption options = kMenuItemWantsUpdateOption;
    return sAIMenu->AddMenuItem(fPluginRef, kLaserScriptDockMenuName, &menuData, options, &fWindowMenuItem);
}

ASErr LaserScriptDockPlugin::RegisterNotifiers()
{
    if (!sAINotifier)
        return kNoErr;

    // There's no theme-changed notifier in the 2027 headers, so the panel
    // re-reads Illustrator's brightness whenever it's shown or refreshed.
    sAINotifier->AddNotifier(fPluginRef, kLaserScriptDockShutdownNotifier,
                             kAIApplicationShutdownNotifier, &fShutdownNotifier);
    return kNoErr;
}

ASErr LaserScriptDockPlugin::GoMenuItem(AIMenuMessage* message)
{
    if (message && message->menuItem == fWindowMenuItem && fPanel && sAIPanel)
    {
        AIBoolean isShown = false;
        sAIPanel->IsShown(fPanel, isShown);
        sAIPanel->Show(fPanel, !isShown);
        SyncMenuCheckmark();
    }
    return kNoErr;
}

ASErr LaserScriptDockPlugin::Notify(AINotifierMessage* message)
{
    if (message && strcmp(message->type, kAIApplicationShutdownNotifier) == 0)
        ShutdownUI();
    return kNoErr;
}

// ----------------------------------------------------------------------------
// Panel events
// ----------------------------------------------------------------------------

void LaserScriptDockPlugin::OnPanelVisibilityChanged(AIBoolean isVisible)
{
    if (isVisible && fUIController)
        fUIController->UpdateTheme(QueryIllustratorTheme());
    SyncMenuCheckmark();
}

void LaserScriptDockPlugin::OnPanelSizeChanged()
{
    if (!fPanel || !sAIPanel || !fUIController)
        return;

    AISize size = { 290, 600 };
    if (sAIPanel->GetSize(fPanel, size) == kNoErr)
    {
        fUIController->Resize(static_cast<int>(std::round(size.width)),
                              static_cast<int>(std::round(size.height)));
    }
}

void LaserScriptDockPlugin::OnPanelClosed()
{
    SyncMenuCheckmark();
}

void LaserScriptDockPlugin::OnFlyoutMenuItemSelected(ai::uint32 itemID)
{
    switch (static_cast<FlyoutMenuId>(itemID))
    {
    case FlyoutMenuId::Refresh:
        RefreshScripts();
        break;

    case FlyoutMenuId::OpenScriptsFolder:
        OpenDockFolder();
        break;

    case FlyoutMenuId::EditSettings:
        OpenSettingsFile();
        break;

    case FlyoutMenuId::AboutPlugin:
        if (sAIUser)
        {
            std::string text =
                "Laser Script Dock v" kLaserScriptDockPluginVersion "\n\n"
                "Your scripts and button settings live in:\n" +
                ScriptExecutor::GetDockFolder() +
                "\n\nDrop a .jsx or .js file in there and press Refresh. "
                "Rename buttons, change groups and descriptions in dock.json.";
            sAIUser->MessageAlert(ai::UnicodeString(text, kAIUTF8CharacterEncoding));
        }
        break;
    }
}

void LaserScriptDockPlugin::SyncMenuCheckmark()
{
    if (!fWindowMenuItem || !fPanel || !sAIMenu || !sAIPanel)
        return;

    AIBoolean isShown = false;
    sAIPanel->IsShown(fPanel, isShown);
    sAIMenu->CheckItem(fWindowMenuItem, isShown);
}

// ----------------------------------------------------------------------------
// Scripts
// ----------------------------------------------------------------------------

void LaserScriptDockPlugin::RefreshScripts()
{
    if (!fUIController)
        return;

    const std::string folder = ScriptExecutor::GetDockFolder();
    if (folder.empty())
    {
        fUIController->SetStatusText("Couldn't find your Documents folder", true);
        return;
    }

    DockConfig::SeedIfEmpty(folder, ScriptExecutor::GetSeedFolder());
    fModel = DockConfig::Load(folder);

    fUIController->UpdateTheme(QueryIllustratorTheme());
    fUIController->SetModel(fModel);
    fUIController->SetStatusText(fModel.message, fModel.messageIsError);
}

void LaserScriptDockPlugin::RunScript(size_t index)
{
    if (!fUIController || index >= fModel.scripts.size())
        return;

    if (fBusy->load())
    {
        fUIController->SetStatusText("Still running " + fRunningName + " - finish that first", true);
        return;
    }

    const DockScript script = fModel.scripts[index];
    const std::string name = script.name;
    std::weak_ptr<PanelUIController> weakUI = fUIController;
    std::shared_ptr<std::atomic<bool>> busy = fBusy;

    busy->store(true);
    fRunningName = name;
    fUIController->SetStatusText("Running " + name + "...");

    // Make Illustrator treat this as our plugin's turn while we start the job.
    AIAppContextHandle appContext = nullptr;
    if (sAIAppContext)
        sAIAppContext->PushAppContext(fPluginRef, &appContext);

    std::string startError;
    const bool started = ScriptExecutor::RunScriptAsync(
        script.fullPath,
        [weakUI, busy, name](bool ok, const std::string& error) {
            busy->store(false);
            if (auto ui = weakUI.lock())
                ui->SetStatusText(ok ? (name + " finished") : (name + ": " + error), !ok);
        },
        startError);

    if (sAIAppContext && appContext)
        sAIAppContext->PopAppContext(appContext);

    if (!started)
    {
        busy->store(false);
        fUIController->SetStatusText(name + ": " + startError, true);
    }
}

void LaserScriptDockPlugin::OpenDockFolder()
{
    const std::string folder = ScriptExecutor::GetDockFolder();
    if (!folder.empty())
        ScriptExecutor::OpenInShell(folder);
}

void LaserScriptDockPlugin::OpenSettingsFile()
{
    const std::string folder = ScriptExecutor::GetDockFolder();
    if (folder.empty())
        return;

    // Make sure dock.json exists before opening it
    DockConfig::SeedIfEmpty(folder, ScriptExecutor::GetSeedFolder());
    DockConfig::Load(folder);

    const auto path = std::filesystem::u8path(folder) / DockConfig::kSettingsFileName;
    ScriptExecutor::OpenInShell(path.u8string());
}

// ----------------------------------------------------------------------------
// Theme (unchanged from v1)
// ----------------------------------------------------------------------------

PanelThemeColors LaserScriptDockPlugin::QueryIllustratorTheme() const
{
    PanelThemeColors colors; // defaults to Medium Dark

    if (!sAIUITheme)
        return colors;

    AIUIThemeColor themeColor {};
    if (sAIUITheme->GetUIThemeColor(kAIUIThemeSelectorPanel, kAIUIComponentColorBackground, themeColor) != kNoErr)
        return colors;

    const AIReal r = themeColor.red;
    const AIReal g = themeColor.green;
    const AIReal b = themeColor.blue;
    const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;

    auto packRGB = [](double rf, double gf, double bf) -> uint32_t {
        auto clampByte = [](double v) -> uint32_t {
            int iv = static_cast<int>(std::round(v * 255.0));
            if (iv < 0) iv = 0;
            if (iv > 255) iv = 255;
            return static_cast<uint32_t>(iv);
        };
        return (clampByte(rf) << 16) | (clampByte(gf) << 8) | clampByte(bf);
    };

    colors.backgroundRGB = packRGB(r, g, b);
    colors.isDark = (luma < 0.50);

    if (colors.isDark)
    {
        if (luma < 0.25)
        {
            // Darkest (~#323232)
            colors.surfaceRGB        = 0x3D3D3D;
            colors.surfaceHoverRGB   = 0x4A4A4A;
            colors.surfacePressedRGB = 0x2A2A2A;
            colors.borderRGB         = 0x262626;
        }
        else
        {
            // Medium Dark (~#535353)
            colors.surfaceRGB        = 0x454545;
            colors.surfaceHoverRGB   = 0x5E5E5E;
            colors.surfacePressedRGB = 0x383838;
            colors.borderRGB         = 0x3A3A3A;
        }
        colors.textPrimaryRGB = 0xEFEFEF;
        colors.textMutedRGB   = 0xB4B4B4;
        colors.accentRGB      = 0x2680EB;
    }
    else
    {
        if (luma > 0.85)
        {
            // Lightest (~#F0F0F0)
            colors.surfaceRGB        = 0xFFFFFF;
            colors.surfaceHoverRGB   = 0xE6E6E6;
            colors.surfacePressedRGB = 0xD8D8D8;
            colors.borderRGB         = 0xCCCCCC;
        }
        else
        {
            // Medium Light (~#B8B8B8)
            colors.surfaceRGB        = 0xC6C6C6;
            colors.surfaceHoverRGB   = 0xD2D2D2;
            colors.surfacePressedRGB = 0xA8A8A8;
            colors.borderRGB         = 0x999999;
        }
        colors.textPrimaryRGB = 0x1F1F1F;
        colors.textMutedRGB   = 0x555555;
        colors.accentRGB      = 0x1473E6;
    }

    return colors;
}

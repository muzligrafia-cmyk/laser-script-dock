#pragma once

// ============================================================================
// DockConfig.h
// Reads the "Laser Script Dock" folder: finds the scripts in it and reads
// dock.json (button names, groups, descriptions, order). New scripts that
// aren't in dock.json yet are added to it automatically under "Other".
// Platform independent: all paths are UTF-8 strings.
// ============================================================================

#include <string>
#include <vector>

struct DockScript
{
    std::string file;         // e.g. "Cleanup.js"
    std::string name;         // button title
    std::string category;     // group heading
    std::string description;  // small text under the title
    std::string fullPath;     // absolute UTF-8 path to the script file
};

struct DockCategory
{
    std::string         name;
    std::vector<size_t> scripts; // indexes into DockModel::scripts, in display order
};

struct DockModel
{
    std::string               folder;    // UTF-8 path of the dock folder
    std::vector<DockScript>   scripts;   // only scripts that exist and aren't hidden
    std::vector<DockCategory> categories;
    std::string               message;   // short summary for the status line
    bool                      messageIsError = false;
};

namespace DockConfig {

constexpr const char* kSettingsFileName = "dock.json";
constexpr const char* kNewScriptCategory = "Other";

// Copies the default scripts + dock.json from seedFolder into dockFolder,
// but only when dockFolder has no dock.json and no scripts yet.
// Either path may be empty / missing; then nothing happens.
void SeedIfEmpty(const std::string& dockFolder, const std::string& seedFolder);

// Scans the folder, reads dock.json, adds entries for new scripts (and saves
// dock.json if it changed and could be read without mistakes).
DockModel Load(const std::string& dockFolder);

} // namespace DockConfig

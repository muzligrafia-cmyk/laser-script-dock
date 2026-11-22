#pragma once

// ============================================================================
// ScriptExecutor.h
// Finding the dock folder and running a script file inside Illustrator.
// (ScriptExecutorWin.cpp / ScriptExecutorMac.mm)
// ============================================================================

#include <functional>
#include <string>

namespace ScriptExecutor {

// Called when a script has finished. On Windows this arrives on a worker
// thread, on macOS on the main thread - only do thread-safe things in it.
using CompletionCallback = std::function<void(bool success, const std::string& errorMessage)>;

// Documents/Laser Script Dock (UTF-8). Created if missing.
std::string GetDockFolder();

// Folder shipped next to the plugin with the default scripts (may not exist).
std::string GetSeedFolder();

// Starts the script without blocking the panel. Returns false (and fills
// errorOut) if it couldn't even be started.
bool RunScriptAsync(const std::string& scriptPathUtf8, CompletionCallback onDone, std::string& errorOut);

// Opens a folder in Explorer/Finder, or a file in its default app.
void OpenInShell(const std::string& pathUtf8);

} // namespace ScriptExecutor

// ============================================================================
// DockConfig.cpp
// ============================================================================

#include "DockConfig.h"
#include "MiniJson.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

fs::path PathFromUtf8(const std::string& utf8)
{
    return fs::u8path(utf8);
}

std::string Utf8FromPath(const fs::path& p)
{
    return p.u8string();
}

std::string Lower(std::string s)
{
    for (char& c : s)
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool IsScriptFile(const fs::path& p)
{
    std::string ext = Lower(Utf8FromPath(p.extension()));
    std::string stem = Utf8FromPath(p.stem());
    if (stem.empty() || stem[0] == '.' || stem[0] == '~')
        return false;
    return ext == ".jsx" || ext == ".js" || ext == ".jsxbin";
}

// Script files directly inside the folder, sorted by name (case-insensitive).
std::vector<std::string> ListScriptFiles(const fs::path& folder)
{
    std::vector<std::string> files;
    std::error_code ec;
    for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code fec;
        if (!it->is_regular_file(fec))
            continue;
        if (IsScriptFile(it->path()))
            files.push_back(Utf8FromPath(it->path().filename()));
    }
    std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
        return Lower(a) < Lower(b);
    });
    return files;
}

bool ReadFile(const fs::path& p, std::string& out)
{
    std::ifstream in(p, std::ios::binary);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Writes via a temporary file so a crash can never leave half a dock.json.
bool WriteFileSafely(const fs::path& p, const std::string& text)
{
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out)
            return false;
    }
    std::error_code ec;
    fs::rename(tmp, p, ec);
    if (ec)
    {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

std::string DefaultNameFor(const std::string& file)
{
    std::string stem = Utf8FromPath(PathFromUtf8(file).stem());
    for (char& c : stem)
    {
        if (c == '_')
            c = ' ';
    }
    return stem.empty() ? file : stem;
}

struct Entry
{
    std::string file;
    std::string name;
    std::string category;
    std::string description;
    bool        hidden = false;
};

const char* kHelpText =
    "Edit the names, groups (category), descriptions and order of the buttons here. "
    "Buttons appear in the order listed. Set hidden to true to hide a button. "
    "New scripts dropped into this folder are added at the bottom automatically. "
    "After saving, press Refresh in the panel.";

std::string BuildSettingsText(const std::vector<std::string>& categories, const std::vector<Entry>& entries)
{
    std::string s;
    s += "{\n";
    s += "  \"_help\": " + mj::Quote(kHelpText) + ",\n\n";
    s += "  \"categories\": [\n";
    for (size_t k = 0; k < categories.size(); ++k)
    {
        s += "    " + mj::Quote(categories[k]);
        s += (k + 1 < categories.size()) ? ",\n" : "\n";
    }
    s += "  ],\n\n";
    s += "  \"scripts\": [\n";
    for (size_t k = 0; k < entries.size(); ++k)
    {
        const Entry& e = entries[k];
        s += "    {\n";
        s += "      \"file\": " + mj::Quote(e.file) + ",\n";
        s += "      \"name\": " + mj::Quote(e.name) + ",\n";
        s += "      \"category\": " + mj::Quote(e.category) + ",\n";
        s += "      \"description\": " + mj::Quote(e.description) + ",\n";
        s += std::string("      \"hidden\": ") + (e.hidden ? "true" : "false") + "\n";
        s += (k + 1 < entries.size()) ? "    },\n" : "    }\n";
    }
    s += "  ]\n";
    s += "}\n";
    return s;
}

std::string Plural(size_t n, const char* one, const char* many)
{
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

} // namespace

namespace DockConfig {

void SeedIfEmpty(const std::string& dockFolder, const std::string& seedFolder)
{
    if (dockFolder.empty() || seedFolder.empty())
        return;

    const fs::path dock = PathFromUtf8(dockFolder);
    const fs::path seed = PathFromUtf8(seedFolder);
    std::error_code ec;

    if (!fs::is_directory(seed, ec))
        return;

    fs::create_directories(dock, ec);
    if (fs::exists(dock / kSettingsFileName, ec) || !ListScriptFiles(dock).empty())
        return; // folder already in use: never overwrite anything

    for (fs::directory_iterator it(seed, ec), end; !ec && it != end; it.increment(ec))
    {
        std::error_code fec;
        if (!it->is_regular_file(fec))
            continue;
        const fs::path name = it->path().filename();
        if (IsScriptFile(name) || Lower(Utf8FromPath(name)) == kSettingsFileName)
            fs::copy_file(it->path(), dock / name, fs::copy_options::skip_existing, fec);
    }
}

DockModel Load(const std::string& dockFolder)
{
    DockModel model;
    model.folder = dockFolder;

    const fs::path folder = PathFromUtf8(dockFolder);
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (!fs::is_directory(folder, ec))
    {
        model.message = "Can't open the scripts folder: " + dockFolder;
        model.messageIsError = true;
        return model;
    }

    const std::vector<std::string> files = ListScriptFiles(folder);
    const fs::path settingsPath = folder / kSettingsFileName;

    // ---- 1. Read dock.json --------------------------------------------------
    std::vector<std::string> categories;
    std::vector<Entry> entries;
    bool settingsExist = fs::exists(settingsPath, ec);
    bool settingsOk = true;
    std::string settingsError;

    if (settingsExist)
    {
        std::string text;
        mj::Value root;
        if (!ReadFile(settingsPath, text))
        {
            settingsOk = false;
            settingsError = "dock.json couldn't be opened";
        }
        else if (!mj::Parse(text, root, settingsError))
        {
            settingsOk = false;
            settingsError = "dock.json has a mistake on " + settingsError;
        }
        else if (!root.IsObject())
        {
            settingsOk = false;
            settingsError = "dock.json should start with {";
        }
        else
        {
            if (const mj::Value* cats = root.Get("categories"); cats && cats->IsArray())
            {
                for (const auto& c : *cats->array)
                {
                    if (c.IsString() && !c.text.empty() &&
                        std::find(categories.begin(), categories.end(), c.text) == categories.end())
                        categories.push_back(c.text);
                }
            }
            if (const mj::Value* list = root.Get("scripts"); list && list->IsArray())
            {
                for (const auto& item : *list->array)
                {
                    Entry e;
                    e.file = item.GetString("file");
                    if (e.file.empty())
                        continue;
                    // Ignore a second entry for the same file
                    bool dup = false;
                    for (const auto& prev : entries)
                        dup = dup || Lower(prev.file) == Lower(e.file);
                    if (dup)
                        continue;
                    e.name        = item.GetString("name", DefaultNameFor(e.file));
                    e.category    = item.GetString("category", kNewScriptCategory);
                    e.description = item.GetString("description");
                    e.hidden      = item.GetBool("hidden", false);
                    if (e.name.empty())     e.name = DefaultNameFor(e.file);
                    if (e.category.empty()) e.category = kNewScriptCategory;
                    entries.push_back(e);
                }
            }
        }
    }

    // ---- 2. Add scripts that aren't listed yet ------------------------------
    size_t added = 0;
    for (const auto& f : files)
    {
        bool known = false;
        for (const auto& e : entries)
            known = known || Lower(e.file) == Lower(f);
        if (known)
            continue;
        Entry e;
        e.file = f;
        e.name = DefaultNameFor(f);
        e.category = kNewScriptCategory;
        entries.push_back(e);
        ++added;
    }

    // Categories used by scripts but missing from the list go at the end
    for (const auto& e : entries)
    {
        if (std::find(categories.begin(), categories.end(), e.category) == categories.end())
            categories.push_back(e.category);
    }

    // ---- 3. Save dock.json when something new was added --------------------
    // Never overwrite a dock.json that has a mistake in it: that would wipe
    // the user's edits.
    bool saveFailed = false;
    if (settingsOk && (added > 0 || !settingsExist))
    {
        if (!WriteFileSafely(settingsPath, BuildSettingsText(categories, entries)))
            saveFailed = true;
    }

    // ---- 4. Build what the panel shows ---------------------------------------
    size_t missing = 0;
    size_t hidden = 0;
    for (const auto& e : entries)
    {
        bool exists = false;
        for (const auto& f : files)
            exists = exists || Lower(f) == Lower(e.file);
        if (!exists)
        {
            ++missing;
            continue;
        }
        if (e.hidden)
        {
            ++hidden;
            continue;
        }
        DockScript s;
        s.file        = e.file;
        s.name        = e.name;
        s.category    = e.category;
        s.description = e.description;
        s.fullPath    = Utf8FromPath(folder / PathFromUtf8(e.file));
        model.scripts.push_back(s);
    }

    for (const auto& c : categories)
    {
        DockCategory cat;
        cat.name = c;
        for (size_t k = 0; k < model.scripts.size(); ++k)
        {
            if (model.scripts[k].category == c)
                cat.scripts.push_back(k);
        }
        if (!cat.scripts.empty())
            model.categories.push_back(cat);
    }

    // ---- 5. Status message ----------------------------------------------------
    if (!settingsOk)
    {
        model.message = settingsError + " - using plain file names until it's fixed";
        model.messageIsError = true;
    }
    else if (saveFailed)
    {
        model.message = "Couldn't save dock.json (is it open or read-only?)";
        model.messageIsError = true;
    }
    else
    {
        model.message = Plural(model.scripts.size(), "script", "scripts") + " ready";
        if (added > 0)
            model.message += ", " + std::to_string(added) + " new added to dock.json";
        if (hidden > 0)
            model.message += ", " + std::to_string(hidden) + " hidden";
        if (missing > 0)
            model.message += ", " + std::to_string(missing) + " listed but missing";
    }

    return model;
}

} // namespace DockConfig

#pragma once

// ============================================================================
// MiniJson.h
// Tiny, forgiving JSON reader/writer for dock.json (no external libraries).
// Forgiving on purpose: allows trailing commas and // comment lines, and
// reports mistakes with a line number a human can find in Notepad.
// ============================================================================

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace mj {

struct Value;
using Array  = std::vector<Value>;
using Object = std::vector<std::pair<std::string, Value>>;

struct Value
{
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type                    type = Type::Null;
    bool                    boolean = false;
    double                  number = 0.0;
    std::string             text;
    std::shared_ptr<Array>  array;
    std::shared_ptr<Object> object;

    bool IsObject() const { return type == Type::Object && object; }
    bool IsArray()  const { return type == Type::Array && array; }
    bool IsString() const { return type == Type::String; }

    // Object helpers (return nullptr / default when missing or wrong type)
    const Value* Get(const std::string& key) const;
    std::string  GetString(const std::string& key, const std::string& fallback = "") const;
    bool         GetBool(const std::string& key, bool fallback = false) const;
};

// Parses UTF-8 text (a leading UTF-8 BOM is skipped).
// On failure returns false and fills errorOut with e.g. "line 12: expected , or }".
bool Parse(const std::string& text, Value& out, std::string& errorOut);

// Returns a JSON string literal (with quotes) for UTF-8 text.
std::string Quote(const std::string& utf8);

} // namespace mj

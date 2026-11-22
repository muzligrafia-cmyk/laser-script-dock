// ============================================================================
// MiniJson.cpp
// ============================================================================

#include "MiniJson.h"

#include <cstdint>
#include <cstdlib>

namespace mj {

const Value* Value::Get(const std::string& key) const
{
    if (!IsObject())
        return nullptr;
    for (const auto& kv : *object)
    {
        if (kv.first == key)
            return &kv.second;
    }
    return nullptr;
}

std::string Value::GetString(const std::string& key, const std::string& fallback) const
{
    const Value* v = Get(key);
    return (v && v->type == Type::String) ? v->text : fallback;
}

bool Value::GetBool(const std::string& key, bool fallback) const
{
    const Value* v = Get(key);
    return (v && v->type == Type::Bool) ? v->boolean : fallback;
}

namespace {

void AppendUtf8(std::string& out, uint32_t cp)
{
    if (cp < 0x80)
    {
        out += static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser
{
public:
    explicit Parser(const std::string& text) : t(text)
    {
        if (t.size() >= 3 &&
            static_cast<unsigned char>(t[0]) == 0xEF &&
            static_cast<unsigned char>(t[1]) == 0xBB &&
            static_cast<unsigned char>(t[2]) == 0xBF)
        {
            i = 3;
        }
    }

    bool Run(Value& out)
    {
        if (!ParseValue(out, 0))
            return false;
        SkipSpace();
        if (i < t.size())
            return Fail("extra text after the end of the settings");
        return true;
    }

    std::string error;

private:
    const std::string& t;
    size_t i = 0;

    bool Fail(const std::string& msg)
    {
        if (error.empty())
        {
            size_t upto = i < t.size() ? i : t.size();
            int line = 1;
            for (size_t k = 0; k < upto; ++k)
                if (t[k] == '\n')
                    ++line;
            error = "line " + std::to_string(line) + ": " + msg;
        }
        return false;
    }

    void SkipSpace()
    {
        while (i < t.size())
        {
            char c = t[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                ++i;
            }
            else if (c == '/' && i + 1 < t.size() && t[i + 1] == '/')
            {
                while (i < t.size() && t[i] != '\n')
                    ++i;
            }
            else
            {
                break;
            }
        }
    }

    bool ParseValue(Value& v, int depth)
    {
        if (depth > 64)
            return Fail("settings are nested too deeply");

        SkipSpace();
        if (i >= t.size())
            return Fail("the file ends too early (missing } or ]?)");

        char c = t[i];
        if (c == '{')
            return ParseObject(v, depth);
        if (c == '[')
            return ParseArray(v, depth);
        if (c == '"')
        {
            v.type = Value::Type::String;
            return ParseString(v.text);
        }
        if (t.compare(i, 4, "true") == 0)
        {
            v.type = Value::Type::Bool;
            v.boolean = true;
            i += 4;
            return true;
        }
        if (t.compare(i, 5, "false") == 0)
        {
            v.type = Value::Type::Bool;
            v.boolean = false;
            i += 5;
            return true;
        }
        if (t.compare(i, 4, "null") == 0)
        {
            v.type = Value::Type::Null;
            i += 4;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9'))
        {
            const char* start = t.c_str() + i;
            char* end = nullptr;
            v.type = Value::Type::Number;
            v.number = std::strtod(start, &end);
            if (end == start)
                return Fail("bad number");
            i += static_cast<size_t>(end - start);
            return true;
        }
        if (c == '\'' || static_cast<unsigned char>(c) >= 0x80)
            return Fail("text must be in straight double quotes \"like this\"");
        return Fail(std::string("unexpected character '") + c + "'");
    }

    bool ParseObject(Value& v, int depth)
    {
        ++i; // {
        v.type = Value::Type::Object;
        v.object = std::make_shared<Object>();

        for (;;)
        {
            SkipSpace();
            if (i >= t.size())
                return Fail("the file ends before a closing }");
            if (t[i] == '}')
            {
                ++i;
                return true;
            }
            if (t[i] != '"')
                return Fail("expected a \"name\" in double quotes (or a missing comma above?)");

            std::string key;
            if (!ParseString(key))
                return false;

            SkipSpace();
            if (i >= t.size() || t[i] != ':')
                return Fail("expected : after \"" + key + "\"");
            ++i;

            Value child;
            if (!ParseValue(child, depth + 1))
                return false;
            v.object->emplace_back(std::move(key), std::move(child));

            SkipSpace();
            if (i < t.size() && t[i] == ',')
            {
                ++i;
                continue; // trailing comma before } is allowed
            }
            if (i < t.size() && t[i] == '}')
            {
                ++i;
                return true;
            }
            return Fail("expected a comma or } (missing comma at the end of the line above?)");
        }
    }

    bool ParseArray(Value& v, int depth)
    {
        ++i; // [
        v.type = Value::Type::Array;
        v.array = std::make_shared<Array>();

        for (;;)
        {
            SkipSpace();
            if (i >= t.size())
                return Fail("the file ends before a closing ]");
            if (t[i] == ']')
            {
                ++i;
                return true;
            }

            Value child;
            if (!ParseValue(child, depth + 1))
                return false;
            v.array->push_back(std::move(child));

            SkipSpace();
            if (i < t.size() && t[i] == ',')
            {
                ++i;
                continue; // trailing comma before ] is allowed
            }
            if (i < t.size() && t[i] == ']')
            {
                ++i;
                return true;
            }
            return Fail("expected a comma or ] (missing comma at the end of the line above?)");
        }
    }

    bool ReadHex4(uint32_t& out)
    {
        if (i + 4 > t.size())
            return Fail("bad \\u escape");
        out = 0;
        for (int k = 0; k < 4; ++k)
        {
            char c = t[i++];
            out <<= 4;
            if (c >= '0' && c <= '9')      out |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
            else return Fail("bad \\u escape");
        }
        return true;
    }

    bool ParseString(std::string& out)
    {
        ++i; // opening quote
        out.clear();
        while (i < t.size())
        {
            char c = t[i++];
            if (c == '"')
                return true;
            if (c == '\n' || c == '\r')
            {
                --i;
                return Fail("text runs past the end of the line (missing closing \"?)");
            }
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (i >= t.size())
                break;
            char e = t[i++];
            switch (e)
            {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u':
            {
                uint32_t cp = 0;
                if (!ReadHex4(cp))
                    return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= t.size() && t[i] == '\\' && t[i + 1] == 'u')
                {
                    i += 2;
                    uint32_t lo = 0;
                    if (!ReadHex4(lo))
                        return false;
                    if (lo >= 0xDC00 && lo <= 0xDFFF)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                AppendUtf8(out, cp);
                break;
            }
            default:
                return Fail("unknown backslash code \\" + std::string(1, e) +
                            " (to write a backslash, type two: \\\\)");
            }
        }
        return Fail("text is missing its closing \"");
    }
};

} // namespace

bool Parse(const std::string& text, Value& out, std::string& errorOut)
{
    Parser p(text);
    out = Value();
    bool ok = p.Run(out);
    errorOut = ok ? std::string() : p.error;
    return ok;
}

std::string Quote(const std::string& utf8)
{
    std::string out = "\"";
    for (unsigned char c : utf8)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20)
            {
                const char* hex = "0123456789abcdef";
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 0xF];
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
    return out;
}

} // namespace mj

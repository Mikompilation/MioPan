#include "miopan_rml_language.h"
#include "../io/miopan_paths.h"
#include "system/os/system.h"
#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <unordered_map>
#include <vector>
namespace MioPanRmlLanguage {
namespace {
using TranslationTable = std::unordered_map<std::string, std::string>;
struct Pattern { std::regex expression; std::string translation; };
struct Language { TranslationTable text; std::vector<Pattern> patterns; bool loaded = false; };
std::array<Language,5> languages;
const std::regex placeholder("%([1-9][0-9]*\\$)?[-+ #0]*[0-9]*(\\.[0-9]+)?[diuoxXfFgGeEcs%]");
std::string UnescapePoString(const std::string &raw)
{
    std::string result;
    result.reserve(raw.size());

    for (size_t i = 0; i < raw.size(); i++)
    {
        if (raw[i] != '\\' || i + 1 >= raw.size())
        {
            result.push_back(raw[i]);
            continue;
        }

        i++;
        switch (raw[i])
        {
        case 'n':
            result.push_back('\n');
            break;
        case 't':
            result.push_back('\t');
            break;
        case '"':
            result.push_back('"');
            break;
        case '\\':
            result.push_back('\\');
            break;
        default:
            result.push_back(raw[i]);
            break;
        }
    }

    return result;
}

/* Extracts the content of one "..." literal starting at line[pos] (which
 * must be '"'). Returns the raw (still-escaped) content, and advances pos
 * past the closing quote. Returns false if the line doesn't start with a
 * quoted string at pos. */
bool ReadQuotedLiteral(const std::string &line, size_t pos, std::string &out)
{
    if (pos >= line.size() || line[pos] != '"')
    {
        return false;
    }

    out.clear();
    pos++;
    while (pos < line.size() && line[pos] != '"')
    {
        if (line[pos] == '\\' && pos + 1 < line.size())
        {
            out.push_back(line[pos]);
            out.push_back(line[pos + 1]);
            pos += 2;
            continue;
        }

        out.push_back(line[pos]);
        pos++;
    }

    return true;
}

/* Reads every "..." literal on directives that continue across lines, e.g.
 *   msgid "Hello, "
 *   "world"
 * and concatenates them (unescaped) into one string. `pos` is the index in
 * `lines` of the directive line itself (already past the "msgid"/"msgstr"
 * keyword); it's updated to the index of the last consumed line. */
std::string ReadPoValue(const std::vector<std::string> &lines,
                        size_t &pos,
                        size_t first_quote_pos)
{
    std::string value;
    std::string literal;

    if (ReadQuotedLiteral(lines[pos], first_quote_pos, literal))
    {
        value += UnescapePoString(literal);
    }

    while (pos + 1 < lines.size())
    {
        const std::string &next = lines[pos + 1];
        size_t start = next.find_first_not_of(" \t");
        if (start == std::string::npos || next[start] != '"')
        {
            break;
        }

        pos++;
        if (ReadQuotedLiteral(lines[pos], start, literal))
        {
            value += UnescapePoString(literal);
        }
    }

    return value;
}

void ParsePoContent(const std::string &content, TranslationTable &out)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= content.size())
    {
        size_t end = content.find('\n', start);
        if (end == std::string::npos)
        {
            lines.push_back(content.substr(start));
            break;
        }

        lines.push_back(content.substr(start, end - start));
        start = end + 1;
    }

    std::string pending_msgid;
    bool has_pending_msgid = false;

    for (size_t i = 0; i < lines.size(); i++)
    {
        std::string &line = lines[i];
        size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#')
        {
            continue;
        }

        if (line.compare(first, 6, "msgid ") == 0)
        {
            size_t quote = line.find('"', first + 6);
            if (quote == std::string::npos)
            {
                continue;
            }

            pending_msgid = ReadPoValue(lines, i, quote);
            has_pending_msgid = true;
        }
        else if (has_pending_msgid && line.compare(first, 7, "msgstr ") == 0)
        {
            size_t quote = line.find('"', first + 7);
            if (quote == std::string::npos)
            {
                continue;
            }

            std::string msgstr = ReadPoValue(lines, i, quote);
            if (!pending_msgid.empty() && !msgstr.empty())
            {
                out.emplace(std::move(pending_msgid), std::move(msgstr));
            }

            has_pending_msgid = false;
        }
    }
}


std::string EscapeRegex(const std::string &text)
{
    std::string result;
    for (char c : text) {
        if (std::string(".^$|()[]{}*+?\\").find(c) != std::string::npos) result += '\\';
        result += c;
    }
    return result;
}
Language &Load(int language)
{
    auto &table = languages[language];
    if (table.loaded) return table;
    table.loaded = true;
    const char *codes[] = {"en", "fr", "de", "es", "it"};
    const std::string relative = std::string("resources/lang/") + codes[language] + ".po";
    char path[4096];
    if (!MioPan_PathResolveAsset(relative.c_str(), path, sizeof(path))) return table;
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    if (!input) return table;
    ParsePoContent(std::string(std::istreambuf_iterator<char>(input), {}), table.text);
    for (const auto &[key,value] : table.text) {
        std::string expression; size_t end = 0; bool has_argument = false;
        for (auto it=std::sregex_iterator(key.begin(),key.end(),placeholder); it!=std::sregex_iterator(); ++it) {
            expression += EscapeRegex(key.substr(end,size_t(it->position())-end));
            expression += it->str()=="%%" ? "%" : "(.*?)";
            has_argument = has_argument || it->str()!="%%";
            end = size_t(it->position()+it->length());
        }
        if (has_argument) {
            expression += EscapeRegex(key.substr(end));
            table.patterns.push_back({std::regex("^"+expression+"$"),value});
        }
    }
    return table;
}
}
std::string Translate(const std::string &text)
{
    const int language = GetLanguage();
    if (language < 1 || language > 4) return text;
    auto &table = Load(language);
    const auto first=text.find_first_not_of(" \t\r\n"), last=text.find_last_not_of(" \t\r\n");
    if (first==std::string::npos) return text;
    std::string key=text.substr(first,last-first+1), translated;
    for (size_t at=0; (at=key.find("&amp;",at))!=std::string::npos;) key.replace(at,5,"&");
    const auto found=table.text.find(key);
    if (found!=table.text.end()) translated=found->second;
    else for (const auto &pattern : table.patterns) {
        std::smatch args;
        if (!std::regex_match(key,args,pattern.expression)) continue;
        size_t end=0, argument=1;
        for (auto it=std::sregex_iterator(pattern.translation.begin(),pattern.translation.end(),placeholder); it!=std::sregex_iterator(); ++it) {
            translated += pattern.translation.substr(end,size_t(it->position())-end);
            if (it->str()=="%%") translated += '%';
            else {
                const size_t index=(*it)[1].matched ? size_t(std::strtoul((*it)[1].str().c_str(), nullptr, 10)) : argument++;
                if (index>=args.size()) return text;
                const auto inner=table.text.find(args[index].str());
                translated += inner==table.text.end() ? args[index].str() : inner->second;
            }
            end=size_t(it->position()+it->length());
        }
        translated += pattern.translation.substr(end);
        break;
    }
    return translated.empty() ? text : text.substr(0,first)+translated+text.substr(last+1);
}
}

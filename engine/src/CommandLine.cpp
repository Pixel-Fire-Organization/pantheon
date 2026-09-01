#include "core/CommandLine.h"
#include "core/EngineDebug.h"

#include <cstdlib>
#include <cstring>

namespace
{

    // Case-insensitive compare of two null-terminated strings. tolower() from
    // <cctype> is avoided: this runs before the platform is up, and the option
    // grammar only ever allows ASCII.
    bool EqualsIgnoreCase(const char* first, const char* second)
    {
        while (*first && *second)
        {
            char charA = *first;
            char charB = *second;
            if (charA >= 'A' && charA <= 'Z')
                charA = static_cast<char>(charA - 'A' + 'a');
            if (charB >= 'A' && charB <= 'Z')
                charB = static_cast<char>(charB - 'A' + 'a');
            if (charA != charB)
                return false;
            ++first;
            ++second;
        }
        return *first == '\0' && *second == '\0';
    }

    // True when `token` reads as an option rather than a value. A bare "-" and a
    // negative number are values, so "--offset -5" parses the way a caller
    // expects instead of losing the -5.
    bool IsOptionToken(const char* token)
    {
        if (!token || token[0] != '-' || token[1] == '\0')
            return false;
        if (token[1] == '-')
            return token[2] != '\0'; // "--" alone is the terminator, not an option
        return !(token[1] >= '0' && token[1] <= '9');
    }

} // namespace

CommandLine::CommandLine() : m_optionCount(0), m_positionalCount(0), m_overflowed(false)
{
    memset(m_options, 0, sizeof(m_options));
    memset(m_positionals, 0, sizeof(m_positionals));
}

bool CommandLine::AddOption(const char* name, uint32_t nameLength, const char* value)
{
    if (m_optionCount >= CMD_MAX_OPTIONS)
    {
        Engine_LogError("CommandLine: more than %d options; '%.*s' and everything after it were dropped.", CMD_MAX_OPTIONS, static_cast<int>(nameLength), name);
        m_overflowed = true;
        return false;
    }

    m_options[m_optionCount].name = name;
    m_options[m_optionCount].nameLength = nameLength;
    m_options[m_optionCount].value = value;
    ++m_optionCount;
    return true;
}

bool CommandLine::AddPositional(const char* value)
{
    if (m_positionalCount >= CMD_MAX_POSITIONALS)
    {
        Engine_LogError("CommandLine: more than %d positional arguments; '%s' and everything after it were dropped.", CMD_MAX_POSITIONALS, value);
        m_overflowed = true;
        return false;
    }

    m_positionals[m_positionalCount] = value;
    ++m_positionalCount;
    return true;
}

bool CommandLine::Parse(int argc, char** argv)
{
    m_optionCount = 0;
    m_positionalCount = 0;
    m_overflowed = false;

    if (argc <= 0 || !argv)
        return true;

    bool optionsTerminated = false;

    for (int i = 0; i < argc; ++i)
    {
        const char* token = argv[i];
        if (!token)
            continue;

        // argv[0] is the executable / device path. It is always positional 0, so
        // the PS2 resource-location token survives parsing untouched.
        if (i == 0 || optionsTerminated)
        {
            AddPositional(token);
            continue;
        }

        if (token[0] == '-' && token[1] == '-' && token[2] == '\0')
        {
            optionsTerminated = true;
            continue;
        }

        if (!IsOptionToken(token))
        {
            AddPositional(token);
            continue;
        }

        const char* name = (token[1] == '-') ? token + 2 : token + 1;

        // --name=value / -n=value: the value lives inside this same token.
        const char* equals = strchr(name, '=');
        if (equals)
        {
            AddOption(name, static_cast<uint32_t>(equals - name), equals + 1);
            continue;
        }

        // --name value: consume the next token unless it is itself an option.
        const uint32_t nameLength = static_cast<uint32_t>(strlen(name));
        if (i + 1 < argc && argv[i + 1] && !IsOptionToken(argv[i + 1]))
        {
            AddOption(name, nameLength, argv[i + 1]);
            ++i;
        }
        else
        {
            // Bare flag. Storing "1" lets GetBool/GetInt read it with no special case.
            AddOption(name, nameLength, "1");
        }
    }

    return !m_overflowed;
}

const CommandLine::Option* CommandLine::Find(const char* name) const
{
    if (!name)
        return nullptr;

    const size_t length = strlen(name);
    for (uint32_t i = 0; i < m_optionCount; ++i)
    {
        if (m_options[i].nameLength != length)
            continue;
        if (strncmp(m_options[i].name, name, length) == 0)
            return &m_options[i];
    }
    return nullptr;
}

bool CommandLine::HasOption(const char* name) const { return Find(name) != nullptr; }

const char* CommandLine::GetString(const char* name, const char* fallback) const
{
    const Option* option = Find(name);
    return option ? option->value : fallback;
}

int32_t CommandLine::GetInt(const char* name, int32_t fallback) const
{
    const Option* option = Find(name);
    if (!option || !option->value || option->value[0] == '\0')
        return fallback;

    char* end = nullptr;
    const long parsed = strtol(option->value, &end, 10);
    if (end == option->value || *end != '\0')
    {
        Engine_LogError("CommandLine: --%s expects an integer, got '%s'. Using %d.", name, option->value, fallback);
        return fallback;
    }
    return static_cast<int32_t>(parsed);
}

float CommandLine::GetFloat(const char* name, float fallback) const
{
    const Option* option = Find(name);
    if (!option || !option->value || option->value[0] == '\0')
        return fallback;

    char* end = nullptr;
    const double parsed = strtod(option->value, &end);
    if (end == option->value || *end != '\0')
    {
        Engine_LogError("CommandLine: --%s expects a number, got '%s'. Using %f.", name, option->value, static_cast<double>(fallback));
        return fallback;
    }
    return static_cast<float>(parsed);
}

bool CommandLine::GetBool(const char* name, bool fallback) const
{
    const Option* option = Find(name);
    if (!option || !option->value)
        return fallback;

    const char* value = option->value;
    if (EqualsIgnoreCase(value, "1") || EqualsIgnoreCase(value, "true") || EqualsIgnoreCase(value, "yes") || EqualsIgnoreCase(value, "on"))
        return true;
    if (EqualsIgnoreCase(value, "0") || EqualsIgnoreCase(value, "false") || EqualsIgnoreCase(value, "no") || EqualsIgnoreCase(value, "off"))
        return false;

    Engine_LogError("CommandLine: --%s expects a boolean (1/0, true/false, yes/no, on/off), got '%s'. Using %s.", name, value, fallback ? "true" : "false");
    return fallback;
}

int32_t CommandLine::GetEnum(const char* name, const CommandLineEnumEntry* table, uint32_t tableCount, int32_t fallback) const
{
    const Option* option = Find(name);
    if (!option || !option->value || !table || tableCount == 0)
        return fallback;

    for (uint32_t i = 0; i < tableCount; ++i)
    {
        if (table[i].name && EqualsIgnoreCase(option->value, table[i].name))
            return table[i].value;
    }

    // Unrecognised: say exactly what would have worked.
    Engine_LogError("CommandLine: --%s does not accept '%s'.", name, option->value);
    for (uint32_t i = 0; i < tableCount; ++i)
    {
        if (table[i].name)
            Engine_LogError("CommandLine:   accepted value: %s", table[i].name);
    }
    return fallback;
}

uint32_t CommandLine::GetPositionalCount() const { return m_positionalCount; }

const char* CommandLine::GetPositional(uint32_t index) const { return (index < m_positionalCount) ? m_positionals[index] : nullptr; }

uint32_t CommandLine::GetOptionCount() const { return m_optionCount; }

const char* CommandLine::GetOptionName(uint32_t index) const { return (index < m_optionCount) ? m_options[index].name : nullptr; }

uint32_t CommandLine::GetOptionNameLength(uint32_t index) const { return (index < m_optionCount) ? m_options[index].nameLength : 0u; }

const char* CommandLine::GetOptionValue(uint32_t index) const { return (index < m_optionCount) ? m_options[index].value : nullptr; }

bool CommandLine::Overflowed() const { return m_overflowed; }

void CommandLine::PrintHelp(const CommandLineHelpEntry* entries, uint32_t entryCount) const
{
    Engine_LogInfo("Usage: <executable> [options]");
    if (entries && entryCount > 0)
    {
        Engine_LogInfo("Options:");
        for (uint32_t i = 0; i < entryCount; ++i)
        {
            if (!entries[i].name)
                continue;
            Engine_LogInfo("  --%-14s %-18s %s", entries[i].name, entries[i].argument ? entries[i].argument : "", entries[i].description ? entries[i].description : "");
        }
    }

    if (m_optionCount > 0)
    {
        Engine_LogInfo("Parsed options:");
        for (uint32_t i = 0; i < m_optionCount; ++i)
            Engine_LogInfo("  --%.*s = %s", static_cast<int>(m_options[i].nameLength), m_options[i].name, m_options[i].value);
    }
}

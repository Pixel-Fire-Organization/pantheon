#pragma once

#include <cstdint>

// Capacity of one parsed command line. Overflow is reported, never silent.
#define CMD_MAX_OPTIONS 32
#define CMD_MAX_POSITIONALS 8

// One row of a name -> integer mapping handed to GetEnum. Callers build these
// as static tables next to the enum they decode.
struct CommandLineEnumEntry
{
    const char* name;
    int32_t value;
};

// One row of the --help listing.
struct CommandLineHelpEntry
{
    const char* name;
    const char* argument; // e.g. "<pal|ntsc>"; null for a flag
    const char* description;
};

class CommandLine final
{
public:
    CommandLine();

    CommandLine(const CommandLine&) = delete;
    CommandLine(CommandLine&&) = delete;
    CommandLine& operator=(const CommandLine&) = delete;
    CommandLine& operator=(CommandLine&&) = delete;

    // Parse argv into the option/positional tables. Returns false if either
    // table overflowed; the successfully parsed prefix is still usable.
    bool Parse(int argc, char** argv);

    // --- Queries ------------------------------------------------------------
    bool HasOption(const char* name) const;

    // Returns the option's value, or `fallback` when absent. A flag written
    // without a value (--verbose) reads back as "1".
    const char* GetString(const char* name, const char* fallback) const;

    int32_t GetInt(const char* name, int32_t fallback) const;
    float GetFloat(const char* name, float fallback) const;

    // Accepts 1/0, true/false, yes/no, on/off (case-insensitive).
    bool GetBool(const char* name, bool fallback) const;

    // Map an option's value onto an integer via `table`. On an unrecognised
    // value logs an actionable error naming every accepted value, then returns
    // `fallback` - the engine's "crash loudly, say what is valid" contract.
    int32_t GetEnum(const char* name, const CommandLineEnumEntry* table, uint32_t tableCount, int32_t fallback) const;

    // --- Positionals --------------------------------------------------------
    uint32_t GetPositionalCount() const;
    const char* GetPositional(uint32_t index) const; // null when out of range

    // --- Introspection ------------------------------------------------------
    uint32_t GetOptionCount() const;
    const char* GetOptionName(uint32_t index) const; // NOT null-terminated at
    uint32_t GetOptionNameLength(uint32_t index) const; // the name's end - use both
    const char* GetOptionValue(uint32_t index) const;

    bool Overflowed() const;

    // Print `entries` plus the parsed positionals to the log. The caller owns
    // the table so each subsystem documents only its own options.
    void PrintHelp(const CommandLineHelpEntry* entries, uint32_t entryCount) const;

private:
    // Points into argv. `name` is not null-terminated at the name boundary when
    // the option was written --name=value, hence the explicit length.
    struct Option
    {
        const char* name;
        const char* value;
        uint32_t nameLength;
    };

    const Option* Find(const char* name) const;
    bool AddOption(const char* name, uint32_t nameLength, const char* value);
    bool AddPositional(const char* value);

    Option m_options[CMD_MAX_OPTIONS];
    const char* m_positionals[CMD_MAX_POSITIONALS];
    uint32_t m_optionCount;
    uint32_t m_positionalCount;
    bool m_overflowed;
};

#include <unity.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "radian_parser.h"

struct Fixture
{
    std::string name;
    std::vector<uint8_t> decoded;
    uint32_t expected_volume;
    uint32_t expected_battery;
    uint32_t expected_counter;
    uint32_t expected_time_start;
    uint32_t expected_time_end;
    bool expected_history_available;
    bool expected_crc_valid;
};

static std::string trim(const std::string &s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
    {
        start++;
    }

    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
    {
        end--;
    }

    return s.substr(start, end - start);
}

static std::vector<std::string> split(const std::string &s, char delim)
{
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, delim))
    {
        out.push_back(part);
    }
    return out;
}

static bool parse_hex_bytes(const std::string &hex_string, std::vector<uint8_t> &out)
{
    out.clear();
    std::stringstream ss(hex_string);
    std::string tok;

    while (ss >> tok)
    {
        if (tok.size() != 2)
        {
            return false;
        }
        char *end = nullptr;
        long value = std::strtol(tok.c_str(), &end, 16);
        if (!(end && *end == '\0' && value >= 0 && value <= 255))
        {
            return false;
        }
        out.push_back(static_cast<uint8_t>(value));
    }

    return !out.empty();
}

static bool parse_u32_field(const std::string &input, uint32_t &out)
{
    const std::string cleaned = trim(input);
    if (cleaned.empty())
    {
        return false;
    }

    errno = 0;
    char *end = nullptr;
    unsigned long value = std::strtoul(cleaned.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0')
    {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

static std::ifstream open_fixture_list()
{
    const char *candidates[] = {
        "test/fixtures/meter_frames/fixtures.lst",
        "../test/fixtures/meter_frames/fixtures.lst",
        "../../test/fixtures/meter_frames/fixtures.lst",
        "../../../test/fixtures/meter_frames/fixtures.lst",
    };

    for (const char *path : candidates)
    {
        std::ifstream file(path);
        if (file.good())
        {
            return file;
        }
    }

    return std::ifstream();
}

struct FixtureLoadResult
{
    std::vector<Fixture> fixtures;
    bool fixture_file_found;
    size_t data_line_count;
    size_t parse_error_count;
};

static FixtureLoadResult load_fixtures()
{
    FixtureLoadResult result{};
    std::ifstream in = open_fixture_list();
    if (!in.good())
    {
        result.fixture_file_found = false;
        return result;
    }
    result.fixture_file_found = true;

    std::string line;
    size_t line_number = 0;
    while (std::getline(in, line))
    {
        line_number++;
        line = trim(line);
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        result.data_line_count++;

        std::vector<std::string> parts = split(line, '|');
        if (parts.size() != 9)
        {
            result.parse_error_count++;
            continue;
        }

        Fixture fx;
        fx.name = trim(parts[0]);
        uint32_t history_u32 = 0;
        uint32_t crc_u32 = 0;
        bool ok = true;
        ok = ok && parse_hex_bytes(parts[1], fx.decoded);
        ok = ok && parse_u32_field(parts[2], fx.expected_volume);
        ok = ok && parse_u32_field(parts[3], fx.expected_battery);
        ok = ok && parse_u32_field(parts[4], fx.expected_counter);
        ok = ok && parse_u32_field(parts[5], fx.expected_time_start);
        ok = ok && parse_u32_field(parts[6], fx.expected_time_end);
        ok = ok && parse_u32_field(parts[7], history_u32);
        ok = ok && parse_u32_field(parts[8], crc_u32);

        if (!ok)
        {
            (void)line_number;
            result.parse_error_count++;
            continue;
        }

        fx.expected_history_available = history_u32 != 0;
        fx.expected_crc_valid = crc_u32 != 0;
        result.fixtures.push_back(fx);
    }

    return result;
}

void setUp(void) {}

void tearDown(void) {}

void test_replay_meter_fixtures(void)
{
    FixtureLoadResult loaded = load_fixtures();

    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        0,
        static_cast<uint32_t>(loaded.parse_error_count),
        "Malformed fixture line(s) found in fixtures.lst");

    std::vector<Fixture> fixtures = loaded.fixtures;

    if (!loaded.fixture_file_found || fixtures.empty())
    {
        TEST_PASS_MESSAGE("No meter fixtures present yet. Capture frames and append fixtures.lst.");
        return;
    }

    for (const Fixture &fx : fixtures)
    {
        TEST_ASSERT_TRUE_MESSAGE(!fx.decoded.empty(), fx.name.c_str());

        bool crc_ok = radian_validate_crc(fx.decoded.data(), fx.decoded.size());
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            fx.expected_crc_valid ? 1 : 0,
            crc_ok ? 1 : 0,
            fx.name.c_str());

        if (!crc_ok)
        {
            continue;
        }

        struct radian_primary_data parsed;
        bool parsed_ok = radian_parse_primary_data(fx.decoded.data(), fx.decoded.size(), &parsed);
        TEST_ASSERT_TRUE_MESSAGE(parsed_ok, fx.name.c_str());

        TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.expected_volume, parsed.volume, fx.name.c_str());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.expected_battery, parsed.battery_left, fx.name.c_str());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.expected_counter, parsed.reads_counter, fx.name.c_str());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.expected_time_start, parsed.time_start, fx.name.c_str());
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(fx.expected_time_end, parsed.time_end, fx.name.c_str());
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            fx.expected_history_available ? 1 : 0,
            parsed.history_available ? 1 : 0,
            fx.name.c_str());
    }
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_replay_meter_fixtures);
    return UNITY_END();
}

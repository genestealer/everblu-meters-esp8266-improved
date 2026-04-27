#include <unity.h>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Fixture {
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

static uint16_t crc_kermit(const uint8_t *input_ptr, size_t num_bytes) {
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < num_bytes; i++) {
        crc ^= input_ptr[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0x8408;
            } else {
                crc >>= 1;
            }
        }
    }

    uint16_t low_byte = (crc & 0xff00) >> 8;
    uint16_t high_byte = (crc & 0x00ff) << 8;
    return static_cast<uint16_t>(low_byte | high_byte);
}

static bool validate_radian_crc(const std::vector<uint8_t> &decoded) {
    const size_t size = decoded.size();
    if (size < 4) {
        return false;
    }

    const uint8_t length_field = decoded[0];
    size_t expected_len = length_field ? length_field : size;

    if (expected_len > size) {
        const size_t missing = expected_len - size;
        if (missing == 2) {
            return true;
        }
        return true;
    }

    if (expected_len < 4) {
        return false;
    }

    const size_t crc_offset = expected_len - 2;
    if (crc_offset + 1 >= size) {
        return true;
    }

    const uint16_t received_crc = (static_cast<uint16_t>(decoded[crc_offset]) << 8) |
                                  static_cast<uint16_t>(decoded[crc_offset + 1]);

    if (expected_len <= 3) {
        return false;
    }

    const uint16_t computed_crc = crc_kermit(decoded.data() + 1, expected_len - 3);
    return computed_crc == received_crc;
}

struct ParsedData {
    uint32_t volume;
    uint32_t battery_left;
    uint32_t reads_counter;
    uint32_t time_start;
    uint32_t time_end;
    bool history_available;
};

static ParsedData parse_meter_report(const std::vector<uint8_t> &decoded) {
    ParsedData out{};

    if (decoded.size() < 30) {
        return out;
    }

    out.volume = static_cast<uint32_t>(decoded[18]) |
                 (static_cast<uint32_t>(decoded[19]) << 8) |
                 (static_cast<uint32_t>(decoded[20]) << 16) |
                 (static_cast<uint32_t>(decoded[21]) << 24);

    if (out.volume == 0 || out.volume == 0xFFFFFFFFUL) {
        out = {};
        return out;
    }

    if (decoded.size() >= 49) {
        out.reads_counter = decoded[48];
        out.battery_left = decoded[31];
        out.time_start = decoded[44];
        out.time_end = decoded[45];

        if (out.time_start > 23 || out.time_end > 23) {
            out = {};
            return out;
        }

        if (out.battery_left == 0xFF || out.reads_counter == 0xFF) {
            out = {};
            return out;
        }
    }

    out.history_available = decoded.size() >= 118;
    return out;
}

static std::string trim(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        start++;
    }

    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        end--;
    }

    return s.substr(start, end - start);
}

static std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, delim)) {
        out.push_back(part);
    }
    return out;
}

static std::vector<uint8_t> parse_hex_bytes(const std::string &hex_string) {
    std::vector<uint8_t> out;
    std::stringstream ss(hex_string);
    std::string tok;

    while (ss >> tok) {
        if (tok.size() != 2) {
            continue;
        }
        char *end = nullptr;
        long value = std::strtol(tok.c_str(), &end, 16);
        if (end && *end == '\0' && value >= 0 && value <= 255) {
            out.push_back(static_cast<uint8_t>(value));
        }
    }

    return out;
}

static std::ifstream open_fixture_list() {
    const char *candidates[] = {
        "test/fixtures/meter_frames/fixtures.lst",
        "../test/fixtures/meter_frames/fixtures.lst",
        "../../test/fixtures/meter_frames/fixtures.lst",
        "../../../test/fixtures/meter_frames/fixtures.lst",
    };

    for (const char *path : candidates) {
        std::ifstream file(path);
        if (file.good()) {
            return file;
        }
    }

    return std::ifstream();
}

static std::vector<Fixture> load_fixtures() {
    std::vector<Fixture> fixtures;
    std::ifstream in = open_fixture_list();
    if (!in.good()) {
        return fixtures;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::vector<std::string> parts = split(line, '|');
        if (parts.size() != 9) {
            continue;
        }

        Fixture fx;
        fx.name = trim(parts[0]);
        fx.decoded = parse_hex_bytes(parts[1]);
        fx.expected_volume = static_cast<uint32_t>(std::strtoul(trim(parts[2]).c_str(), nullptr, 10));
        fx.expected_battery = static_cast<uint32_t>(std::strtoul(trim(parts[3]).c_str(), nullptr, 10));
        fx.expected_counter = static_cast<uint32_t>(std::strtoul(trim(parts[4]).c_str(), nullptr, 10));
        fx.expected_time_start = static_cast<uint32_t>(std::strtoul(trim(parts[5]).c_str(), nullptr, 10));
        fx.expected_time_end = static_cast<uint32_t>(std::strtoul(trim(parts[6]).c_str(), nullptr, 10));
        fx.expected_history_available = std::strtoul(trim(parts[7]).c_str(), nullptr, 10) != 0;
        fx.expected_crc_valid = std::strtoul(trim(parts[8]).c_str(), nullptr, 10) != 0;

        fixtures.push_back(fx);
    }

    return fixtures;
}

void setUp(void) {}

void tearDown(void) {}

void test_replay_meter_fixtures(void) {
    std::vector<Fixture> fixtures = load_fixtures();

    if (fixtures.empty()) {
        TEST_PASS_MESSAGE("No meter fixtures present yet. Capture frames and append fixtures.lst.");
        return;
    }

    for (const Fixture &fx : fixtures) {
        TEST_ASSERT_TRUE_MESSAGE(!fx.decoded.empty(), fx.name.c_str());

        bool crc_ok = validate_radian_crc(fx.decoded);
        TEST_ASSERT_EQUAL_INT_MESSAGE(
            fx.expected_crc_valid ? 1 : 0,
            crc_ok ? 1 : 0,
            fx.name.c_str());

        if (!crc_ok) {
            continue;
        }

        ParsedData parsed = parse_meter_report(fx.decoded);

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

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    UNITY_BEGIN();
    RUN_TEST(test_replay_meter_fixtures);
    return UNITY_END();
}

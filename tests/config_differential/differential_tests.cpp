// The differential test for the config conversion.
//
// Oracle: the reader of the newest published build (the rolling dev pre-release,
// c91277b, core f92be69), vendored under oracle/published/ byte for byte, with the
// startup code that turned its output into the session's state.
// Import: the frozen reader in src/legacy_config/ and its map into Config, the
// owner's LegacyImport, through the mod's startup code.
// Migration: the config owner in a scratch folder holding only the file as
// HeadTracking.ini, which it imports into a new CameraUnlock.ini, then the
// canonical reader and table on that file. Every owner reads a scratch
// Defaults.ini the first load creates with the built-in values, so a row whose
// imported value is the built-in one migrates as `default`.
//
// Every input runs through all three: the published build's first-run file, no
// file, an empty file and the corpus core generates from the first-run file.
//
// Comparison 1, oracle against import, compares every field the published Config
// and the frozen one share, floats bit for bit, the startup state and the
// registered hotkey bindings. The one difference it finds is normalisation N3: a
// hotkey code on a Ctrl, Shift or Alt key alone is not bound, and the action
// keeps its Ctrl+Shift chord. No commit since c91277b changed how the file is
// read.
//
// Comparison 2, import against migration, compares every field of Config and the
// startup state, and finds no difference. The only value the conversion drops is
// a hotkey on a Ctrl, Shift or Alt key alone (N3, ModifierKey), which the import
// reports and the owner logs. No moved default: every Config default is the value
// the published build shipped, so no file converts to the published defaults too.
//
// A row the player never changed from what the published build shipped follows
// Defaults.ini (owner rule of 2026-09-26): the import lists it in
// follows_defaults_ini, the tracking mode pair as one unit. The test derives
// that list from what the frozen reader read against the frozen defaults and
// holds the import's list to it on every input. Every present input also
// migrates over a Defaults.ini that differs from the built-in value on every
// global row: an untouched row is written `default` and takes that file's
// value, and a changed row keeps the player's.
//
// The published build never refuses a file, so no input is refused.
//
// After every load HeadTracking.ini keeps its bytes and its last write time, and
// the folder holds it and CameraUnlock.ini and nothing else. Every input is also
// migrated from a read-only HeadTracking.ini, which has to give the same file
// and keep its read-only attribute. A second load reads CameraUnlock.ini, does
// not import, and changes neither file.
//
// The distinct migrated files are written beside the executable under
// migrated\, for lint-migrated.mjs to run core's canonical config lint over.
//
// What is recorded here, and checked by hash below:
//   - The published builds: only the dev pre-release, at c91277b. No v* tag and no
//     predecessor repo exists.
//   - oracle/published/src: c91277b:src/{config.h,config.cpp,logging.h}.
//   - oracle/published/core: the core sources those include, at f92be69, the pin
//     c91277b built against.
//   - The frozen import: src/legacy_config/{legacy_config.h,legacy_config.cpp}. It
//     compiles core's IniReader, which hashes equal to f92be69's; legacy_import.h
//     gives it only the LegacyKey type, and file_log only its log lines.
//   - inputs/: the first-run file of the published build. No build shipped a
//     config file in its ZIP or launcher seed; the mod wrote one at first launch.

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <cameraunlock/config/canonical_ini.h>
#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/config/testing/ini_mutations.h>
#include <cameraunlock/input/key_bindings.h>

#include "config.h"
#include "hotkey_bindings.h"
#include "legacy_config/legacy_config.h"
#include "oracle/oracle.h"

namespace {

namespace fs = std::filesystem;
using cameraunlock::config::testing::GenerateIniMutations;
using cameraunlock::config::testing::IniMutation;
using cameraunlock::config::testing::MutationKey;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

int g_failures = 0;

void Check(bool condition, const std::string& name) {
    std::cout << (condition ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!condition) ++g_failures;
}

// A failure in a loop over the corpus, printed once per input.
void Fail(const std::string& name) {
    std::cout << "  [FAIL] " << name << "\n";
    ++g_failures;
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << bytes;
}

std::string Sha256(const std::string& bytes) {
    unsigned char digest[32] = {};
    const NTSTATUS status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                                       reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                       static_cast<ULONG>(bytes.size()), digest, sizeof(digest));
    if (status != 0) throw std::runtime_error("BCryptHash failed");
    std::string hex;
    char two[3];
    for (const unsigned char b : digest) {
        std::snprintf(two, sizeof(two), "%02x", b);
        hex += two;
    }
    return hex;
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

FILETIME WriteTime(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        throw std::runtime_error("cannot stat " + path.string());
    return data.ftLastWriteTime;
}

bool SameTime(const FILETIME& a, const FILETIME& b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

bool IsReadOnly(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("cannot stat " + path.string());
    return (attributes & FILE_ATTRIBUTE_READONLY) != 0;
}

// A fresh folder under %TEMP% for the whole run, removed at the end.
class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() /
                ("kcd-config-differential-" + std::to_string(GetCurrentProcessId()));
        Clear();
        fs::create_directories(root_);
    }
    ~Scratch() { Clear(); }

    // A new empty folder, never reused within the run.
    fs::path Folder(const char* kind) {
        const fs::path dir = root_ / (std::string(kind) + "-" + std::to_string(next_++));
        fs::create_directories(dir);
        return dir;
    }

    // The run's one Defaults.ini, outside every game folder. The first owner
    // creates it with the built-in values and every later one reads it.
    cameraunlock::config::DefaultsFile Defaults() const {
        return cameraunlock::config::DefaultsFile::At((root_ / "global" / "Defaults.ini").wstring());
    }

    // A second Defaults.ini, which SkewedDefaultsTests writes from the built-in
    // one with every global row changed.
    cameraunlock::config::DefaultsFile Skewed() const {
        return cameraunlock::config::DefaultsFile::At(SkewedPath().wstring());
    }
    fs::path BuiltinPath() const { return root_ / "global" / "Defaults.ini"; }
    fs::path SkewedPath() const { return root_ / "skewed" / "Defaults.ini"; }

private:
    void Clear() {
        if (!fs::exists(root_)) return;
        for (const auto& entry : fs::recursive_directory_iterator(root_))
            SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        fs::remove_all(root_);
    }

    fs::path root_;
    int next_ = 0;
};

struct Input {
    std::string name;
    bool present = true;
    std::string bytes;
};

// Every key the frozen reader reads, with a valid value other than the shipped
// one and, for each range it clamps or refuses, values on both sides of it.
std::vector<MutationKey> Descriptors() {
    const auto k = [](const char* section, const char* key, const char* alternate,
                      std::vector<std::string> out_of_range, bool hotkey = false) {
        MutationKey d;
        d.section = section;
        d.key = key;
        d.alternate = alternate;
        d.out_of_range = std::move(out_of_range);
        d.hotkey = hotkey;
        return d;
    };
    return {
        k("HeadTracking", "UdpPort", "5252", {"1023", "65536"}),
        k("HeadTracking", "EnableOnStartup", "false", {}),
        k("HeadTracking", "WorldSpaceYaw", "false", {}),
        k("HeadTracking", "LocalSmoothing", "0.25", {"-0.5", "1.5"}),
        k("HeadTracking", "RemoteSmoothing", "0.5", {"-1", "2"}),
        k("HeadTracking", "MaxExtrapolationFraction", "0.25", {"-0.1", "1.1"}),
        k("Camera", "FieldOfView", "90", {"39", "121"}),
        k("Position", "Enabled", "false", {}),
        k("Position", "LimitX", "0.25", {"0.001", "6"}),
        k("Position", "LimitY", "0.3", {"0.001", "6"}),
        k("Position", "LimitYDown", "0.1", {"0.001", "6"}),
        k("Position", "LimitZ", "0.5", {"0.001", "6"}),
        k("Position", "LimitZBack", "0.05", {"0.001", "6"}),
        k("Hotkeys", "ToggleKey", "0x70", {"0x06", "0xFF"}, true),
        k("Hotkeys", "PositionKey", "0x71", {"0x06", "0xFF"}, true),
        k("Hotkeys", "YawModeKey", "0x72", {"0x06", "0xFF"}, true),
    };
}

const fs::path kDir = KCD_DIFFERENTIAL_DIR;
const fs::path kRepo = KCD_REPO_DIR;

std::string PublishedFirstRun() { return ReadBytes(kDir / "inputs" / "first-run-dev-c91277b.ini"); }

// The Ctrl, Shift and Alt virtual-key codes N3 unbinds.
constexpr int kModifierCodes[] = {0x10, 0x11, 0x12, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};

bool IsModifierCode(int code) {
    return std::find(std::begin(kModifierCodes), std::end(kModifierCodes), code) != std::end(kModifierCodes);
}

std::string Replaced(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) throw std::runtime_error("the text has no " + from);
    return text.replace(at, from.size(), to);
}

std::vector<Input> Inputs() {
    std::vector<Input> inputs;
    inputs.push_back({"first run of the published build (dev, c91277b)", true, PublishedFirstRun()});
    inputs.push_back({"no file", false, {}});
    inputs.push_back({"empty file", true, {}});
    for (IniMutation& m : GenerateIniMutations(PublishedFirstRun(), kcd_ht::legacy::Keys(), Descriptors()))
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    // The corpus's hotkey alternates are plain keys, so N3 gets its own inputs.
    for (const std::string line : {"ToggleKey=0x23", "PositionKey=0x21", "YawModeKey=0x22"}) {
        const std::string key = line.substr(0, line.find('='));
        for (const int code : kModifierCodes) {
            char value[8];
            std::snprintf(value, sizeof(value), "0x%02X", code);
            inputs.push_back({"modifier key: " + key + "=" + value, true,
                              Replaced(PublishedFirstRun(), line, key + "=" + value)});
        }
    }
    return inputs;
}

// What both builds can be compared on: every field their Config types share,
// and the startup state.
struct Observed {
    int udp_port = 0;
    bool enable_on_startup = false;
    bool world_space_yaw = false;
    float local_smoothing = 0.0f;
    float remote_smoothing = 0.0f;
    float max_extrapolation_fraction = 0.0f;
    float field_of_view = 0.0f;
    bool position_enabled = false;
    float limit_x = 0.0f;
    float limit_y = 0.0f;
    float limit_y_down = 0.0f;
    float limit_z = 0.0f;
    float limit_z_back = 0.0f;
    int toggle_key = 0;
    int position_key = 0;
    int yaw_mode_key = 0;

    bool tracking_enabled = false;
    int tracking_mode = 0;
    bool world_yaw = false;
    std::vector<KeyBinding> toggle;
    std::vector<KeyBinding> cycle_tracking_mode;
    std::vector<KeyBinding> yaw_mode;
};

std::vector<KeyBinding> Bindings(const std::vector<kcd_config_oracle::Binding>& published) {
    std::vector<KeyBinding> out;
    for (const auto& b : published) out.push_back({static_cast<KeyModifiers>(b.modifiers), b.vk});
    return out;
}

Observed FromOracle(const kcd_config_oracle::Result& r) {
    Observed o;
    o.udp_port = r.udp_port;
    o.enable_on_startup = r.enable_on_startup;
    o.world_space_yaw = r.world_space_yaw;
    o.local_smoothing = r.local_smoothing;
    o.remote_smoothing = r.remote_smoothing;
    o.max_extrapolation_fraction = r.max_extrapolation_fraction;
    o.field_of_view = r.field_of_view;
    o.position_enabled = r.position_enabled;
    o.limit_x = r.limit_x;
    o.limit_y = r.limit_y;
    o.limit_y_down = r.limit_y_down;
    o.limit_z = r.limit_z;
    o.limit_z_back = r.limit_z_back;
    o.toggle_key = r.toggle_key;
    o.position_key = r.position_key;
    o.yaw_mode_key = r.yaw_mode_key;
    o.tracking_enabled = r.tracking_enabled;
    o.tracking_mode = r.tracking_mode;
    o.world_yaw = r.world_yaw;
    o.toggle = Bindings(r.toggle);
    o.cycle_tracking_mode = Bindings(r.cycle_tracking_mode);
    o.yaw_mode = Bindings(r.yaw_mode);
    return o;
}

// The import as the owner runs it on <file>, starting from the table's defaults,
// and the frozen struct its reader filled.
struct Imported {
    cameraunlock::config::ImportResult result;
    kcd_ht::legacy::Config frozen;
    kcd_ht::Config config;
};

Imported RunImport(const fs::path& file) {
    Imported i{cameraunlock::config::ImportResult::Imported({}), {}, kcd_ht::ConfigTableFor().defaults()};
    kcd_ht::legacy::LoadConfig(file.parent_path().string(), i.frozen);
    cameraunlock::config::LegacyInput input;
    input.path = file.wstring();
    input.ansi_path = file.string();
    i.result = kcd_ht::LegacyImportFor().run(input, i.config);
    return i;
}

// Startup as the mod's own startup code derives it from a Config.
void Startup(const kcd_ht::Config& c, Observed& o) {
    o.tracking_enabled = c.enable_on_startup;
    o.tracking_mode = static_cast<int>(kcd_ht::StartupMode(c));
    o.world_yaw = c.world_space_yaw;
    const kcd_ht::HotkeyBindings bindings = kcd_ht::BindingsFor(c);
    o.toggle = bindings.toggle;
    o.cycle_tracking_mode = bindings.cycle_tracking_mode;
    o.yaw_mode = bindings.yaw_mode;
}

// Every setting the runtime Config still has comes from the import's map, so a
// field the map mis-copies differs from the published build. Only what Config no
// longer holds comes from the frozen reader: the hex hotkey codes, whose lists
// the registered bindings compare.
Observed FromImport(const Imported& i) {
    const kcd_ht::Config& c = i.config;
    const kcd_ht::legacy::Config& f = i.frozen;
    Observed o;
    o.udp_port = c.udp_port;
    o.enable_on_startup = c.enable_on_startup;
    o.world_space_yaw = c.world_space_yaw;
    o.local_smoothing = c.local_smoothing;
    o.remote_smoothing = c.remote_smoothing;
    o.max_extrapolation_fraction = c.max_extrapolation_fraction;
    o.field_of_view = c.field_of_view;
    o.position_enabled = c.position_enabled;
    o.limit_x = c.limit_x;
    o.limit_y = c.limit_y;
    o.limit_y_down = c.limit_y_down;
    o.limit_z = c.limit_z;
    o.limit_z_back = c.limit_z_back;
    o.toggle_key = f.toggle_key;
    o.position_key = f.position_key;
    o.yaw_mode_key = f.yaw_mode_key;
    Startup(c, o);
    return o;
}

std::vector<std::string> Differences(const Observed& a, const Observed& b) {
    std::vector<std::string> d;
    const auto field = [&d](bool same, const char* name) {
        if (!same) d.push_back(name);
    };
    field(a.udp_port == b.udp_port, "udp_port");
    field(a.enable_on_startup == b.enable_on_startup, "enable_on_startup");
    field(a.world_space_yaw == b.world_space_yaw, "world_space_yaw");
    field(SameBits(a.local_smoothing, b.local_smoothing), "local_smoothing");
    field(SameBits(a.remote_smoothing, b.remote_smoothing), "remote_smoothing");
    field(SameBits(a.max_extrapolation_fraction, b.max_extrapolation_fraction), "max_extrapolation_fraction");
    field(SameBits(a.field_of_view, b.field_of_view), "field_of_view");
    field(a.position_enabled == b.position_enabled, "position_enabled");
    field(SameBits(a.limit_x, b.limit_x), "limit_x");
    field(SameBits(a.limit_y, b.limit_y), "limit_y");
    field(SameBits(a.limit_y_down, b.limit_y_down), "limit_y_down");
    field(SameBits(a.limit_z, b.limit_z), "limit_z");
    field(SameBits(a.limit_z_back, b.limit_z_back), "limit_z_back");
    field(a.toggle_key == b.toggle_key, "toggle_key");
    field(a.position_key == b.position_key, "position_key");
    field(a.yaw_mode_key == b.yaw_mode_key, "yaw_mode_key");
    field(a.tracking_enabled == b.tracking_enabled, "startup: tracking enabled");
    field(a.tracking_mode == b.tracking_mode, "startup: tracking mode");
    field(a.world_yaw == b.world_yaw, "startup: yaw mode");
    field(a.toggle == b.toggle, "hotkeys: toggle");
    field(a.cycle_tracking_mode == b.cycle_tracking_mode, "hotkeys: cycle tracking mode");
    field(a.yaw_mode == b.yaw_mode, "hotkeys: yaw mode");
    return d;
}

// Every field of the runtime Config.
std::vector<std::string> ConfigDifferences(const kcd_ht::Config& a, const kcd_ht::Config& b) {
    std::vector<std::string> d;
    const auto field = [&d](bool same, const char* name) {
        if (!same) d.push_back(name);
    };
    field(a.udp_port == b.udp_port, "udp_port");
    field(a.enable_on_startup == b.enable_on_startup, "enable_on_startup");
    field(a.world_space_yaw == b.world_space_yaw, "world_space_yaw");
    field(a.rotation_enabled == b.rotation_enabled, "rotation_enabled");
    field(a.position_enabled == b.position_enabled, "position_enabled");
    field(SameBits(a.local_smoothing, b.local_smoothing), "local_smoothing");
    field(SameBits(a.remote_smoothing, b.remote_smoothing), "remote_smoothing");
    field(SameBits(a.max_extrapolation_fraction, b.max_extrapolation_fraction), "max_extrapolation_fraction");
    field(SameBits(a.field_of_view, b.field_of_view), "field_of_view");
    field(SameBits(a.limit_x, b.limit_x), "limit_x");
    field(SameBits(a.limit_y, b.limit_y), "limit_y");
    field(SameBits(a.limit_y_down, b.limit_y_down), "limit_y_down");
    field(SameBits(a.limit_z, b.limit_z), "limit_z");
    field(SameBits(a.limit_z_back, b.limit_z_back), "limit_z_back");
    field(a.toggle_key == b.toggle_key, "toggle_key");
    field(a.cycle_tracking_mode_key == b.cycle_tracking_mode_key, "cycle_tracking_mode_key");
    field(a.yaw_mode_key == b.yaw_mode_key, "yaw_mode_key");
    return d;
}

std::vector<fs::path> Listing(const fs::path& dir) {
    std::vector<fs::path> names;
    for (const auto& entry : fs::directory_iterator(dir)) names.push_back(entry.path().filename());
    return names;
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines)
        if (line.find(text) != std::string::npos) return true;
    return false;
}

using cameraunlock::config::schema::Concept;

// The rows the import must leave to Defaults.ini: every one whose legacy value is
// what the published build shipped, derived here from the frozen reader's output
// and its own defaults, the tracking mode as one unit.
std::set<Concept> UntouchedRows(const kcd_ht::legacy::Config& read) {
    const kcd_ht::legacy::Config shipped;
    std::set<Concept> rows;
    const auto row = [&rows](bool unchanged, Concept id) {
        if (unchanged) rows.insert(id);
    };
    row(read.udp_port == shipped.udp_port, Concept::UdpPort);
    row(read.enable_on_startup == shipped.enable_on_startup, Concept::EnableOnStartup);
    row(read.world_space_yaw == shipped.world_space_yaw, Concept::WorldSpaceYaw);
    row(read.position_enabled == shipped.position_enabled, Concept::RotationEnabled);
    row(read.position_enabled == shipped.position_enabled, Concept::PositionEnabled);
    row(read.local_smoothing == shipped.local_smoothing, Concept::LocalSmoothing);
    row(read.remote_smoothing == shipped.remote_smoothing, Concept::RemoteSmoothing);
    row(read.limit_x == shipped.limit_x, Concept::PositionLimitX);
    row(read.limit_y == shipped.limit_y, Concept::PositionLimitY);
    row(read.limit_y_down == shipped.limit_y_down, Concept::PositionLimitYDown);
    row(read.limit_z == shipped.limit_z, Concept::PositionLimitZ);
    row(read.limit_z_back == shipped.limit_z_back, Concept::PositionLimitZBack);
    row(read.toggle_key == shipped.toggle_key, Concept::ToggleKey);
    row(read.position_key == shipped.position_key, Concept::CycleTrackingModeKey);
    row(read.yaw_mode_key == shipped.yaw_mode_key, Concept::YawModeKey);
    return rows;
}

// Every global row of the table: the rows a file nobody changed leaves to
// Defaults.ini.
const std::set<Concept> kAllRows = {
    Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
    Concept::PositionEnabled, Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::PositionLimitX,
    Concept::PositionLimitY, Concept::PositionLimitYDown, Concept::PositionLimitZ, Concept::PositionLimitZBack,
    Concept::ToggleKey, Concept::CycleTrackingModeKey, Concept::YawModeKey,
};

// A Defaults.ini value other than the built-in one on every global row, as the
// built-in file spells each line and as SkewedConfig() reads the new value.
struct SkewedLine {
    const char* builtin;
    const char* skewed;
};
const SkewedLine kSkewedLines[] = {
    {"UdpPort=4242", "UdpPort=4343"},
    {"EnableOnStartup=true", "EnableOnStartup=false"},
    {"WorldSpaceYaw=true", "WorldSpaceYaw=false"},
    {"RotationEnabled=true", "RotationEnabled=false"},
    {"LocalSmoothing=0.0", "LocalSmoothing=0.3"},
    {"RemoteSmoothing=0.15", "RemoteSmoothing=0.4"},
    {"PositionLimitX=0.3", "PositionLimitX=0.25"},
    {"PositionLimitY=0.2", "PositionLimitY=0.15"},
    {"PositionLimitYDown=0.2", "PositionLimitYDown=0.12"},
    {"PositionLimitZ=0.4", "PositionLimitZ=0.35"},
    {"PositionLimitZBack=0.1", "PositionLimitZBack=0.07"},
    {"ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=F9"},
    {"CycleTrackingModeKey=PageUp, Ctrl+Shift+G", "CycleTrackingModeKey=F10"},
    {"YawModeKey=PageDown, Ctrl+Shift+H", "YawModeKey=F11"},
};

// The settings a file of nothing but `default` runs on over the skewed
// Defaults.ini. The tracking mode there is position only.
kcd_ht::Config SkewedConfig() {
    kcd_ht::Config c = kcd_ht::ConfigTableFor().defaults();
    c.udp_port = 4343;
    c.enable_on_startup = false;
    c.world_space_yaw = false;
    c.rotation_enabled = false;
    c.position_enabled = true;
    c.local_smoothing = 0.3f;
    c.remote_smoothing = 0.4f;
    c.limit_x = 0.25f;
    c.limit_y = 0.15f;
    c.limit_y_down = 0.12f;
    c.limit_z = 0.35f;
    c.limit_z_back = 0.07f;
    c.toggle_key = "F9";
    c.cycle_tracking_mode_key = "F10";
    c.yaw_mode_key = "F11";
    return c;
}

// @p c with every row in @p follows taken from @p over.
kcd_ht::Config OverDefaults(kcd_ht::Config c, const std::set<Concept>& follows, const kcd_ht::Config& over) {
    const auto take = [&follows](Concept id) { return follows.count(id) != 0; };
    if (take(Concept::UdpPort)) c.udp_port = over.udp_port;
    if (take(Concept::EnableOnStartup)) c.enable_on_startup = over.enable_on_startup;
    if (take(Concept::WorldSpaceYaw)) c.world_space_yaw = over.world_space_yaw;
    if (take(Concept::RotationEnabled)) c.rotation_enabled = over.rotation_enabled;
    if (take(Concept::PositionEnabled)) c.position_enabled = over.position_enabled;
    if (take(Concept::LocalSmoothing)) c.local_smoothing = over.local_smoothing;
    if (take(Concept::RemoteSmoothing)) c.remote_smoothing = over.remote_smoothing;
    if (take(Concept::PositionLimitX)) c.limit_x = over.limit_x;
    if (take(Concept::PositionLimitY)) c.limit_y = over.limit_y;
    if (take(Concept::PositionLimitYDown)) c.limit_y_down = over.limit_y_down;
    if (take(Concept::PositionLimitZ)) c.limit_z = over.limit_z;
    if (take(Concept::PositionLimitZBack)) c.limit_z_back = over.limit_z_back;
    if (take(Concept::ToggleKey)) c.toggle_key = over.toggle_key;
    if (take(Concept::CycleTrackingModeKey)) c.cycle_tracking_mode_key = over.cycle_tracking_mode_key;
    if (take(Concept::YawModeKey)) c.yaw_mode_key = over.yaw_mode_key;
    return c;
}

std::string ConceptKey(Concept id) {
    return cameraunlock::config::schema::kConcepts[static_cast<std::size_t>(id)].key;
}

// The oracle is the published source and the core sources it compiled against,
// unchanged; the import compiles core's IniReader, which is identical to the one
// the published build compiled.
void FrozenSourceTests() {
    std::cout << "Frozen sources\n";
    struct Recorded {
        fs::path path;
        const char* sha256;
    };
    const fs::path published = kDir / "oracle" / "published";
    const fs::path core = kRepo / "cameraunlock-core" / "cpp";
    const Recorded recorded[] = {
        {published / "src" / "config.h", "b32d75276877a93de9b744cd3a1a02b3d746efda4fe082f54e0a653b19f9f549"},
        {published / "src" / "config.cpp", "c5d3007d744a513ad1a0e1f1ca7ceaa3f877b4b9aef12ec5e434b441b7ef79f7"},
        {published / "src" / "logging.h", "4383d474c5165246e4afe7db06a59c5d2bbf87184f47219f1ad56b24a47a089e"},
        {published / "core/include/cameraunlock/config/ini_reader.h",
         "a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"},
        {published / "core/include/cameraunlock/math/finite_utils.h",
         "c59772d698d54ade3374ee1221b74f5563a86d76f0eb34a989ef7efab389c0ad"},
        {published / "core/include/cameraunlock/protocol/port_utils.h",
         "91bff564d5e279b66527ec5553afcf4d591812dab0e78f71a48ef412e4db7a44"},
        {published / "core/include/cameraunlock/data/position_settings.h",
         "b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"},
        {published / "core/include/cameraunlock/math/smoothing_utils.h",
         "fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"},
        {published / "core/include/cameraunlock/math/angle_utils.h",
         "d7a905270933e3cb0c4c361d29d3fd701655498cbcd1875ea79d180468bdbe6a"},
        {published / "core/include/cameraunlock/logging/file_log.h",
         "43bdd2ef8554c78e5f440333463750c13b95110fe672b0b6e273244df9e7d169"},
        {published / "core/src/config/ini_reader.cpp",
         "e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"},
        {published / "core/src/logging/file_log.cpp",
         "73c53c2baa06bbfebe8211f62678aa2b60cb95f604743d3686951ba56b87ea47"},
        {core / "include/cameraunlock/config/ini_reader.h",
         "a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"},
        {core / "src/config/ini_reader.cpp", "e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"},
        {kRepo / "src/legacy_config/legacy_config.h",
         "3972674a3b7f4ed29db3ca1f12eb479bd71736234ee9f72710b54f6f9adac51c"},
        {kRepo / "src/legacy_config/legacy_config.cpp",
         "ade294f40c380968f608c924cabeed64d3638b1f60f36da54ac2b3e5a79f7380"},
    };
    for (const Recorded& r : recorded)
        Check(Sha256(ReadBytes(r.path)) == r.sha256, r.path.lexically_relative(kRepo).generic_string() +
                                                         " hashes as recorded");
}

void FirstRunTests(Scratch& scratch) {
    std::cout << "Published first run\n";
    const fs::path dir = scratch.Folder("first-run");
    kcd_config_oracle::Startup(dir.string());
    Check(ReadBytes(dir / "HeadTracking.ini") == PublishedFirstRun(),
          "the published build's first run writes inputs/first-run-dev-c91277b.ini byte for byte");
}

// The legacy file as a load must leave it: its bytes, its last write time and,
// for a read-only copy, its read-only attribute.
struct LegacyState {
    std::string bytes;
    FILETIME written{};
    bool read_only = false;
};

LegacyState StateOf(const fs::path& file) { return {ReadBytes(file), WriteTime(file), IsReadOnly(file)}; }

bool Unchanged(const fs::path& file, const LegacyState& before) {
    return ReadBytes(file) == before.bytes && SameTime(WriteTime(file), before.written)
        && IsReadOnly(file) == before.read_only;
}

std::vector<fs::path> Sorted(std::vector<fs::path> names) {
    std::sort(names.begin(), names.end());
    return names;
}

struct Migrated {
    kcd_ht::Config config;
    std::string bytes;
};

// Puts <bytes> (or no file) in a fresh folder as HeadTracking.ini, read-only when
// @p readOnly, lets the mod's owner load, and checks what the load leaves behind.
// Returns the settings the session runs on and the CameraUnlock.ini it created.
Migrated Migrate(Scratch& scratch, const Input& input, const Imported& imported, bool readOnly) {
    using cameraunlock::config::ConfigLoadStatus;
    using cameraunlock::config::ConfigOwner;

    const std::string name = input.name + (readOnly ? " (read-only)" : "");
    const fs::path dir = scratch.Folder("migrate");
    const fs::path legacy = dir / "HeadTracking.ini";
    const fs::path file = dir / "CameraUnlock.ini";
    LegacyState before;
    if (input.present) {
        WriteBytes(legacy, input.bytes);
        if (readOnly) SetFileAttributesW(legacy.c_str(), FILE_ATTRIBUTE_READONLY);
        before = StateOf(legacy);
    }
    const std::vector<fs::path> expectedListing = input.present
        ? std::vector<fs::path>{"CameraUnlock.ini", "HeadTracking.ini"}
        : std::vector<fs::path>{"CameraUnlock.ini"};

    ConfigOwner<kcd_ht::Config> owner(kcd_ht::OwnerOptions(dir.wstring(), scratch.Defaults()));
    const auto loaded = owner.Load();
    const ConfigLoadStatus expected = input.present ? ConfigLoadStatus::Migrated : ConfigLoadStatus::Created;
    if (loaded.status != expected) {
        Fail(name + ": the owner's load is " + cameraunlock::config::ConfigLoadStatusName(loaded.status) +
             ", not " + cameraunlock::config::ConfigLoadStatusName(expected) + " (" + loaded.reason + ")");
        return {loaded.config, {}};
    }
    if (input.present && !Unchanged(legacy, before))
        Fail(name + ": the import changed HeadTracking.ini");
    if (Sorted(Listing(dir)) != expectedListing)
        Fail(name + ": the folder holds more than HeadTracking.ini and CameraUnlock.ini");

    const std::string migrated = ReadBytes(file);
    if (input.present) {
        for (const auto& dropped : imported.result.dropped) {
            if (!Contains(loaded.log, cameraunlock::config::DescribeDroppedValue(dropped)))
                Fail(name + ": the log does not name the dropped " + dropped.key);
        }
    }

    // The migrated bytes as the canonical reader and the table read them, over the
    // built-in values Defaults.ini holds: nothing to report, and the settings the
    // session runs on.
    const cameraunlock::config::CanonicalIni doc = cameraunlock::config::ParseCanonicalIni(migrated);
    const auto table = kcd_ht::ConfigTableFor();
    kcd_ht::Config reread = table.defaults();
    if (doc.status != cameraunlock::config::CanonicalReadStatus::Readable || !doc.diagnostics.empty()
            || !cameraunlock::config::ApplyCanonical(doc, table, reread).diagnostics.empty())
        Fail(name + ": the migrated file draws a diagnostic");
    for (const std::string& field : ConfigDifferences(reread, loaded.config))
        Fail(name + ": the migrated file reads back a different " + field);

    // A second launch reads CameraUnlock.ini, does not import, and changes neither
    // file.
    const FILETIME migratedTime = WriteTime(file);
    ConfigOwner<kcd_ht::Config> next(kcd_ht::OwnerOptions(dir.wstring(), scratch.Defaults()));
    const auto again = next.Load();
    if (again.status != ConfigLoadStatus::Canonical || !ConfigDifferences(again.config, loaded.config).empty()
            || ReadBytes(file) != migrated || !SameTime(WriteTime(file), migratedTime)
            || (input.present && !Unchanged(legacy, before)) || Sorted(Listing(dir)) != expectedListing)
        Fail(name + ": a second load did not read CameraUnlock.ini as it was and leave both files alone");
    if (input.present && !Contains(again.log, "is left as it was and is not read."))
        Fail(name + ": a second load does not log that HeadTracking.ini is not read");
    return {loaded.config, migrated};
}

void Comparisons(Scratch& scratch, std::set<std::string>& distinct) {
    std::cout << "Comparison 1, published build against the import, and comparison 2, import against migration\n";
    const std::vector<Input> inputs = Inputs();
    int compared = 0;
    int droppedModifier = 0;
    int touched = 0;
    int modeTouched = 0;
    int skewedMigrated = 0;
    for (const Input& input : inputs) {
        const fs::path oracleDir = scratch.Folder("oracle");
        const fs::path importDir = scratch.Folder("import");
        const fs::path importFile = importDir / "HeadTracking.ini";
        if (input.present) {
            WriteBytes(oracleDir / "HeadTracking.ini", input.bytes);
            WriteBytes(importFile, input.bytes);
            SetFileAttributesW(importFile.c_str(), FILE_ATTRIBUTE_READONLY);
        }

        const kcd_config_oracle::Result published = kcd_config_oracle::Startup(oracleDir.string());
        const Imported imported = RunImport(importFile);

        // N3: the published build bound a Ctrl, Shift or Alt code as a key of its
        // own. The import leaves that one binding out and keeps the chord.
        Observed oracle = FromOracle(published);
        const auto withoutModifier = [](std::vector<KeyBinding>& list, int code) {
            if (!IsModifierCode(code)) return;
            list.erase(std::remove(list.begin(), list.end(), KeyBinding{KeyModifiers::kNone, code}), list.end());
        };
        withoutModifier(oracle.toggle, imported.frozen.toggle_key);
        withoutModifier(oracle.cycle_tracking_mode, imported.frozen.position_key);
        withoutModifier(oracle.yaw_mode, imported.frozen.yaw_mode_key);
        for (const std::string& field : Differences(oracle, FromImport(imported)))
            Fail(input.name + ": comparison 1: " + field + " differs from the published build");

        const auto expectedStatus = input.present ? cameraunlock::config::ImportStatus::Imported
                                                  : cameraunlock::config::ImportStatus::Absent;
        if (imported.result.status != expectedStatus)
            Fail(input.name + ": the import's status is not " + (input.present ? "Imported" : "Absent"));
        using cameraunlock::config::DropRule;
        std::set<std::pair<DropRule, std::string>> expectedDrops;
        if (IsModifierCode(imported.frozen.toggle_key)) expectedDrops.insert({DropRule::ModifierKey, "ToggleKey"});
        if (IsModifierCode(imported.frozen.position_key)) expectedDrops.insert({DropRule::ModifierKey, "PositionKey"});
        if (IsModifierCode(imported.frozen.yaw_mode_key)) expectedDrops.insert({DropRule::ModifierKey, "YawModeKey"});
        std::set<std::pair<DropRule, std::string>> drops;
        for (const auto& d : imported.result.dropped) drops.insert({d.rule, d.key});
        if (drops != expectedDrops || drops.size() != imported.result.dropped.size())
            Fail(input.name + ": the import's dropped values are not exactly the hotkeys on a Ctrl, "
                 "Shift or Alt key alone");
        for (const auto& d : drops)
            if (d.first == DropRule::ModifierKey) ++droppedModifier;

        const std::set<Concept> follows(imported.result.follows_defaults_ini.begin(),
                                        imported.result.follows_defaults_ini.end());
        const std::set<Concept> untouched = UntouchedRows(imported.frozen);
        if (follows.size() != imported.result.follows_defaults_ini.size() || follows != untouched)
            Fail(input.name + ": the rows left to Defaults.ini are not exactly the ones the player never changed");
        if (!input.present && follows != kAllRows)
            Fail(input.name + ": with no file, not every row follows Defaults.ini");
        if (untouched != kAllRows) ++touched;
        if (!untouched.count(Concept::RotationEnabled)) ++modeTouched;
        if (!imported.result.pose_shaping.empty())
            Fail(input.name + ": the import recorded pose shaping the published build never applied");

        if (input.present && (Listing(importDir) != std::vector<fs::path>{"HeadTracking.ini"}
                              || ReadBytes(importFile) != input.bytes))
            Fail(input.name + ": the import changed the folder of a read-only file");

        const Migrated migrated = Migrate(scratch, input, imported, false);
        for (const std::string& field : ConfigDifferences(imported.config, migrated.config))
            Fail(input.name + ": comparison 2: " + field + " differs between the import and the migration");
        if (input.present) {
            const Migrated fromReadOnly = Migrate(scratch, input, imported, true);
            if (fromReadOnly.bytes != migrated.bytes
                    || !ConfigDifferences(fromReadOnly.config, migrated.config).empty())
                Fail(input.name + ": a read-only HeadTracking.ini migrates differently from a writable one");

            // Over a Defaults.ini that differs everywhere: an untouched row is
            // written default and takes its value, a changed row keeps the player's.
            const fs::path dir = scratch.Folder("skewed");
            WriteBytes(dir / "HeadTracking.ini", input.bytes);
            cameraunlock::config::ConfigOwner<kcd_ht::Config> owner(
                kcd_ht::OwnerOptions(dir.wstring(), scratch.Skewed()));
            const auto loaded = owner.Load();
            if (loaded.status != cameraunlock::config::ConfigLoadStatus::Migrated) {
                Fail(input.name + " (skewed Defaults.ini): the owner's load is not Migrated");
            } else {
                const kcd_ht::Config want = OverDefaults(imported.config, follows, SkewedConfig());
                for (const std::string& field : ConfigDifferences(want, loaded.config))
                    Fail(input.name + " (skewed Defaults.ini): " + field +
                         " is not Defaults.ini's where untouched and the import's where changed");
                const std::string bytes = ReadBytes(dir / "CameraUnlock.ini");
                for (const Concept row : follows)
                    if (bytes.find("\r\n" + ConceptKey(row) + "=default\r\n") == std::string::npos)
                        Fail(input.name + " (skewed Defaults.ini): " + ConceptKey(row) + " is not written default");
                distinct.insert(bytes);
                ++skewedMigrated;
            }
        }
        distinct.insert(migrated.bytes);
        ++compared;
    }
    Check(compared > 100, std::to_string(compared) + " inputs compared");
    Check(droppedModifier >= 27,
          std::to_string(droppedModifier) + " hotkeys on a Ctrl, Shift or Alt key alone dropped as ModifierKey");
    Check(touched > 0 && modeTouched > 0,
          std::to_string(touched) + " inputs change a row, " + std::to_string(modeTouched) +
              " of them the tracking mode, which then does not follow Defaults.ini");
    Check(skewedMigrated == compared - 1,
          std::to_string(skewedMigrated) + " present inputs migrated over a Defaults.ini that differs everywhere");
}

// The published build's first-run file, which is also what its players hold if
// they never changed a setting, imports into exactly the file a fresh install
// creates: with Defaults.ini at the built-in values every row it leaves at its
// default migrates as `default`. No build shipped a config in a ZIP or a
// launcher seed.
void FreshEqualsUpgradeTests(Scratch& scratch) {
    std::cout << "Fresh install against upgrade\n";
    const std::string committed = ReadBytes(kRepo / "CameraUnlock.ini");

    const fs::path upgraded = scratch.Folder("upgrade");
    WriteBytes(upgraded / "HeadTracking.ini", PublishedFirstRun());
    cameraunlock::config::ConfigOwner<kcd_ht::Config> upgrade(
        kcd_ht::OwnerOptions(upgraded.wstring(), scratch.Defaults()));
    const auto loaded = upgrade.Load();
    Check(loaded.status == cameraunlock::config::ConfigLoadStatus::Migrated
              && ReadBytes(upgraded / "CameraUnlock.ini") == committed,
          "the published first-run file imports into a CameraUnlock.ini equal to the committed file");
    Check(ReadBytes(upgraded / "HeadTracking.ini") == PublishedFirstRun(),
          "and HeadTracking.ini keeps the published build's bytes");

    const fs::path fresh = scratch.Folder("fresh");
    cameraunlock::config::ConfigOwner<kcd_ht::Config> create(
        kcd_ht::OwnerOptions(fresh.wstring(), scratch.Defaults()));
    Check(create.Load().status == cameraunlock::config::ConfigLoadStatus::Created
              && ReadBytes(fresh / "CameraUnlock.ini") == committed,
          "a fresh install creates the committed file byte for byte as CameraUnlock.ini");
}

// Writes the skewed Defaults.ini from the built-in one an earlier owner created,
// and checks it: a fresh install over it writes the committed file and runs on
// its values.
void SkewedDefaultsTests(Scratch& scratch) {
    std::cout << "Skewed Defaults.ini\n";
    std::string text = ReadBytes(scratch.BuiltinPath());
    for (const SkewedLine& line : kSkewedLines)
        text = Replaced(text, std::string("\r\n") + line.builtin + "\r\n",
                        std::string("\r\n") + line.skewed + "\r\n");
    fs::create_directories(scratch.SkewedPath().parent_path());
    WriteBytes(scratch.SkewedPath(), text);

    const fs::path fresh = scratch.Folder("fresh-skewed");
    cameraunlock::config::ConfigOwner<kcd_ht::Config> create(
        kcd_ht::OwnerOptions(fresh.wstring(), scratch.Skewed()));
    const auto loaded = create.Load();
    Check(loaded.status == cameraunlock::config::ConfigLoadStatus::Created
              && ConfigDifferences(loaded.config, SkewedConfig()).empty()
              && ReadBytes(fresh / "CameraUnlock.ini") == ReadBytes(kRepo / "CameraUnlock.ini"),
          "a fresh install over the skewed Defaults.ini writes the committed file and runs on its values");
}

// Each distinct migrated file, under migrated\ beside this executable, replacing
// what an earlier run left there.
void WriteForLint(const std::set<std::string>& distinct) {
    wchar_t exe[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) throw std::runtime_error("cannot read the test's own path");
    const fs::path dir = fs::path(std::wstring(exe, length)).parent_path() / "migrated";
    fs::remove_all(dir);
    fs::create_directories(dir);
    int n = 0;
    for (const std::string& bytes : distinct) WriteBytes(dir / (std::to_string(n++) + ".ini"), bytes);
    std::cout << "  wrote " << distinct.size() << " distinct migrated files to " << dir.string() << "\n";
}

}  // namespace

int main() {
    std::cout << "Kingdom Come: Deliverance config differential test\n";
    try {
        Scratch scratch;
        FrozenSourceTests();
        FirstRunTests(scratch);
        FreshEqualsUpgradeTests(scratch);
        SkewedDefaultsTests(scratch);
        std::set<std::string> distinct;
        Comparisons(scratch, distinct);
        WriteForLint(distinct);
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] threw: " << e.what() << "\n";
        ++g_failures;
    }
    if (g_failures == 0) {
        std::cout << "All differential tests passed\n";
        return 0;
    }
    std::cout << g_failures << " differential test(s) FAILED\n";
    return 1;
}

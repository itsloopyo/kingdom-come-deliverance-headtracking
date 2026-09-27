// CameraUnlock.ini: the committed file the table renders, what the owner creates
// and reads, what it takes from Defaults.ini, what each hotkey saves and what it
// may not, how it imports HeadTracking.ini and leaves it alone, and the frozen
// legacy reader's own contract.

#include "config.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <windows.h>

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/input/key_bindings.h>

#include "hotkey_bindings.h"
#include "legacy_config/legacy_config.h"
#include "test_support.h"

namespace {

namespace fs = std::filesystem;
using kcd_tests::Check;
using kcd_tests::NearEqual;
using cameraunlock::config::ConfigLoadStatus;
using cameraunlock::config::ConfigOwner;
using cameraunlock::config::ConfigSaveStatus;
using cameraunlock::config::DefaultsFile;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

const fs::path kCommitted = fs::path(KCD_REPO_DIR) / "CameraUnlock.ini";

std::string ReadBytes(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void WriteBytes(const fs::path& path, const std::string& bytes)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

FILETIME WriteTime(const fs::path& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        throw std::runtime_error("cannot stat " + path.string());
    return data.ftLastWriteTime;
}

bool SameTime(const FILETIME& a, const FILETIME& b)
{
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

// A new, empty game folder, with a scratch Defaults.ini under the test's own
// root rather than the player's, so no test reaches %AppData%.
struct Folder {
    fs::path game;
    fs::path defaults;

    explicit Folder(const char* name)
    {
        const fs::path root = fs::temp_directory_path() / "kcd-ht-config-tests" / name;
        std::error_code ignored;
        fs::remove_all(root, ignored);
        game = root / "game";
        fs::create_directories(game);
        defaults = root / "global" / "Defaults.ini";
    }

    fs::path Config() const { return game / "CameraUnlock.ini"; }
    fs::path Legacy() const { return game / "HeadTracking.ini"; }

    std::unique_ptr<ConfigOwner<kcd_ht::Config>> Owner() const
    {
        return std::make_unique<ConfigOwner<kcd_ht::Config>>(
            kcd_ht::OwnerOptions(game.wstring(), DefaultsFile::At(defaults.wstring())));
    }

    std::vector<std::string> Listing() const
    {
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(game)) names.push_back(entry.path().filename().string());
        std::sort(names.begin(), names.end());
        return names;
    }
};

bool Contains(const std::vector<std::string>& lines, const std::string& text)
{
    for (const std::string& line : lines)
        if (line.find(text) != std::string::npos) return true;
    return false;
}

// The lines of @p b that differ from @p a, which must have as many.
std::vector<std::string> ChangedLines(const std::string& a, const std::string& b)
{
    std::vector<std::string> la, lb, changed;
    std::istringstream sa(a), sb(b);
    for (std::string line; std::getline(sa, line);) la.push_back(line);
    for (std::string line; std::getline(sb, line);) lb.push_back(line);
    if (la.size() != lb.size()) return {"(line count differs)"};
    for (std::size_t i = 0; i < la.size(); ++i)
        if (la[i] != lb[i]) changed.push_back(lb[i]);
    return changed;
}

std::string Render()
{
    cameraunlock::config::RenderHeader header;
    header.display_name = kcd_ht::kGameDisplayName;
    return cameraunlock::config::RenderCanonicalFresh(kcd_ht::ConfigTableFor(), header);
}

// The committed file with each `from` line replaced by `to`.
std::string CommittedWith(const std::vector<std::pair<std::string, std::string>>& lines)
{
    std::string text = ReadBytes(kCommitted);
    for (const auto& [from, to] : lines) {
        const std::size_t at = text.find(from + "\r\n");
        if (at == std::string::npos) throw std::logic_error(from + " is not a line of the committed file");
        text.replace(at, from.size(), to);
    }
    return text;
}

void CommittedFileTests(int& failures)
{
    Check(failures, ReadBytes(kCommitted) == Render(),
          "CameraUnlock.ini is the table's fresh render (pixi run render-config rewrites it)");

    const Folder folder("created");
    const auto owner = folder.Owner();
    const auto loaded = owner->Load();
    Check(failures, loaded.status == ConfigLoadStatus::Created, "with no file of either name, CameraUnlock.ini is created");
    Check(failures, ReadBytes(folder.Config()) == ReadBytes(kCommitted),
          "and holds the committed file byte for byte");
    Check(failures, folder.Listing() == std::vector<std::string>{"CameraUnlock.ini"},
          "and nothing else is written beside it");
    Check(failures, fs::exists(folder.defaults), "a missing Defaults.ini is created with the built-in values");

    const kcd_ht::Config& c = loaded.config;
    Check(failures, c.udp_port == 4242 && c.enable_on_startup && c.world_space_yaw
                 && c.rotation_enabled && c.position_enabled && NearEqual(c.field_of_view, 0.0f),
          "defaults: port 4242, on at startup, world yaw, 6DOF, the game's own field of view");
    Check(failures, NearEqual(c.local_smoothing, 0.0f) && NearEqual(c.remote_smoothing, 0.15f)
                 && NearEqual(c.max_extrapolation_fraction, 0.5f),
          "defaults: smoothing 0 local, 0.15 remote, extrapolation 0.5");
    Check(failures, NearEqual(c.limit_z, 0.40f) && NearEqual(c.limit_z_back, 0.10f),
          "the Z limits default asymmetric: more room to lean in than back");
    Check(failures, c.toggle_key == "End, Ctrl+Shift+Y"
                 && c.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G"
                 && c.yaw_mode_key == "PageDown, Ctrl+Shift+H",
          "the hotkeys default to the fleet's nav keys and chords");

    const auto again = owner->Reload();
    Check(failures, again.status == cameraunlock::config::ConfigReloadStatus::Unchanged,
          "reloading the created file finds nothing to apply");
}

void CanonicalReadTests(int& failures)
{
    const Folder folder("canonical");
    WriteBytes(folder.Config(), CommittedWith({
        {"UdpPort=default", "UdpPort=5252"},
        {"EnableOnStartup=default", "EnableOnStartup=false"},
        {"WorldSpaceYaw=default", "WorldSpaceYaw=false"},
        {"RotationEnabled=default", "RotationEnabled=false"},
        {"FieldOfView=0.0", "FieldOfView=95.0"},
        {"MaxExtrapolationFraction=0.5", "MaxExtrapolationFraction=0.0"},
        {"PositionLimitYDown=default", "PositionLimitYDown=0.05"},
        {"ToggleKey=default", "ToggleKey=F9"},
    }));

    const auto loaded = folder.Owner()->Load();
    const kcd_ht::Config& c = loaded.config;
    Check(failures, loaded.status == ConfigLoadStatus::Canonical && loaded.diagnostics.empty(),
          "a stamped file is read as canonical with nothing to report");
    Check(failures, c.udp_port == 5252 && !c.enable_on_startup && !c.world_space_yaw
                 && !c.rotation_enabled && c.position_enabled && NearEqual(c.field_of_view, 95.0f)
                 && NearEqual(c.max_extrapolation_fraction, 0.0f)
                 && NearEqual(c.limit_y_down, 0.05f) && c.toggle_key == "F9",
          "every edited row is read");
    Check(failures, kcd_ht::StartupMode(c) == cameraunlock::TrackingMode::PositionOnly,
          "RotationEnabled=false with PositionEnabled=true starts position only");
}

// A row holding `default` takes Defaults.ini's value; a value in the game's file
// wins over it.
void DefaultsIniTests(int& failures)
{
    const Folder folder("defaults-ini");
    fs::create_directories(folder.defaults.parent_path());
    WriteBytes(folder.defaults, "[General]\r\nWorldSpaceYaw=false\r\n[Hotkeys]\r\nToggleKey=F8\r\n"
                                "YawModeKey=F7\r\n[Network]\r\nUdpPort=5000\r\n");
    WriteBytes(folder.Config(), CommittedWith({{"YawModeKey=default", "YawModeKey=F6"}}));
    const std::string defaultsBefore = ReadBytes(folder.defaults);

    const auto owner = folder.Owner();
    const auto loaded = owner->Load();
    const kcd_ht::Config& c = loaded.config;
    Check(failures, loaded.status == ConfigLoadStatus::Canonical && !c.world_space_yaw
                 && c.toggle_key == "F8" && c.udp_port == 5000,
          "rows holding default take Defaults.ini's values");
    Check(failures, c.yaw_mode_key == "F6", "a value in CameraUnlock.ini wins over Defaults.ini");
    Check(failures, c.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G",
          "a row Defaults.ini leaves out takes the built-in value");

    const std::string before = ReadBytes(folder.Config());
    const auto saved = owner->Save([](kcd_ht::Config& config) { config.world_space_yaw = true; });
    Check(failures, saved.status == ConfigSaveStatus::Saved
                 && ChangedLines(before, ReadBytes(folder.Config())) == std::vector<std::string>{"WorldSpaceYaw=true\r"},
          "the yaw toggle writes its value over default and changes no other line");
    Check(failures, Contains(saved.log, "WorldSpaceYaw=true is now set for this game, and no longer follows Defaults.ini."),
          "and the save's log says the row no longer follows Defaults.ini");
    Check(failures, ReadBytes(folder.defaults) == defaultsBefore, "the mod never writes Defaults.ini");
}

void SaveTests(int& failures)
{
    const Folder folder("save");
    const auto owner = folder.Owner();
    owner->Load();

    std::string before = ReadBytes(folder.Config());
    Check(failures, owner->Save([](kcd_ht::Config& c) { c.world_space_yaw = false; }).status
                     == ConfigSaveStatus::Saved,
          "the yaw mode saves");
    Check(failures, ChangedLines(before, ReadBytes(folder.Config())) == std::vector<std::string>{"WorldSpaceYaw=false\r"},
          "and changes the WorldSpaceYaw line and no other byte");

    before = ReadBytes(folder.Config());
    const cameraunlock::TrackingModeChannels rotationOnly =
        cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::RotationOnly);
    owner->Save([rotationOnly](kcd_ht::Config& c) {
        c.rotation_enabled = rotationOnly.rotation_enabled;
        c.position_enabled = rotationOnly.position_enabled;
    });
    Check(failures, ChangedLines(before, ReadBytes(folder.Config()))
                     == std::vector<std::string>{"RotationEnabled=true\r", "PositionEnabled=false\r"},
          "a mode change writes both rows of the pair over default");

    bool refused = false;
    try {
        owner->Save([](kcd_ht::Config& c) { c.enable_on_startup = false; });
    } catch (const std::logic_error&) {
        refused = true;
    }
    Check(failures, refused, "EnableOnStartup is not Writable, so nothing End does can reach the file");

    const kcd_ht::Config restarted = folder.Owner()->Load().config;
    Check(failures, !restarted.world_space_yaw && restarted.rotation_enabled && !restarted.position_enabled
                 && restarted.enable_on_startup,
          "every saved toggle comes back at the next start");
    Check(failures, folder.Listing() == std::vector<std::string>{"CameraUnlock.ini"},
          "no save writes anything but CameraUnlock.ini");
}

// HeadTracking.ini is imported once, while CameraUnlock.ini is absent, and is
// never written.
void LegacyFileTests(int& failures)
{
    const Folder folder("legacy-file");
    const std::string legacy = "[HeadTracking]\r\nWorldSpaceYaw=false\r\n[Position]\r\nLimitX=0.25\r\n";
    WriteBytes(folder.Legacy(), legacy);
    const FILETIME legacyTime = WriteTime(folder.Legacy());

    const auto first = folder.Owner()->Load();
    Check(failures, first.status == ConfigLoadStatus::Migrated && !first.config.world_space_yaw
                 && NearEqual(first.config.limit_x, 0.25f),
          "with no CameraUnlock.ini, HeadTracking.ini is imported into a new one");
    Check(failures, folder.Listing() == std::vector<std::string>{"CameraUnlock.ini", "HeadTracking.ini"},
          "and the folder then holds the two files and nothing else");
    Check(failures, ReadBytes(folder.Legacy()) == legacy && SameTime(WriteTime(folder.Legacy()), legacyTime),
          "HeadTracking.ini keeps its bytes and its write time");

    WriteBytes(folder.Legacy(), "[HeadTracking]\r\nWorldSpaceYaw=true\r\n");
    const std::string migrated = ReadBytes(folder.Config());
    const auto second = folder.Owner()->Load();
    Check(failures, second.status == ConfigLoadStatus::Canonical && !second.config.world_space_yaw
                 && ReadBytes(folder.Config()) == migrated,
          "while CameraUnlock.ini exists, HeadTracking.ini is not read again");
    Check(failures, Contains(second.log, "is left as it was and is not read."),
          "and the log says so");

    fs::remove(folder.Config());
    const auto third = folder.Owner()->Load();
    Check(failures, third.status == ConfigLoadStatus::Migrated && third.config.world_space_yaw,
          "deleting only CameraUnlock.ini imports HeadTracking.ini again");
}

// FieldOfView is 0 or 40 to 120. Anything between 0 and 40 is refused and the
// row keeps the game's own field of view.
void FieldOfViewTests(int& failures)
{
    const Folder folder("field-of-view");
    WriteBytes(folder.Config(), CommittedWith({{"FieldOfView=0.0", "FieldOfView=20.0"}}));
    const auto loaded = folder.Owner()->Load();
    Check(failures, loaded.status == ConfigLoadStatus::Canonical && !loaded.diagnostics.empty()
                 && NearEqual(loaded.config.field_of_view, 0.0f),
          "FieldOfView=20 is refused with a diagnostic and the game's own field of view is kept");

    const Folder wide("field-of-view-wide");
    WriteBytes(wide.Config(), CommittedWith({{"FieldOfView=0.0", "FieldOfView=120.0"}}));
    const auto read = wide.Owner()->Load();
    Check(failures, read.diagnostics.empty() && NearEqual(read.config.field_of_view, 120.0f),
          "FieldOfView=120 is read");
}

void HotkeyBindingTests(int& failures)
{
    const kcd_ht::HotkeyBindings b = kcd_ht::BindingsFor(kcd_ht::Config{});
    constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;
    Check(failures, b.toggle == std::vector<KeyBinding>{{KeyModifiers::kNone, 0x23}, {kChord, 0x59}},
          "End and Ctrl+Shift+Y toggle tracking");
    Check(failures, b.cycle_tracking_mode == std::vector<KeyBinding>{{KeyModifiers::kNone, 0x21}, {kChord, 0x47}},
          "Page Up and Ctrl+Shift+G cycle the tracking mode");
    Check(failures, b.yaw_mode == std::vector<KeyBinding>{{KeyModifiers::kNone, 0x22}, {kChord, 0x48}},
          "Page Down and Ctrl+Shift+H switch the yaw mode");
}

// The frozen reader, as the published builds read HeadTracking.ini.
void LegacyReaderTests(int& failures)
{
    const auto read = [](const char* name, const char* body) {
        const Folder folder(name);
        WriteBytes(folder.Legacy(), body);
        kcd_ht::legacy::Config config;
        kcd_ht::legacy::LoadConfig(folder.game.string(), config);
        return config;
    };

    const auto raised = read("legacy-limit-y", "[Position]\nLimitY=0.40\n");
    Check(failures, NearEqual(raised.limit_y, 0.40f) && NearEqual(raised.limit_y_down, 0.40f),
          "legacy: LimitY without LimitYDown gives the same travel each way");

    const auto hotkeys = read("legacy-hotkeys", "[Hotkeys]\nToggleKey=0\nPositionKey=0x01\nYawModeKey=0xFFFFFFFF\n");
    Check(failures, hotkeys.toggle_key == 0x23 && hotkeys.position_key == 0x21 && hotkeys.yaw_mode_key == 0x22,
          "legacy: a hotkey outside 0x07-0xFE, a mouse button included, keeps its default");

    const auto fov = read("legacy-fov", "[Camera]\nFieldOfView=5\n");
    Check(failures, NearEqual(fov.field_of_view, 0.0f),
          "legacy: a field of view outside 40-120 leaves the game's own setting alone");

    const auto bad = read("legacy-values", "[HeadTracking]\nUdpPort=99999\nLocalSmoothing=nan\n[Position]\nLimitZ=-3\n");
    Check(failures, bad.udp_port == 4242 && NearEqual(bad.local_smoothing, 0.0f) && bad.limit_z > 0.0f,
          "legacy: an out-of-range port, a NaN and a negative limit do not get through");
}

}  // namespace

std::string RenderCommittedConfig() { return Render(); }

int RunConfigTests()
{
    int failures = 0;
    std::cout << "Config tests\n";

    CommittedFileTests(failures);
    CanonicalReadTests(failures);
    DefaultsIniTests(failures);
    SaveTests(failures);
    LegacyFileTests(failures);
    FieldOfViewTests(failures);
    HotkeyBindingTests(failures);
    LegacyReaderTests(failures);

    return kcd_tests::Report("Config tests", failures);
}

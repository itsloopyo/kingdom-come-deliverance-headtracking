// Characterization tests for the INI contract: the keys the mod reads, the
// defaults it falls back to, and the agreement between the file it writes on
// first run and the defaults compiled into Config. A key renamed on one side
// only would otherwise ship as a setting that silently does nothing.

#include "config.h"

#include <cstdio>
#include <string>

#include <windows.h>

#include "test_support.h"

namespace
{
    using kcd_tests::Check;
    using kcd_tests::NearEqual;

    std::string MakeTempDir()
    {
        char tempRoot[MAX_PATH]{};
        GetTempPathA(MAX_PATH, tempRoot);
        std::string dir = std::string(tempRoot) + "kcd-ht-config-tests";
        CreateDirectoryA(dir.c_str(), nullptr);
        DeleteFileA((dir + "\\HeadTracking.ini").c_str());
        return dir;
    }

    void WriteIni(const std::string& dir, const char* body)
    {
        FILE* file = nullptr;
        fopen_s(&file, (dir + "\\HeadTracking.ini").c_str(), "w");
        if (file == nullptr) return;
        std::fputs(body, file);
        std::fclose(file);
    }

    void MissingFileTests(int& failures)
    {
        const std::string dir = MakeTempDir();

        kcd_ht::Config config;
        kcd_ht::LoadConfig(dir, config);

        Check(failures, config.udp_port == 4242, "a missing INI leaves the OpenTrack port at 4242");
        Check(failures, config.enable_on_startup, "a missing INI leaves tracking enabled on startup");
        Check(failures, config.world_space_yaw, "a missing INI leaves yaw horizon-locked");
        Check(failures, NearEqual(config.local_smoothing, 0.0f),
              "local smoothing defaults to 0 - a tracker on this machine is already stable");
        Check(failures, NearEqual(config.remote_smoothing, 0.15f),
              "remote smoothing defaults to 0.15 for a phone over WiFi");
        Check(failures, NearEqual(config.field_of_view, 0.0f),
              "a missing INI leaves the field of view to the game");
        Check(failures, config.position_enabled, "position tracking defaults on");
        Check(failures, NearEqual(config.limit_z, 0.40f) && NearEqual(config.limit_z_back, 0.10f),
              "the Z limits default asymmetric: more room to lean in than back");
    }

    void ParsingTests(int& failures)
    {
        const std::string dir = MakeTempDir();
        WriteIni(dir,
            "[HeadTracking]\nUdpPort=5252\nEnableOnStartup=0\nWorldSpaceYaw=0\n"
            "LocalSmoothing=0.25\nRemoteSmoothing=0.75\nMaxExtrapolationFraction=0\n"
            "[Camera]\nFieldOfView=95\n"
            "[Position]\nEnabled=0\n"
            "LimitX=0.11\nLimitY=0.22\nLimitYDown=0.05\nLimitZ=0.33\nLimitZBack=0.44\n"
            "[Hotkeys]\nToggleKey=0x51\nPositionKey=0x52\nYawModeKey=0x53\n");

        kcd_ht::Config config;
        kcd_ht::LoadConfig(dir, config);

        Check(failures, config.udp_port == 5252, "UdpPort is read");
        Check(failures, !config.enable_on_startup && !config.world_space_yaw,
              "the booleans are read");
        Check(failures, NearEqual(config.local_smoothing, 0.25f)
                     && NearEqual(config.remote_smoothing, 0.75f),
              "both smoothing values are read");
        Check(failures, NearEqual(config.max_extrapolation_fraction, 0.0f),
              "extrapolation can be turned off");
        Check(failures, NearEqual(config.field_of_view, 95.0f), "the field of view override is read");
        Check(failures, !config.position_enabled, "position tracking can be turned off");
        Check(failures, NearEqual(config.limit_x, 0.11f) && NearEqual(config.limit_y, 0.22f)
                     && NearEqual(config.limit_y_down, 0.05f) && NearEqual(config.limit_z, 0.33f)
                     && NearEqual(config.limit_z_back, 0.44f),
              "every position limit is read, including the asymmetric LimitYDown");
        Check(failures, config.toggle_key == 0x51 && config.position_key == 0x52
                     && config.yaw_mode_key == 0x53,
              "the hotkeys are read as hex");
    }

    void ValidationTests(int& failures)
    {
        const std::string dir = MakeTempDir();
        WriteIni(dir,
            "[HeadTracking]\nUdpPort=99999\nLocalSmoothing=nan\nRemoteSmoothing=1e400\n"
            "[Camera]\nFieldOfView=5\n"
            "[Position]\nLimitZ=-3\n");

        kcd_ht::Config config;
        kcd_ht::LoadConfig(dir, config);

        Check(failures, config.udp_port == 4242, "an out-of-range port falls back to the default");
        Check(failures, NearEqual(config.local_smoothing, 0.0f),
              "a NaN smoothing value falls back to the default rather than poisoning the filter");
        Check(failures, config.remote_smoothing >= 0.0f && config.remote_smoothing <= 1.0f,
              "an infinite smoothing value is clamped into range");
        Check(failures, config.limit_z > 0.0f, "a negative position limit is rejected");
        // Clamping this one into the band would change what the player sees on the
        // strength of a typo, so it turns the override off instead.
        Check(failures, NearEqual(config.field_of_view, 0.0f),
              "a field of view outside the band leaves the game's own setting alone");
    }

    // ReadHex has no range of its own, so an unchecked hotkey key is whatever
    // strtol made of the text. The three values below are the ones that reach a
    // user: 0 from a typo (the poller treats it as unbound and fires nothing),
    // a mouse button (GetAsyncKeyState reports it like any other key, so every
    // click in normal play toggles the mod), and an overflowed literal.
    void HotkeyValidationTests(int& failures)
    {
        const std::string dir = MakeTempDir();
        WriteIni(dir,
            "[Hotkeys]\nToggleKey=0\nPositionKey=0x01\nYawModeKey=0xFFFFFFFF\n");

        kcd_ht::Config config;
        kcd_ht::LoadConfig(dir, config);
        const kcd_ht::Config compiled;

        Check(failures, config.toggle_key == compiled.toggle_key,
              "an unbindable hotkey of 0 keeps the default instead of silently firing nothing");
        Check(failures, config.position_key == compiled.position_key,
              "a mouse button is rejected - every click would otherwise cycle tracking mode");
        Check(failures, config.yaw_mode_key == compiled.yaw_mode_key,
              "a hotkey that overflowed strtol keeps the default");
    }

    void HotkeyBindableRangeTests(int& failures)
    {
        const std::string dir = MakeTempDir();
        // The nav cluster and the Ctrl+Shift chord letters both have to survive
        // the check - rejecting the mod's own shipped bindings would be worse
        // than not checking at all.
        WriteIni(dir, "[Hotkeys]\nToggleKey=0x23\nPositionKey=0x21\nYawModeKey=0xFE\n");

        kcd_ht::Config config;
        kcd_ht::LoadConfig(dir, config);

        Check(failures, config.toggle_key == 0x23 && config.position_key == 0x21
                     && config.yaw_mode_key == 0xFE,
              "the shipped nav-cluster codes and the top of the range are accepted");
    }

    void DefaultFileMatchesDefaultsTests(int& failures)
    {
        const std::string dir = MakeTempDir();
        kcd_ht::WriteDefaultConfigIfMissing(dir);

        kcd_ht::Config fromFile;
        kcd_ht::LoadConfig(dir, fromFile);
        const kcd_ht::Config compiled;

        Check(failures, fromFile.udp_port == compiled.udp_port
                     && fromFile.enable_on_startup == compiled.enable_on_startup
                     && fromFile.world_space_yaw == compiled.world_space_yaw
                     && NearEqual(fromFile.local_smoothing, compiled.local_smoothing)
                     && NearEqual(fromFile.remote_smoothing, compiled.remote_smoothing)
                     && NearEqual(fromFile.max_extrapolation_fraction,
                                  compiled.max_extrapolation_fraction)
                     && NearEqual(fromFile.field_of_view, compiled.field_of_view)
                     && fromFile.position_enabled == compiled.position_enabled
                     && NearEqual(fromFile.limit_x, compiled.limit_x)
                     && NearEqual(fromFile.limit_y, compiled.limit_y)
                     && NearEqual(fromFile.limit_y_down, compiled.limit_y_down)
                     && NearEqual(fromFile.limit_z, compiled.limit_z)
                     && NearEqual(fromFile.limit_z_back, compiled.limit_z_back)
                     && fromFile.toggle_key == compiled.toggle_key
                     && fromFile.position_key == compiled.position_key
                     && fromFile.yaw_mode_key == compiled.yaw_mode_key,
              "the INI written on first run round-trips to the compiled defaults");
    }
}

int RunConfigTests()
{
    int failures = 0;
    std::cout << "Config tests\n";

    MissingFileTests(failures);
    ParsingTests(failures);
    ValidationTests(failures);
    HotkeyValidationTests(failures);
    HotkeyBindableRangeTests(failures);
    DefaultFileMatchesDefaultsTests(failures);

    return kcd_tests::Report("Config tests", failures);
}

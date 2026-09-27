#pragma once

#include <memory>

#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/input/hotkey_poller.h>

#include "config.h"
#include "runtime_state.h"

namespace kcd_ht
{
    // Registers each action's key list, then starts polling. Every toggle but
    // the master one saves its new state through @p owner. The returned poller
    // owns the polling thread; Stop() it before the session or the owner goes
    // away.
    std::unique_ptr<cameraunlock::input::HotkeyPoller> StartHotkeys(
        Session& session, const Config& config, cameraunlock::config::ConfigOwner<Config>& owner);
}

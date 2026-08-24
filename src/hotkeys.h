#pragma once

#include <memory>

#include <cameraunlock/input/hotkey_poller.h>

#include "config.h"
#include "runtime_state.h"

namespace kcd_ht
{
    // Binds the nav-cluster keys and their Ctrl+Shift chord alternatives, then
    // starts polling. The returned poller owns the polling thread and captures
    // @p session, so both have to outlive it - they do, because the mod pins
    // itself and has no shutdown path.
    std::unique_ptr<cameraunlock::input::HotkeyPoller> StartHotkeys(Session& session,
                                                                    const Config& config);
}

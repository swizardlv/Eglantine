// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/hle/service/ipc_helpers.h"
#include "core/hle/service/nx2_stub_interface.h"
#include "core/hle/service/service.h"

namespace Service::Audio {

// Switch 2 "auddmg:u" (nn::audio::devmgr::IUserSessionManager). Command 0 takes a PID, a u32
// and a u64 pid_reserved and returns an IUserSession (nn::audio::devmgr::IUserSession). The
// session's own commands (0, 100, 200, ...) are undocumented, so it is returned as a named
// stub whose unknown commands are auto-stubbed and logged.
class INx2AudioDeviceManager final : public ServiceFramework<INx2AudioDeviceManager> {
public:
    explicit INx2AudioDeviceManager(Core::System& system_)
        : ServiceFramework{system_, "nx2:auddmg:u"} {
        // clang-format off
        static const FunctionInfo functions[] = {
            {0, &INx2AudioDeviceManager::GetUserSession, "GetUserSession (guessed)"},
        };
        // clang-format on
        RegisterHandlers(functions);
    }

private:
    void GetUserSession(HLERequestContext& ctx) {
        ReturnNx2StubInterface(system, ctx, "auddmg:u");
    }
};

} // namespace Service::Audio

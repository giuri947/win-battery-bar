#pragma once

#include "battery_model.h"
#include <string>

namespace SettingsStore {
    AppSettings Load();
    void Save(const AppSettings& s);
    bool IsAutoStartEnabled();
    bool SetAutoStartEnabled(bool enable);
    bool IsDeviceHidden(const AppSettings& s, const std::wstring& id);
    void ToggleDeviceHidden(AppSettings& s, const std::wstring& id);
}

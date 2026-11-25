#pragma once
#include "daemon/framework/application.hpp"

namespace daemonos {
class LauncherApp final : public Application {
public: AppId id() const override; std::string_view title() const override;
};
class ClockApp final : public Application {
public: AppId id() const override; std::string_view title() const override;
};
class SettingsApp final : public Application {
public: AppId id() const override; std::string_view title() const override;
};
class CreatureApp final : public Application {
public:
    AppId id() const override;
    std::string_view title() const override;
    Capabilities capabilities() const override;
};
}

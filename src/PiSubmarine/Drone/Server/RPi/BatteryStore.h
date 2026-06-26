#pragma once

#include <filesystem>

#include "PiSubmarine/Battery/Persistence/Api/IStore.h"

namespace PiSubmarine::Drone::Server::RPi
{
    class BatteryStore final : public Battery::Persistence::Api::IStore
    {
    public:
        explicit BatteryStore(std::filesystem::path path);

        [[nodiscard]] Error::Api::Result<std::vector<std::byte>> Load() const override;
        [[nodiscard]] Error::Api::Result<void> Save(std::span<const std::byte> data) override;

    private:
        std::filesystem::path m_Path;
    };
}

#pragma once

#include "PiSubmarine/Control/Lamp/Api/IController.h"
#include "PiSubmarine/Lamp/Api/IController.h"
#include "PiSubmarine/Lamp/Telemetry/Api/IProvider.h"

namespace PiSubmarine::Drone::Server::RPi
{
    class ControlLampAdapter final
        : public Control::Lamp::Api::IController
        , public Lamp::Telemetry::Api::IProvider
    {
    public:
        ControlLampAdapter(
            Lamp::Api::IController& lampController,
            Lamp::Telemetry::Api::IProvider& telemetryProvider) noexcept;

        [[nodiscard]] Error::Api::Result<void> SetTarget(const Control::Lamp::Api::Command& target) override;
        [[nodiscard]] Error::Api::Result<Lamp::Telemetry::Api::Status> GetStatus() const override;

    private:
        Lamp::Api::IController& m_LampController;
        Lamp::Telemetry::Api::IProvider& m_TelemetryProvider;
    };
}

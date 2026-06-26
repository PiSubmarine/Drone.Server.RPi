#include "PiSubmarine/Drone/Server/RPi/ControlLampAdapter.h"

namespace PiSubmarine::Drone::Server::RPi
{
    ControlLampAdapter::ControlLampAdapter(
        Lamp::Api::IController& lampController,
        Lamp::Telemetry::Api::IProvider& telemetryProvider) noexcept
        : m_LampController(lampController)
        , m_TelemetryProvider(telemetryProvider)
    {
    }

    Error::Api::Result<void> ControlLampAdapter::SetTarget(const Control::Lamp::Api::Command& target)
    {
        return m_LampController.SetIntensity(target.Intensity());
    }

    Error::Api::Result<Lamp::Telemetry::Api::Status> ControlLampAdapter::GetStatus() const
    {
        return m_TelemetryProvider.GetStatus();
    }
}

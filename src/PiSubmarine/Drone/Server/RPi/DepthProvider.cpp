#include "PiSubmarine/Drone/Server/RPi/DepthProvider.h"

namespace PiSubmarine::Drone::Server::RPi
{
    Error::Api::Result<Depth::Telemetry::Api::State> DepthProvider::GetState() const
    {
        return Depth::Telemetry::Api::State{Meters{0}};
    }
}

#include "PiSubmarine/Drone/Server/RPi/ProximityProvider.h"

namespace PiSubmarine::Drone::Server::RPi
{
    Error::Api::Result<Proximity::Telemetry::Api::State> ProximityProvider::GetState() const
    {
        return Proximity::Telemetry::Api::State{Meters{0}};
    }
}

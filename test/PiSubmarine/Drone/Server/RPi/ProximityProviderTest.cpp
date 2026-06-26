#include <gtest/gtest.h>

#include "PiSubmarine/Drone/Server/RPi/ProximityProvider.h"

namespace PiSubmarine::Drone::Server::RPi
{
    TEST(ProximityProviderTest, ReturnsPlaceholderEmptyState)
    {
        ProximityProvider provider;

        const auto state = provider.GetState();

        ASSERT_TRUE(state.has_value());
        EXPECT_FALSE(state->Distance.has_value());
    }
}

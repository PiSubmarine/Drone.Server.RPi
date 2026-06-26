#include <gtest/gtest.h>

#include "PiSubmarine/Drone/Server/RPi/DepthProvider.h"

namespace PiSubmarine::Drone::Server::RPi
{
    TEST(DepthProviderTest, ReturnsPlaceholderEmptyState)
    {
        DepthProvider provider;

        const auto state = provider.GetState();

        ASSERT_TRUE(state.has_value());
        EXPECT_FALSE(state->Depth.has_value());
    }
}

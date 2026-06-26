#include <gtest/gtest.h>

#include <array>
#include <filesystem>

#include "PiSubmarine/Drone/Server/RPi/BatteryStore.h"

namespace PiSubmarine::Drone::Server::RPi
{
    TEST(BatteryStoreTest, ReportsNotFoundForMissingFile)
    {
        const auto path = std::filesystem::temp_directory_path() / "pisubmarine-battery-store-missing.bin";
        std::error_code errorCode;
        std::filesystem::remove(path, errorCode);
        BatteryStore store(path);

        const auto loadResult = store.Load();

        ASSERT_FALSE(loadResult.has_value());
        EXPECT_EQ(loadResult.error().Condition, Error::Api::ErrorCondition::NotFound);
    }

    TEST(BatteryStoreTest, SavesAndLoadsPayload)
    {
        const auto path = std::filesystem::temp_directory_path() / "pisubmarine-battery-store.bin";
        BatteryStore store(path);
        const std::array<std::byte, 3> payload{
            std::byte{0x01},
            std::byte{0x23},
            std::byte{0x45}};

        ASSERT_TRUE(store.Save(payload).has_value());

        const auto loadResult = store.Load();

        ASSERT_TRUE(loadResult.has_value());
        EXPECT_EQ(loadResult->size(), payload.size());
        EXPECT_EQ((*loadResult)[0], payload[0]);
        EXPECT_EQ((*loadResult)[1], payload[1]);
        EXPECT_EQ((*loadResult)[2], payload[2]);

        std::error_code errorCode;
        std::filesystem::remove(path, errorCode);
    }
}

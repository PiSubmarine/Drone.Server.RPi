#include "PiSubmarine/Drone/Server/RPi/BatteryStore.h"

#include <fstream>
#include <iterator>

#include "PiSubmarine/Error/Api/ErrorCondition.h"
#include "PiSubmarine/Error/Api/MakeError.h"

namespace PiSubmarine::Drone::Server::RPi
{
    namespace
    {
        [[nodiscard]] Error::Api::Error MakePersistenceError(const Error::Api::ErrorCondition condition)
        {
            return Error::Api::MakeError(condition);
        }
    }

    BatteryStore::BatteryStore(std::filesystem::path path)
        : m_Path(std::move(path))
    {
    }

    Error::Api::Result<std::vector<std::byte>> BatteryStore::Load() const
    {
        std::ifstream stream(m_Path, std::ios::binary);
        if (!stream.is_open())
        {
            if (!std::filesystem::exists(m_Path))
            {
                return std::unexpected(MakePersistenceError(Error::Api::ErrorCondition::NotFound));
            }

            return std::unexpected(MakePersistenceError(Error::Api::ErrorCondition::CommunicationError));
        }

        const auto bytes = std::vector<char>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        std::vector<std::byte> result;
        result.reserve(bytes.size());
        for (const char value : bytes)
        {
            result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
        }

        return result;
    }

    Error::Api::Result<void> BatteryStore::Save(const std::span<const std::byte> data)
    {
        const auto parent = m_Path.parent_path();
        if (!parent.empty())
        {
            std::error_code errorCode;
            std::filesystem::create_directories(parent, errorCode);
            if (errorCode)
            {
                return std::unexpected(MakePersistenceError(Error::Api::ErrorCondition::CommunicationError));
            }
        }

        std::ofstream stream(m_Path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            return std::unexpected(MakePersistenceError(Error::Api::ErrorCondition::CommunicationError));
        }

        for (const std::byte value : data)
        {
            stream.put(static_cast<char>(value));
        }

        if (!stream.good())
        {
            return std::unexpected(MakePersistenceError(Error::Api::ErrorCondition::CommunicationError));
        }

        return {};
    }
}

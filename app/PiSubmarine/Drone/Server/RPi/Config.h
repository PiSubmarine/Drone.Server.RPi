#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include "PiSubmarine/Battery/Motherboard/Config.h"
#include "PiSubmarine/Degrees.h"
#include "PiSubmarine/Drone/Server/BallastControlTuning.h"
#include "PiSubmarine/Drone/Server/VerticalControlTuning.h"
#include "PiSubmarine/Max17261/Units.h"
#include "PiSubmarine/Motor/Drv8908/Config.h"

namespace PiSubmarine::Drone::Server::RPi
{
	struct Config
	{
		std::string GrpcAddress = "0.0.0.0:50051";
		std::string ControlAddress = "0.0.0.0:50052";
		std::string TelemetryAddress = "0.0.0.0:50053";
		std::filesystem::path ServerCertificatePath;
		std::filesystem::path ServerPrivateKeyPath;
		std::filesystem::path ClientCertificateAuthorityPath;
		std::string VideoResourceId = "video-main";
		std::string VideoSourceDescription;
		std::string StartupVideoEndpoint;
		bool StartupVideoEnable = false;
		std::int64_t TickPeriodMilliseconds = std::chrono::milliseconds(10).count();
		std::size_t ReceiveQueueCapacity = 64;
		std::size_t MaxDatagramSize = 65507;

		std::filesystem::path I2cDevice = "/dev/i2c-1";
		std::filesystem::path ThrustersSpiDevice = "/dev/spidev0.0";
		std::filesystem::path LampsAndBallastSpiDevice = "/dev/spidev0.1";
		std::filesystem::path GpioChip = "/dev/gpiochip0";
		std::filesystem::path PwmChannel = "/sys/class/pwm/pwmchip0/pwm0";
		std::filesystem::path BatteryStorePath = "/var/lib/pisubmarine/battery-learning.bin";
		std::size_t ThrustersNSleepPin = 5;
		std::size_t ThrustersNFaultPin = 6;
		std::size_t LampsAndBallastNSleepPin = 16;
		std::size_t LampsAndBallastNFaultPin = 20;
		std::uint32_t SpiSpeed = 5'000'000;
		std::int64_t BatteryDesignCapacityUah = 3'500'000;
		std::int64_t BatteryChargeTerminationUa = 68'000;
		std::int64_t BatteryEmptyVoltageUv = 12'000'000;
		bool BatteryForceGaugeReset = false;
		Motor::Drv8908::Config ThrusterMotor;
		Motor::Drv8908::Config BallastMotor;
		::PiSubmarine::Drone::Server::BallastControlTuning BallastControl;
		::PiSubmarine::Drone::Server::VerticalControlTuning VerticalControl;
		Degrees GimbalNeutralServoAngle = Degrees{90.0};
		bool GimbalPitchInverted = false;
		Degrees MinimumGimbalPitch = Degrees{-90.0};
		Degrees MaximumGimbalPitch = Degrees{90.0};

		[[nodiscard]] Battery::Motherboard::Config CreateBatteryConfig() const
		{
			return Battery::Motherboard::Config{
				.DesignCapacity = Max17261::MicroAmpereHours{BatteryDesignCapacityUah},
				.ChargeTerminationCurrent = Max17261::MicroAmperes{BatteryChargeTerminationUa},
				.EmptyVoltage = Max17261::MicroVolts{BatteryEmptyVoltageUv},
				.ForceGaugeReset = BatteryForceGaugeReset
			};
		}
	};
}

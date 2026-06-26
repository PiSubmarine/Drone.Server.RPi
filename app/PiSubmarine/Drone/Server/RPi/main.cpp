#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>

#include <CLI/CLI.hpp>
#include <linux/spi/spidev.h>
#include <spdlog/spdlog.h>

#include "PiSubmarine/Ballast/Telemetry/Chipset/Provider.h"
#include "PiSubmarine/Battery/Motherboard/Provider.h"
#include "PiSubmarine/Chipset/Client/I2C/Client.h"
#include "PiSubmarine/Drone/Server/Logging.h"
#include "PiSubmarine/Drone/Server/LoggerFactory.h"
#include "PiSubmarine/Drone/Server/Runtime.h"
#include "PiSubmarine/Drone/Server/RPi/BatteryStore.h"
#include "PiSubmarine/Drone/Server/RPi/ControlLampAdapter.h"
#include "PiSubmarine/Drone/Server/RPi/DepthProvider.h"
#include "PiSubmarine/Drone/Server/RPi/ProximityProvider.h"
#include "PiSubmarine/Drv8908/Device.h"
#include "PiSubmarine/Drv8908/PowerManager.h"
#include "PiSubmarine/Error/Api/Error.h"
#include "PiSubmarine/GPIO/Linux/Driver.h"
#include "PiSubmarine/I2C/Linux/Driver.h"
#include "PiSubmarine/Lamp/Drv8908/Controller.h"
#include "PiSubmarine/Max17261/Device.h"
#include "PiSubmarine/Motor/Bidirectional/Drv8908/Controller.h"
#include "PiSubmarine/Motor/Unidirectional/Drv8908/Controller.h"
#include "PiSubmarine/PWM/Linux/Driver.h"
#include "PiSubmarine/SPI/Linux/Driver.h"
#include "PiSubmarine/Servo/SG90/Controller.h"
#include "PiSubmarine/Video/Server/GStreamer/Source.h"
#include "PiSubmarine/Video/Subscription/Api/Endpoint.h"

namespace PiSubmarine::Drone::Server::RPi
{
	namespace
	{
		struct HardwareConfig
		{
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
			Battery::Motherboard::Config Battery{
				                             .DesignCapacity = Max17261::MicroAmpereHours{3'500'000},
				                             .ChargeTerminationCurrent = Max17261::MicroAmperes{68'000},
				                             .EmptyVoltage = Max17261::MicroVolts{12'000'000}
			                             };
            Motor::Drv8908::Config ThrusterMotor{};
			Motor::Drv8908::Config BallastMotor{};
		};

		[[nodiscard]] std::optional<PiSubmarine::Udp::Api::Endpoint> ParseEndpoint(const std::string& value)
		{
			const auto separator = value.rfind(':');
			if (separator == std::string::npos || separator == 0 || separator == value.size() - 1)
			{
				return std::nullopt;
			}

			try
			{
				const auto port = std::stoul(value.substr(separator + 1));
				if (port > std::numeric_limits<std::uint16_t>::max())
				{
					return std::nullopt;
				}

				return PiSubmarine::Udp::Api::Endpoint{
					.Address = value.substr(0, separator),
					.Port = static_cast<std::uint16_t>(port)
				};
			}
			catch (...)
			{
				return std::nullopt;
			}
		}

		[[nodiscard]] std::string FormatEndpoint(const PiSubmarine::Udp::Api::Endpoint& endpoint)
		{
			return endpoint.Address + ":" + std::to_string(endpoint.Port);
		}

		[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream.is_open())
			{
				throw std::runtime_error("Failed to open file: " + path.string());
			}

			return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		}

		[[nodiscard]] std::shared_ptr<spdlog::logger> CreateLogger()
		{
			return PiSubmarine::Drone::Server::CreateConfiguredLogger("Drone.Server.RPi.App");
		}

		[[nodiscard]] const char* ToString(const Error::Api::ErrorCondition condition) noexcept
		{
			switch (condition)
			{
			case Error::Api::ErrorCondition::ContractError:
				return "ContractError";
			case Error::Api::ErrorCondition::CommunicationError:
				return "CommunicationError";
			case Error::Api::ErrorCondition::DeviceError:
				return "DeviceError";
			case Error::Api::ErrorCondition::NotFound:
				return "NotFound";
			case Error::Api::ErrorCondition::NotReady:
				return "NotReady";
			case Error::Api::ErrorCondition::UnknownError:
				return "UnknownError";
			}

			return "UnknownError";
		}

		[[nodiscard]] std::string ToString(const Error::Api::Error& error)
		{
			std::string text = ToString(error.Condition);
			if (error.HasCause())
			{
				text += " (";
				text += error.Cause.message();
				text += ")";
			}

			return text;
		}
	}
}

int main(const int argc, char** argv)
{
	using Runtime = PiSubmarine::Drone::Server::Runtime;
	using namespace PiSubmarine::Drone::Server::RPi;
	using namespace PiSubmarine::RegUtils;
	const auto logger = CreateLogger();

	try
	{
		Runtime::Config config;
		HardwareConfig hardware;
		std::filesystem::path serverCertificatePath;
		std::filesystem::path serverPrivateKeyPath;
		std::filesystem::path clientCertificateAuthorityPath;
		auto controlAddress = FormatEndpoint(config.ControlEndpoint);
		auto telemetryAddress = FormatEndpoint(config.TelemetryEndpoint);
		std::string grpcAddress = "0.0.0.0:50051";
		std::string videoResourceId = config.VideoController.ResourceId.Value;
		std::string videoSourceDescription;
		std::string startupVideoEndpoint;
		bool startupVideoEnable = config.StartupVideoEnable;
		auto tickPeriodMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(config.TickPeriod).count();
		auto batteryDesignCapacityUah = hardware.Battery.DesignCapacity.GetMicroAmpereHours();
		auto batteryChargeTerminationUa = hardware.Battery.ChargeTerminationCurrent.GetMicroAmperes();
		auto batteryEmptyVoltageUv = hardware.Battery.EmptyVoltage.GetMicroVolts();

		CLI::App app{"PiSubmarine Raspberry Pi drone server"};
		app.add_option("--grpc-address", grpcAddress, "Shared gRPC bind address")->default_val(grpcAddress);
		app.add_option("--server-cert", serverCertificatePath, "PEM server certificate chain file")->required();
		app.add_option("--server-key", serverPrivateKeyPath, "PEM server private key file")->required();
		app.add_option("--client-ca", clientCertificateAuthorityPath, "PEM client certificate authority file")->
		    required();
		app.add_option("--control-address", controlAddress, "Control UDP bind address")->default_val(controlAddress);
		app.add_option("--telemetry-address", telemetryAddress, "Telemetry UDP bind address")->default_val(
			telemetryAddress);
		app.add_option("--video-resource-id", videoResourceId, "Lease resource id used for video streaming")->
		    default_val(videoResourceId);
		app.add_option("--video-source", videoSourceDescription,
		               "Optional explicit GStreamer source element description. Leave empty for autodetect.");
		app.add_option("--startup-video-endpoint", startupVideoEndpoint,
		               "Optional host:port RTP endpoint to subscribe immediately at startup.");
		app.add_flag("--startup-video-enable", startupVideoEnable,
		             "Enable video streaming target at startup using low-latency profile and autofocus.");
		app.add_option("--tick-period-ms", tickPeriodMilliseconds, "Tick period in milliseconds")->default_val(
			tickPeriodMilliseconds);
		app.add_option("--receive-queue-capacity", config.ReceiveQueueCapacity, "Per-socket receive queue capacity")->
		    default_val(config.ReceiveQueueCapacity);
		app.add_option("--max-datagram-size", config.MaxDatagramSize, "Maximum UDP datagram size in bytes")->
		    default_val(config.MaxDatagramSize);

		app.add_option("--i2c-device", hardware.I2cDevice, "Linux I2C device used for chipset and battery monitor")
		   ->default_val(hardware.I2cDevice.string());
		app.add_option("--thrusters-spi-device", hardware.ThrustersSpiDevice, "SPI device for the thrusters DRV8908")
		   ->default_val(hardware.ThrustersSpiDevice.string());
		app.add_option("--lamps-ballast-spi-device", hardware.LampsAndBallastSpiDevice,
		               "SPI device for the lamps and ballast DRV8908")
		   ->default_val(hardware.LampsAndBallastSpiDevice.string());
		app.add_option("--gpio-chip", hardware.GpioChip, "GPIO chip path for DRV8908 control pins")
		   ->default_val(hardware.GpioChip.string());
		app.add_option("--pwm-channel", hardware.PwmChannel, "Linux PWM sysfs channel path used for the gimbal servo")
		   ->default_val(hardware.PwmChannel.string());
		app.add_option("--battery-store", hardware.BatteryStorePath,
		               "Persistent file path for MAX17261 learning parameters")
		   ->default_val(hardware.BatteryStorePath.string());
		app.add_option("--thrusters-n-sleep-pin", hardware.ThrustersNSleepPin,
		               "GPIO line connected to thrusters DRV8908 nSLEEP")
		   ->default_val(hardware.ThrustersNSleepPin);
		app.add_option("--thrusters-n-fault-pin", hardware.ThrustersNFaultPin,
		               "GPIO line connected to thrusters DRV8908 nFAULT")
		   ->default_val(hardware.ThrustersNFaultPin);
		app.add_option("--lamps-ballast-n-sleep-pin", hardware.LampsAndBallastNSleepPin,
		               "GPIO line connected to lamps/ballast DRV8908 nSLEEP")
		   ->default_val(hardware.LampsAndBallastNSleepPin);
		app.add_option("--lamps-ballast-n-fault-pin", hardware.LampsAndBallastNFaultPin,
		               "GPIO line connected to lamps/ballast DRV8908 nFAULT")
		   ->default_val(hardware.LampsAndBallastNFaultPin);
		app.add_option("--spi-speed-hz", hardware.SpiSpeed, "SPI bus speed for both DRV8908 devices")->default_val(
			hardware.SpiSpeed);
		app.add_option("--battery-design-capacity-uah", batteryDesignCapacityUah,
		               "MAX17261 design capacity in microampere-hours")
		   ->default_val(batteryDesignCapacityUah);
		app.add_option("--battery-charge-termination-ua", batteryChargeTerminationUa,
		               "MAX17261 charge termination current in microamperes")
		   ->default_val(batteryChargeTerminationUa);
		app.add_option("--battery-empty-voltage-uv", batteryEmptyVoltageUv,
		               "MAX17261 battery empty voltage in microvolts")
		   ->default_val(batteryEmptyVoltageUv);
		app.add_flag("--battery-force-gauge-reset", hardware.Battery.ForceGaugeReset,
		             "Force MAX17261 gauge reset during startup.");

		CLI11_PARSE(app, argc, argv);

		const auto parsedControlEndpoint = ParseEndpoint(controlAddress);
		if (!parsedControlEndpoint.has_value())
		{
			SPDLOG_LOGGER_CRITICAL(logger, "Invalid --control-address value. Expected host:port.");
			return 2;
		}

		const auto parsedTelemetryEndpoint = ParseEndpoint(telemetryAddress);
		if (!parsedTelemetryEndpoint.has_value())
		{
			SPDLOG_LOGGER_CRITICAL(logger, "Invalid --telemetry-address value. Expected host:port.");
			return 2;
		}

		std::optional<PiSubmarine::Video::Subscription::Api::Endpoint> parsedStartupVideoEndpoint;
		if (!startupVideoEndpoint.empty())
		{
			const auto parsedEndpoint = ParseEndpoint(startupVideoEndpoint);
			if (!parsedEndpoint.has_value())
			{
				SPDLOG_LOGGER_CRITICAL(logger, "Invalid --startup-video-endpoint value. Expected host:port.");
				return 2;
			}

			parsedStartupVideoEndpoint = PiSubmarine::Video::Subscription::Api::Endpoint{
				.Host = parsedEndpoint->Address,
				.Port = parsedEndpoint->Port
			};
		}

		config.ControlEndpoint = *parsedControlEndpoint;
		config.TelemetryEndpoint = *parsedTelemetryEndpoint;
		config.StartupVideoEndpoint = parsedStartupVideoEndpoint;
		config.StartupVideoEnable = startupVideoEnable;
		config.TickPeriod = std::chrono::milliseconds(tickPeriodMilliseconds);
		config.GrpcServer.Address = grpcAddress;
		config.GrpcServer.ServerCertificateChain = ReadTextFile(serverCertificatePath);
		config.GrpcServer.ServerPrivateKey = ReadTextFile(serverPrivateKeyPath);
		config.GrpcServer.ClientCertificateAuthority = ReadTextFile(clientCertificateAuthorityPath);
		config.VideoController.ResourceId = PiSubmarine::Lease::Api::ResourceId{.Value = videoResourceId};
		hardware.Battery.DesignCapacity = PiSubmarine::Max17261::MicroAmpereHours{batteryDesignCapacityUah};
		hardware.Battery.ChargeTerminationCurrent = PiSubmarine::Max17261::MicroAmperes{batteryChargeTerminationUa};
		hardware.Battery.EmptyVoltage = PiSubmarine::Max17261::MicroVolts{batteryEmptyVoltageUv};
		if (!videoSourceDescription.empty())
		{
			config.VideoController.VideoSource = PiSubmarine::Video::Server::GStreamer::ElementSource{
				.Description = videoSourceDescription
			};
		}

		PiSubmarine::I2C::Linux::Driver i2cDriver(hardware.I2cDevice);
		PiSubmarine::Chipset::Client::I2C::Client chipsetClient(i2cDriver);
		PiSubmarine::Max17261::Device batteryGauge(i2cDriver);
		BatteryStore batteryStore(hardware.BatteryStorePath);

		PiSubmarine::GPIO::Linux::Driver gpioDriver("PiSubmarine.Drone.Server.RPi.App");
		auto thrustersPinGroup = gpioDriver.CreatePinGroup(
			"Thrusters",
			hardware.GpioChip,
			{hardware.ThrustersNSleepPin, hardware.ThrustersNFaultPin});
		auto lampsAndBallastPinGroup = gpioDriver.CreatePinGroup(
			"LampsAndBallast",
			hardware.GpioChip,
			{hardware.LampsAndBallastNSleepPin, hardware.LampsAndBallastNFaultPin});

		PiSubmarine::SPI::Linux::Driver thrustersSpiDriver(
			hardware.ThrustersSpiDevice.string(),
			hardware.SpiSpeed,
			8,
			SPI_MODE_1,
			SPI_MODE_1);
		PiSubmarine::SPI::Linux::Driver lampsAndBallastSpiDriver(
			hardware.LampsAndBallastSpiDevice.string(),
			hardware.SpiSpeed,
			8,
			SPI_MODE_1,
			SPI_MODE_1);

		PiSubmarine::Drv8908::Device thrusterChip(thrustersSpiDriver, *thrustersPinGroup);
		PiSubmarine::Drv8908::PowerManager thrusterPowerManager(thrusterChip);
		PiSubmarine::Drv8908::Device lampsAndBallastChip(lampsAndBallastSpiDriver, *lampsAndBallastPinGroup);
		PiSubmarine::Drv8908::PowerManager lampsAndBallastPowerManager(lampsAndBallastChip);

		PiSubmarine::PWM::Linux::Driver pwmDriver(hardware.PwmChannel, std::chrono::milliseconds(10), 100);

		PiSubmarine::Ballast::Telemetry::Chipset::Provider ballastTelemetryProvider(chipsetClient);
		PiSubmarine::Motor::Bidirectional::Drv8908::Controller ballastMotor(
			lampsAndBallastChip,
			lampsAndBallastPowerManager,
			PiSubmarine::Drv8908::PwmGenerator::PwmGenerator1,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge1 | PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge2,
			PiSubmarine::Motor::Drv8908::BridgeSide::High,
			hardware.BallastMotor);
		PiSubmarine::Drone::Server::LoggerFactory loggerFactory;

		PiSubmarine::Battery::Motherboard::Provider batteryProvider(
			batteryGauge,
			chipsetClient,
			batteryStore,
			loggerFactory,
			hardware.Battery);
		const auto batteryInitResult = batteryProvider.Initialize();
		if (!batteryInitResult.has_value())
		{
			SPDLOG_LOGGER_CRITICAL(logger, "Battery provider initialization failed: {}",
			                    ToString(batteryInitResult.error()));
			return 1;
		}

		PiSubmarine::Motor::Unidirectional::Drv8908::Controller frontLeftMotor(
			thrusterChip,
			thrusterPowerManager,
			PiSubmarine::Drv8908::PwmGenerator::PwmGenerator3,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge1 | PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge2,
			PiSubmarine::Motor::Drv8908::BridgeSide::High,
			hardware.ThrusterMotor);
		PiSubmarine::Motor::Unidirectional::Drv8908::Controller frontRightMotor(
			thrusterChip,
			thrusterPowerManager,
			PiSubmarine::Drv8908::PwmGenerator::PwmGenerator1,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge3 | PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge4,
			PiSubmarine::Motor::Drv8908::BridgeSide::High,
			hardware.ThrusterMotor);
		PiSubmarine::Motor::Unidirectional::Drv8908::Controller rearLeftMotor(
			thrusterChip,
			thrusterPowerManager,
			PiSubmarine::Drv8908::PwmGenerator::PwmGenerator4,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge5 | PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge6,
			PiSubmarine::Motor::Drv8908::BridgeSide::High,
			hardware.ThrusterMotor);
		PiSubmarine::Motor::Unidirectional::Drv8908::Controller rearRightMotor(
			thrusterChip,
			thrusterPowerManager,
			PiSubmarine::Drv8908::PwmGenerator::PwmGenerator2,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge7 | PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge8,
			PiSubmarine::Motor::Drv8908::BridgeSide::High,
			hardware.ThrusterMotor);

		PiSubmarine::Lamp::Drv8908::Controller lampController(
			lampsAndBallastChip,
			lampsAndBallastPowerManager,
			PiSubmarine::Drv8908::PwmGenerator::PwmGenerator5,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge5,
			PiSubmarine::Lamp::Drv8908::SwitchSide::High);
		ControlLampAdapter controlLampAdapter(lampController, lampController);
		PiSubmarine::Servo::SG90::Controller servoController(pwmDriver);
		DepthProvider depthProvider;
		ProximityProvider proximityProvider;

		Runtime runtime(
			config,
			Runtime::Dependencies{
				.BallastTelemetryProvider = ballastTelemetryProvider,
				.BatteryTelemetryProvider = batteryProvider,
				.BallastMotorController = ballastMotor,
				.BallastMotorTelemetryProvider = ballastMotor,
				.FrontLeftMotorController = frontLeftMotor,
				.FrontLeftMotorTelemetryProvider = frontLeftMotor,
				.FrontRightMotorController = frontRightMotor,
				.FrontRightMotorTelemetryProvider = frontRightMotor,
				.RearLeftMotorController = rearLeftMotor,
				.RearLeftMotorTelemetryProvider = rearLeftMotor,
				.RearRightMotorController = rearRightMotor,
				.RearRightMotorTelemetryProvider = rearRightMotor,
				.DepthTelemetryProvider = depthProvider,
				.LampController = controlLampAdapter,
				.LampTelemetryProvider = controlLampAdapter,
				.ProximityTelemetryProvider = proximityProvider,
				.ServoController = servoController,
				.PlatformTickables = {
					chipsetClient,
					batteryProvider,
					ballastMotor,
					frontLeftMotor,
					frontRightMotor,
					rearLeftMotor,
					rearRightMotor
				}
			});

		const auto runResult = runtime.Run();
		if (!runResult.has_value())
		{
			SPDLOG_LOGGER_CRITICAL(logger, "{}", ToString(runResult.error()));
			return 1;
		}

		return 0;
	}
	catch (const std::exception& exception)
	{
		SPDLOG_LOGGER_CRITICAL(logger, "{}", exception.what());
		return 1;
	}
}

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
#include "PiSubmarine/Drone/Server/RPi/Config.h"
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
#include "PiSubmarine/Video/Server/GStreamer/Head.h"
#include "PiSubmarine/Video/Subscription/Api/Endpoint.h"

namespace PiSubmarine::Drone::Server::RPi
{
	namespace
	{
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

		[[nodiscard]] std::string ReadTextFile(const std::filesystem::path& path)
		{
			std::ifstream stream(path, std::ios::binary);
			if (!stream.is_open())
			{
				throw std::runtime_error("Failed to open file: " + path.string());
			}

			return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
		}

		void WriteTextFile(const std::filesystem::path& path, const std::string& text)
		{
			std::ofstream stream(path, std::ios::binary | std::ios::trunc);
			if (!stream.is_open())
			{
				throw std::runtime_error("Failed to open file for writing: " + path.string());
			}

			stream << text;
			if (!stream)
			{
				throw std::runtime_error("Failed to write file: " + path.string());
			}
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

		[[nodiscard]] PiSubmarine::Video::Server::GStreamer::ExternalProcessHead CreateDefaultRpicamVideoHead()
		{
			return PiSubmarine::Video::Server::GStreamer::ExternalProcessHead{
				.Executable = "rpicam-vid",
				.Arguments = {
					"--timeout",
					"0",
					"--nopreview",
					"--flush",
					"--inline",
					"--codec",
					"h264",
					"--width",
					"1280",
					"--height",
					"720",
					"--framerate",
					"30",
					"--bitrate",
					"1000000",
					"-o",
					"-"
				}
			};
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
		Config config;
		std::filesystem::path configWritePath;
		double ballastControlPositionDeadband = static_cast<double>(config.BallastControl.PositionDeadband);
		double ballastControlMaxDutyCycle = static_cast<double>(config.BallastControl.MaxDutyCycle);
		double verticalControlDepthDeadbandMeters = config.VerticalControl.DepthDeadband.Value;
		double verticalControlMaximumBallastCorrection =
			static_cast<double>(config.VerticalControl.MaximumBallastCorrection);
		double verticalControlInitialEquilibriumBallastFill =
			static_cast<double>(config.VerticalControl.InitialEquilibriumBallastFill);
		double gimbalNeutralServoAngleDegrees = config.GimbalNeutralServoAngle.Value;
		double minimumGimbalPitchDegrees = config.MinimumGimbalPitch.Value;
		double maximumGimbalPitchDegrees = config.MaximumGimbalPitch.Value;

		CLI::App app{"PiSubmarine Raspberry Pi drone server"};
		app.set_config("--config", "", "Read a TOML configuration file");
		app.add_option("--config-write", configWritePath, "Write the current configuration to a TOML file and exit")
		   ->configurable(false);
		app.add_option("--grpc-address", config.GrpcAddress, "Shared gRPC bind address")->default_val(config.GrpcAddress);
		app.add_option("--server-cert", config.ServerCertificatePath, "PEM server certificate chain file");
		app.add_option("--server-key", config.ServerPrivateKeyPath, "PEM server private key file");
		app.add_option("--client-ca", config.ClientCertificateAuthorityPath, "PEM client certificate authority file");
		app.add_option("--control-address", config.ControlAddress, "Control UDP bind address")->default_val(
			config.ControlAddress);
		app.add_option("--telemetry-address", config.TelemetryAddress, "Telemetry UDP bind address")->default_val(
			config.TelemetryAddress);
		app.add_option("--video-resource-id", config.VideoResourceId, "Lease resource id used for video streaming")->
		    default_val(config.VideoResourceId);
		app.add_option("--video-source", config.VideoSourceDescription,
		               "Optional explicit GStreamer source element description. Leave empty for autodetect.");
		app.add_option("--startup-video-endpoint", config.StartupVideoEndpoint,
		               "Optional host:port RTP endpoint to subscribe immediately at startup.");
		app.add_flag("--startup-video-enable", config.StartupVideoEnable,
		             "Enable video streaming target at startup using low-latency profile and autofocus.");
		app.add_option("--tick-period-ms", config.TickPeriodMilliseconds, "Tick period in milliseconds")->default_val(
			config.TickPeriodMilliseconds);
		app.add_option("--receive-queue-capacity", config.ReceiveQueueCapacity, "Per-socket receive queue capacity")
		   ->default_val(config.ReceiveQueueCapacity);
		app.add_option("--max-datagram-size", config.MaxDatagramSize, "Maximum UDP datagram size in bytes")
		   ->default_val(config.MaxDatagramSize);

		app.add_option("--i2c-device", config.I2cDevice, "Linux I2C device used for chipset and battery monitor")
		   ->default_val(config.I2cDevice.string());
		app.add_option("--thrusters-spi-device", config.ThrustersSpiDevice, "SPI device for the thrusters DRV8908")
		   ->default_val(config.ThrustersSpiDevice.string());
		app.add_option("--lamps-ballast-spi-device", config.LampsAndBallastSpiDevice,
		               "SPI device for the lamps and ballast DRV8908")
		   ->default_val(config.LampsAndBallastSpiDevice.string());
		app.add_option("--gpio-chip", config.GpioChip, "GPIO chip path for DRV8908 control pins")
		   ->default_val(config.GpioChip.string());
		app.add_option("--pwm-channel", config.PwmChannel, "Linux PWM sysfs channel path used for the gimbal servo")
		   ->default_val(config.PwmChannel.string());
		app.add_option("--battery-store", config.BatteryStorePath,
		               "Persistent file path for MAX17261 learning parameters")
		   ->default_val(config.BatteryStorePath.string());
		app.add_option("--thrusters-n-sleep-pin", config.ThrustersNSleepPin,
		               "GPIO line connected to thrusters DRV8908 nSLEEP")
		   ->default_val(config.ThrustersNSleepPin);
		app.add_option("--thrusters-n-fault-pin", config.ThrustersNFaultPin,
		               "GPIO line connected to thrusters DRV8908 nFAULT")
		   ->default_val(config.ThrustersNFaultPin);
		app.add_option("--lamps-ballast-n-sleep-pin", config.LampsAndBallastNSleepPin,
		               "GPIO line connected to lamps/ballast DRV8908 nSLEEP")
		   ->default_val(config.LampsAndBallastNSleepPin);
		app.add_option("--lamps-ballast-n-fault-pin", config.LampsAndBallastNFaultPin,
		               "GPIO line connected to lamps/ballast DRV8908 nFAULT")
		   ->default_val(config.LampsAndBallastNFaultPin);
		app.add_option("--spi-speed-hz", config.SpiSpeed, "SPI bus speed for both DRV8908 devices")->default_val(
			config.SpiSpeed);
		app.add_option("--battery-design-capacity-uah", config.BatteryDesignCapacityUah,
		               "MAX17261 design capacity in microampere-hours")
		   ->default_val(config.BatteryDesignCapacityUah);
		app.add_option("--battery-charge-termination-ua", config.BatteryChargeTerminationUa,
		               "MAX17261 charge termination current in microamperes")
		   ->default_val(config.BatteryChargeTerminationUa);
		app.add_option("--battery-empty-voltage-uv", config.BatteryEmptyVoltageUv,
		               "MAX17261 battery empty voltage in microvolts")
		   ->default_val(config.BatteryEmptyVoltageUv);
		app.add_flag("--battery-force-gauge-reset", config.BatteryForceGaugeReset,
		             "Force MAX17261 gauge reset during startup.");
		app.add_option("--ballast-control-proportional-gain", config.BallastControl.ProportionalGain,
		               "Ballast position controller proportional gain")
		   ->default_val(config.BallastControl.ProportionalGain);
		app.add_option("--ballast-control-integral-gain-per-second", config.BallastControl.IntegralGainPerSecond,
		               "Ballast position controller integral gain per second")
		   ->default_val(config.BallastControl.IntegralGainPerSecond);
		app.add_option("--ballast-control-integral-limit", config.BallastControl.IntegralLimit,
		               "Ballast position controller symmetric integral limit")
		   ->default_val(config.BallastControl.IntegralLimit);
		app.add_option("--ballast-control-position-deadband", ballastControlPositionDeadband,
		               "Ballast position controller deadband as normalized fraction")
		   ->default_val(ballastControlPositionDeadband);
		app.add_option("--ballast-control-max-duty-cycle", ballastControlMaxDutyCycle,
		               "Ballast position controller maximum absolute motor duty cycle")
		   ->default_val(ballastControlMaxDutyCycle);
		app.add_option("--vertical-control-proportional-gain", config.VerticalControl.ProportionalGain,
		               "Vertical depth controller proportional gain")
		   ->default_val(config.VerticalControl.ProportionalGain);
		app.add_option("--vertical-control-integral-gain-per-second", config.VerticalControl.IntegralGainPerSecond,
		               "Vertical depth controller integral gain per second")
		   ->default_val(config.VerticalControl.IntegralGainPerSecond);
		app.add_option("--vertical-control-derivative-gain-seconds", config.VerticalControl.DerivativeGainSeconds,
		               "Vertical depth controller derivative gain in seconds")
		   ->default_val(config.VerticalControl.DerivativeGainSeconds);
		app.add_option("--vertical-control-integral-limit-meters-seconds",
		               config.VerticalControl.IntegralLimitMetersSeconds,
		               "Vertical depth controller symmetric integral limit in meter-seconds")
		   ->default_val(config.VerticalControl.IntegralLimitMetersSeconds);
		app.add_option("--vertical-control-depth-deadband-m", verticalControlDepthDeadbandMeters,
		               "Vertical depth controller deadband in meters")
		   ->default_val(verticalControlDepthDeadbandMeters);
		app.add_option("--vertical-control-maximum-ballast-correction", verticalControlMaximumBallastCorrection,
		               "Vertical depth controller maximum ballast bias correction as normalized fraction")
		   ->default_val(verticalControlMaximumBallastCorrection);
		app.add_option("--vertical-control-initial-equilibrium-ballast-fill",
		               verticalControlInitialEquilibriumBallastFill,
		               "Initial ballast fill guess used by the vertical controller")
		   ->default_val(verticalControlInitialEquilibriumBallastFill);
		app.add_option("--gimbal-neutral-servo-angle-deg", gimbalNeutralServoAngleDegrees,
		               "Servo angle in degrees that corresponds to camera pitch 0 (horizon)")
		   ->default_val(gimbalNeutralServoAngleDegrees);
		app.add_flag("--gimbal-pitch-inverted", config.GimbalPitchInverted,
		             "Invert gimbal pitch direction when translating camera pitch to servo angle.");
		app.add_option("--minimum-gimbal-pitch-deg", minimumGimbalPitchDegrees,
		               "Minimum allowed camera pitch in degrees, relative to horizon")
		   ->default_val(minimumGimbalPitchDegrees);
		app.add_option("--maximum-gimbal-pitch-deg", maximumGimbalPitchDegrees,
		               "Maximum allowed camera pitch in degrees, relative to horizon")
		   ->default_val(maximumGimbalPitchDegrees);

		CLI11_PARSE(app, argc, argv);

		if (!configWritePath.empty())
		{
			WriteTextFile(configWritePath, app.config_to_str(true));
			return 0;
		}

		if (config.ServerCertificatePath.empty())
		{
			SPDLOG_LOGGER_CRITICAL(logger, "Missing --server-cert value.");
			return 2;
		}

		if (config.ServerPrivateKeyPath.empty())
		{
			SPDLOG_LOGGER_CRITICAL(logger, "Missing --server-key value.");
			return 2;
		}

		if (config.ClientCertificateAuthorityPath.empty())
		{
			SPDLOG_LOGGER_CRITICAL(logger, "Missing --client-ca value.");
			return 2;
		}

		config.BallastControl.PositionDeadband = PiSubmarine::NormalizedFraction{ballastControlPositionDeadband};
		config.BallastControl.MaxDutyCycle = PiSubmarine::NormalizedFraction{ballastControlMaxDutyCycle};
		config.VerticalControl.DepthDeadband = PiSubmarine::Meters{verticalControlDepthDeadbandMeters};
		config.VerticalControl.MaximumBallastCorrection =
			PiSubmarine::NormalizedFraction{verticalControlMaximumBallastCorrection};
		config.VerticalControl.InitialEquilibriumBallastFill =
			PiSubmarine::Ballast::BallastFillFraction{
				PiSubmarine::NormalizedFraction{verticalControlInitialEquilibriumBallastFill}};
		config.GimbalNeutralServoAngle = PiSubmarine::Degrees{gimbalNeutralServoAngleDegrees};
		config.MinimumGimbalPitch = PiSubmarine::Degrees{minimumGimbalPitchDegrees};
		config.MaximumGimbalPitch = PiSubmarine::Degrees{maximumGimbalPitchDegrees};

		const auto parsedControlEndpoint = ParseEndpoint(config.ControlAddress);
		if (!parsedControlEndpoint.has_value())
		{
			SPDLOG_LOGGER_CRITICAL(logger, "Invalid --control-address value. Expected host:port.");
			return 2;
		}

		const auto parsedTelemetryEndpoint = ParseEndpoint(config.TelemetryAddress);
		if (!parsedTelemetryEndpoint.has_value())
		{
			SPDLOG_LOGGER_CRITICAL(logger, "Invalid --telemetry-address value. Expected host:port.");
			return 2;
		}

		std::optional<PiSubmarine::Video::Subscription::Api::Endpoint> parsedStartupVideoEndpoint;
		if (!config.StartupVideoEndpoint.empty())
		{
			const auto parsedEndpoint = ParseEndpoint(config.StartupVideoEndpoint);
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

		PiSubmarine::Drone::Server::Config runtimeConfig;
		runtimeConfig.ControlEndpoint = *parsedControlEndpoint;
		runtimeConfig.TelemetryEndpoint = *parsedTelemetryEndpoint;
		runtimeConfig.StartupVideoEndpoint = parsedStartupVideoEndpoint;
		runtimeConfig.StartupVideoEnable = config.StartupVideoEnable;
		runtimeConfig.TickPeriod = std::chrono::milliseconds(config.TickPeriodMilliseconds);
		runtimeConfig.ReceiveQueueCapacity = config.ReceiveQueueCapacity;
		runtimeConfig.MaxDatagramSize = config.MaxDatagramSize;
		runtimeConfig.BallastControl = config.BallastControl;
		runtimeConfig.VerticalControl = config.VerticalControl;
		runtimeConfig.Gimbal.NeutralServoAngle = config.GimbalNeutralServoAngle.ToRadians();
		runtimeConfig.Gimbal.IsPitchInverted = config.GimbalPitchInverted;
		runtimeConfig.Gimbal.MinimumPitch = config.MinimumGimbalPitch.ToRadians();
		runtimeConfig.Gimbal.MaximumPitch = config.MaximumGimbalPitch.ToRadians();
		runtimeConfig.GrpcServer.Address = config.GrpcAddress;
		runtimeConfig.GrpcServer.ServerCertificateChain = ReadTextFile(config.ServerCertificatePath);
		runtimeConfig.GrpcServer.ServerPrivateKey = ReadTextFile(config.ServerPrivateKeyPath);
		runtimeConfig.GrpcServer.ClientCertificateAuthority = ReadTextFile(config.ClientCertificateAuthorityPath);
		runtimeConfig.VideoController.ResourceId = PiSubmarine::Lease::Api::ResourceId{.Value = config.VideoResourceId};
		runtimeConfig.VideoController.VideoHead = CreateDefaultRpicamVideoHead();
		if (!config.VideoSourceDescription.empty())
		{
			runtimeConfig.VideoController.VideoHead = PiSubmarine::Video::Server::GStreamer::AutoDetectPipelineHead{
				.VideoSource = PiSubmarine::Video::Server::GStreamer::ElementSource{
					.Description = config.VideoSourceDescription
				}
			};
		}

		const auto batteryConfig = config.CreateBatteryConfig();

		PiSubmarine::I2C::Linux::Driver i2cDriver(config.I2cDevice);
		PiSubmarine::Chipset::Client::I2C::Client chipsetClient(i2cDriver);
		PiSubmarine::Max17261::Device batteryGauge(i2cDriver);
		BatteryStore batteryStore(config.BatteryStorePath);

		PiSubmarine::GPIO::Linux::Driver gpioDriver("PiSubmarine.Drone.Server.RPi.App");
		auto thrustersPinGroup = gpioDriver.CreatePinGroup(
			"Thrusters",
			config.GpioChip,
			{config.ThrustersNSleepPin, config.ThrustersNFaultPin});
		auto lampsAndBallastPinGroup = gpioDriver.CreatePinGroup(
			"LampsAndBallast",
			config.GpioChip,
			{config.LampsAndBallastNSleepPin, config.LampsAndBallastNFaultPin});

		PiSubmarine::SPI::Linux::Driver thrustersSpiDriver(
			config.ThrustersSpiDevice.string(),
			config.SpiSpeed,
			8,
			SPI_MODE_1,
			SPI_MODE_1);
		PiSubmarine::SPI::Linux::Driver lampsAndBallastSpiDriver(
			config.LampsAndBallastSpiDevice.string(),
			config.SpiSpeed,
			8,
			SPI_MODE_1,
			SPI_MODE_1);

		PiSubmarine::Drv8908::Device thrusterChip(thrustersSpiDriver, *thrustersPinGroup);
		PiSubmarine::Drv8908::PowerManager thrusterPowerManager(thrusterChip);
		PiSubmarine::Drv8908::Device lampsAndBallastChip(lampsAndBallastSpiDriver, *lampsAndBallastPinGroup);
		PiSubmarine::Drv8908::PowerManager lampsAndBallastPowerManager(lampsAndBallastChip);

		PiSubmarine::PWM::Linux::Driver pwmDriver(config.PwmChannel, std::chrono::milliseconds(10), 100);

		PiSubmarine::Ballast::Telemetry::Chipset::Provider ballastTelemetryProvider(chipsetClient);
		PiSubmarine::Motor::Bidirectional::Drv8908::Controller ballastMotor(
			lampsAndBallastChip,
			lampsAndBallastPowerManager,
			PiSubmarine::Drv8908::PwmGenerator::PwmGenerator1,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge1 | PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge2,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge3 | PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge4,
			config.BallastMotor);
		PiSubmarine::Drone::Server::LoggerFactory loggerFactory;

		PiSubmarine::Battery::Motherboard::Provider batteryProvider(
			batteryGauge,
			chipsetClient,
			batteryStore,
			loggerFactory,
			batteryConfig);
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
			config.ThrusterMotor);
		PiSubmarine::Motor::Unidirectional::Drv8908::Controller frontRightMotor(
			thrusterChip,
			thrusterPowerManager,
			PiSubmarine::Drv8908::PwmGenerator::PwmGenerator1,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge3 | PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge4,
			PiSubmarine::Motor::Drv8908::BridgeSide::High,
			config.ThrusterMotor);
		PiSubmarine::Motor::Unidirectional::Drv8908::Controller rearLeftMotor(
			thrusterChip,
			thrusterPowerManager,
			PiSubmarine::Drv8908::PwmGenerator::PwmGenerator4,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge5 | PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge6,
			PiSubmarine::Motor::Drv8908::BridgeSide::High,
			config.ThrusterMotor);
		PiSubmarine::Motor::Unidirectional::Drv8908::Controller rearRightMotor(
			thrusterChip,
			thrusterPowerManager,
			PiSubmarine::Drv8908::PwmGenerator::PwmGenerator2,
			PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge7 | PiSubmarine::Drv8908::HalfBridgeBitMask::HalfBridge8,
			PiSubmarine::Motor::Drv8908::BridgeSide::High,
			config.ThrusterMotor);

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
			runtimeConfig,
			PiSubmarine::Drone::Server::Dependencies{
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

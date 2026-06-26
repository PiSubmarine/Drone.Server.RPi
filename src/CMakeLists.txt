include("${PISUBMARINE_BUILD_SYSTEM}")

message("Adding library: ${PISUBMARINE_MODULE_NAME}")
add_library(${PISUBMARINE_MODULE_NAME}
        PiSubmarine/Drone/Server/RPi/BatteryStore.cpp
        PiSubmarine/Drone/Server/RPi/BatteryStore.h
        PiSubmarine/Drone/Server/RPi/ControlLampAdapter.cpp
        PiSubmarine/Drone/Server/RPi/ControlLampAdapter.h
        PiSubmarine/Drone/Server/RPi/DepthProvider.cpp
        PiSubmarine/Drone/Server/RPi/DepthProvider.h
        PiSubmarine/Drone/Server/RPi/ProximityProvider.cpp
        PiSubmarine/Drone/Server/RPi/ProximityProvider.h)
target_include_directories(${PISUBMARINE_MODULE_NAME}
        PUBLIC "../public"
        PRIVATE "./")
PiSubmarineInitTarget(${PISUBMARINE_MODULE_NAME})

find_package(spdlog CONFIG REQUIRED)
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC spdlog::spdlog)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Battery.Persistence.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Battery.Persistence.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Control.Lamp.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Control.Lamp.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Depth.Telemetry.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Depth.Telemetry.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Drone.Server.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Drone.Server)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Error.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Error.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Lamp.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Lamp.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Lamp.Telemetry.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Lamp.Telemetry.Api)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Proximity.Telemetry.Api.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME} PUBLIC PiSubmarine.Proximity.Telemetry.Api)

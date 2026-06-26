include("${PISUBMARINE_BUILD_SYSTEM}")

project(${PISUBMARINE_MODULE_NAME}.App LANGUAGES CXX)

message("Adding executable: ${PISUBMARINE_MODULE_NAME}.App")
add_executable(${PISUBMARINE_MODULE_NAME}.App PiSubmarine/Drone/Server/RPi/main.cpp)
PiSubmarineInitTarget(${PISUBMARINE_MODULE_NAME}.App)

target_include_directories(${PISUBMARINE_MODULE_NAME}.App PRIVATE "./" "../src")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE ${PISUBMARINE_MODULE_NAME})

find_package(CLI11 CONFIG REQUIRED)
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE CLI11::CLI11)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Ballast.Telemetry.Chipset.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.Ballast.Telemetry.Chipset)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Battery.Motherboard.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.Battery.Motherboard)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Chipset.Client.I2C.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.Chipset.Client.I2C)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Drv8908.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.Drv8908)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Drv8908.PowerManager.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.Drv8908.PowerManager)

PiSubmarineAddDependency("https://github.com/PiSubmarine/GPIO.Linux.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.GPIO.Linux)

PiSubmarineAddDependency("https://github.com/PiSubmarine/I2C.Linux.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.I2C.Linux)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Lamp.Drv8908.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.Lamp.Drv8908)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Max17261.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.Max17261)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Motor.Drv8908.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.Motor.Drv8908)

PiSubmarineAddDependency("https://github.com/PiSubmarine/PWM.Linux.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.PWM.Linux)

PiSubmarineAddDependency("https://github.com/PiSubmarine/Servo.SG90.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.Servo.SG90)

PiSubmarineAddDependency("https://github.com/PiSubmarine/SPI.Linux.git" "")
target_link_libraries(${PISUBMARINE_MODULE_NAME}.App PRIVATE PiSubmarine.SPI.Linux)

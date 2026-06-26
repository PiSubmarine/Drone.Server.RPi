cmake_minimum_required(VERSION 3.30)

cmake_policy(SET CMP0168 NEW)

include(FetchContent)

FetchContent_Declare(
        PiSubmarineBuild
        GIT_REPOSITORY "https://github.com/PiSubmarine/Build.CMake.git"
        GIT_TAG        "main"
)

FetchContent_MakeAvailable(PiSubmarineBuild)

set(PISUBMARINE_BUILD_SYSTEM "${pisubmarinebuild_SOURCE_DIR}/PiSubmarine.Build.CMake.cmake")
set(_build_cmake_local "${pisubmarinebuild_SOURCE_DIR}/PiSubmarine.Build.CMake.cmake")

include("${pisubmarinebuild_SOURCE_DIR}/PiSubmarine.Build.CMake.cmake")

PiSubmarineInitModule("PiSubmarine.Drone.Server.RPi")
project(${PISUBMARINE_MODULE_NAME})

PiSubmarineAddDependency("https://github.com/PiSubmarine/Gstreamer.Build.git" "main")
PiSubmarineConfigureModule()

enable_testing()

if(TARGET ${PISUBMARINE_MODULE_NAME}.App)
    PiSubmarineGstreamerFinalizeCompositionRoot(${PISUBMARINE_MODULE_NAME}.App)
endif()

cmake_minimum_required(VERSION 3.16)
project(ksystemstats_plugin_ryzenpower)

set(CMAKE_CXX_STANDARD 17)

find_package(ECM 6.0.0 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(KF6 REQUIRED COMPONENTS CoreAddons)
find_package(KSysGuard REQUIRED)

kcoreaddons_add_plugin(ksystemstats_plugin_ryzenpower
    SOURCES ryzen_power_plugin.cpp ryzen_power_plugin.h
    INSTALL_NAMESPACE "ksystemstats"
)

target_link_libraries(ksystemstats_plugin_ryzenpower
    Qt6::Core
    KF6::CoreAddons
    KSysGuard::SystemStats
    KSysGuard::Formatter
)

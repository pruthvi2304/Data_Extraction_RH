cmake_minimum_required(VERSION 3.20)

project(DataExtractionQt VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(MSVC)
    add_compile_options(/Zc:__cplusplus /permissive-)
    add_definitions(-D_WIN32_WINNT=0x0601)
endif()

if(NOT DEFINED Qt6_DIR AND EXISTS "C:/Qt/6.11.0/mingw_64/lib/cmake/Qt6")
    set(Qt6_DIR "C:/Qt/6.11.0/mingw_64/lib/cmake/Qt6" CACHE PATH "Qt6 package directory")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Charts Network Sql)

find_path(NATS_INCLUDE_DIR
    NAMES nats.h
    PATHS ${NATS_DIR}/src
    PATH_SUFFIXES include src
)

find_library(NATS_LIBRARY NAMES nats
    PATHS
        ${NATS_DIR}/build/lib/Release
        ${NATS_DIR}/build/lib/Debug
        ${NATS_DIR}/lib
        ${NATS_DIR}/build/src
    PATH_SUFFIXES lib
)

set(HAVE_NATS_DEPENDENCIES OFF)
if(NATS_INCLUDE_DIR AND EXISTS "${NATS_INCLUDE_DIR}" AND NATS_LIBRARY)
    set(HAVE_NATS_DEPENDENCIES ON)
    message(STATUS "Found NATS: Include=${NATS_INCLUDE_DIR}, Library=${NATS_LIBRARY}")
else()
    message(WARNING "NATS not found or incomplete. Set -DNATS_DIR=<path-to-nats.c> to enable NATS support.")
    if(NOT NATS_INCLUDE_DIR OR NOT EXISTS "${NATS_INCLUDE_DIR}")
        message(WARNING "  - Include path not found: ${NATS_INCLUDE_DIR}")
    endif()
    if(NOT NATS_LIBRARY)
        message(WARNING "  - Library not found. Searched in build/lib/Release, build/lib/Debug")
    endif()
endif()

add_executable(data_extraction_dashboard
    ui/include/application_runtime.hpp
    ui/include/central_event_store.hpp
    ui/include/dashboardwidget.hpp
    ui/include/devicetablemodel.hpp
    ui/include/mainwindow.hpp
    ui/include/metricswidget.hpp
    ui/include/modbus_device_worker.hpp
    ui/include/nats_publisher_service.hpp
    ui/include/natsstatuswidget.hpp
    ui/include/sensorchartwidget.hpp
    ui/include/sensormodel.hpp
    ui/include/ui_config.hpp
    ui/include/ui_config_loader.hpp
    ui/include/ui_file_logger.hpp
    ui/include/timestamp_utils.hpp
    ui/src/application_runtime.cpp
    ui/src/central_event_store.cpp
    ui/src/dashboard_main.cpp
    ui/src/dashboardwidget.cpp
    ui/src/devicetablemodel.cpp
    ui/src/json_parser.cpp
    ui/src/mainwindow.cpp
    ui/src/metricswidget.cpp
    ui/src/modbus_device_worker.cpp
    ui/src/nats_publisher_service.cpp
    ui/src/natsstatuswidget.cpp
    ui/src/sensormodel.cpp
    ui/src/sensorchartwidget.cpp
    ui/src/ui_config_loader.cpp
    ui/src/ui_file_logger.cpp
    ui/src/timestamp_utils.cpp
    ui/resources/dashboard.qrc
)

target_include_directories(data_extraction_dashboard
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/ui/include
)

target_link_libraries(data_extraction_dashboard
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Charts
        Qt6::Network
        Qt6::Sql
        ws2_32
)

if(HAVE_NATS_DEPENDENCIES)
    target_include_directories(data_extraction_dashboard PRIVATE ${NATS_INCLUDE_DIR})
    target_link_libraries(data_extraction_dashboard PRIVATE ${NATS_LIBRARY})
    target_compile_definitions(data_extraction_dashboard PRIVATE HAVE_NATS=1)
endif()

qt_finalize_executable(data_extraction_dashboard)

# Keep the certificate tree beside the built executable so configured paths such
# as certs/publisher/client.crt and certs/nats/ca.crt resolve in build output.
add_custom_command(TARGET data_extraction_dashboard POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/certs"
        "$<TARGET_FILE_DIR:data_extraction_dashboard>/certs"
    COMMENT "Copying NATS certificates to the build output"
)

if(WIN32)
    get_filename_component(QT_DEPLOY_PREFIX "${Qt6_DIR}" DIRECTORY)
    get_filename_component(QT_DEPLOY_PREFIX "${QT_DEPLOY_PREFIX}" DIRECTORY)
    get_filename_component(QT_DEPLOY_PREFIX "${QT_DEPLOY_PREFIX}" DIRECTORY)
    set(QT_WINDEPLOYQT_EXECUTABLE "${QT_DEPLOY_PREFIX}/bin/windeployqt.exe")

    if(EXISTS "${QT_WINDEPLOYQT_EXECUTABLE}")
        add_custom_command(TARGET data_extraction_dashboard POST_BUILD
            COMMAND "${QT_WINDEPLOYQT_EXECUTABLE}"
                --dir "$<TARGET_FILE_DIR:data_extraction_dashboard>"
                "$<TARGET_FILE:data_extraction_dashboard>"
            COMMENT "Deploying Qt runtime for data_extraction_dashboard"
        )
    endif()
endif()

# SPDX-FileCopyrightText: 2025 Open Mobile Platform LLC <community@omp.ru>
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.5)
project(ru.auroraos.GigaAsrDemo CXX)

find_package(Qt5 COMPONENTS Core Network Qml Gui Quick Multimedia DBus LinguistTools REQUIRED)
find_package(OpenMP REQUIRED)

include(FindPkgConfig)
pkg_search_module(AURORA auroraapp REQUIRED)
pkg_search_module(AURORA auroraapp_i18n REQUIRED)
pkg_check_modules(LLAMACPP REQUIRED IMPORTED_TARGET llama-cpp)

set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

pkg_check_modules(onnxruntime REQUIRED IMPORTED_TARGET onnxruntime)

set(short_name GigaAsrDemo)

file(GLOB TsFiles "translations/*.ts")
qt5_add_translation(QmFiles ${TsFiles})

set(SOURCES
    src/main.cpp
    src/asrworker.cpp
    src/commandmodelmanager.cpp
    src/commandworker.cpp
    src/functioncallengine.cpp
    src/gigaasrrunner.cpp
    src/rnntrecognizer.cpp
    src/toolexecutor.cpp
)

set(CMAKE_SKIP_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/share/${PROJECT_NAME}/lib")

add_executable(${PROJECT_NAME} ${SOURCES} ${QmFiles})
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>
)
target_include_directories(${PROJECT_NAME} PRIVATE
    $<BUILD_INTERFACE:
    ${AURORA_INCLUDE_DIRS}
    PkgConfig::onnxruntime
>)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt5::Quick
    Qt5::Multimedia
    Qt5::DBus
    ${AURORA_LDFLAGS}
    PkgConfig::onnxruntime
    PkgConfig::LLAMACPP
    OpenMP::OpenMP_CXX
)

install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY qml
    DESTINATION share/${PROJECT_NAME}
)

install(DIRECTORY models
    DESTINATION share/${PROJECT_NAME}
)

foreach(_file IN LISTS QmFiles)
    get_filename_component(_filename "${_file}" NAME)
    string(REPLACE "${short_name}" "${PROJECT_NAME}" _full_filename "${_filename}")
    install(FILES ${_file} DESTINATION share/${PROJECT_NAME}/translations RENAME ${_full_filename})
endforeach()

set(IconSize 86x86 108x108 128x128 172x172)
foreach(_size IN LISTS IconSize)
    install(FILES icons/${_size}/${PROJECT_NAME}.png DESTINATION share/icons/hicolor/${_size}/apps)
endforeach()

install(FILES ${PROJECT_NAME}.desktop
    DESTINATION share/applications
)

# Get the other files reachable from the project tree in Qt Creator
file(GLOB_RECURSE IconFiles "icons" "*.png")
set(RESOURCE_FILES ${IconFiles})

file(GLOB_RECURSE QmlFiles "qml/*.qml")

add_custom_target(distfiles
    SOURCES
        ${PROJECT_NAME}.desktop
        rpm/${PROJECT_NAME}.spec
        rpm/${PROJECT_NAME}.changes.in
        rpm/${PROJECT_NAME}.changes.run.in
        ${QmlFiles}
        ${TsFiles}
        ${IconFiles})

file(WRITE "${CMAKE_BINARY_DIR}/QtCreatorDeployment.txt"
    "${CMAKE_INSTALL_PREFIX}
${CMAKE_BINARY_DIR}/${PROJECT_NAME}:bin
")

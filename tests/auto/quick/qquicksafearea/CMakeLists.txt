# Copyright (C) 2024 The Qt Company Ltd.
# SPDX-License-Identifier: BSD-3-Clause

if(NOT QT_BUILD_STANDALONE_TESTS AND NOT QT_BUILDING_QT)
    cmake_minimum_required(VERSION 3.16)
    project(tst_qquicksafearea LANGUAGES CXX)
    find_package(Qt6BuildInternals REQUIRED COMPONENTS STANDALONE_TEST)
endif()

file(GLOB_RECURSE test_data_glob
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    data/*)
list(APPEND test_data ${test_data_glob})

qt_internal_add_test(tst_qquicksafearea
    SOURCES
        tst_qquicksafearea.cpp
    LIBRARIES
        Qt::CorePrivate
        Qt::Gui
        Qt::GuiPrivate
        Qt::Qml
        Qt::QmlPrivate
        Qt::QuickPrivate
        Qt::QuickTestUtilsPrivate
        Qt::QuickTemplates2Private
    TESTDATA ${test_data}
)

if(ANDROID OR IOS)
    set(TESTDATA_DIR ":/data")
else()
    set(TESTDATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()

if(APPLE)
    enable_language(OBJCXX)
    set_source_files_properties(tst_qquicksafearea.cpp PROPERTIES LANGUAGE OBJCXX)
endif()

qt_internal_extend_target(tst_qquicksafearea
    DEFINES
        QT_QMLTEST_DATADIR="${TESTDATA_DIR}"
)

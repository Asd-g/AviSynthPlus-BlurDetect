cmake_minimum_required(VERSION 3.16)

project(BlurDetect LANGUAGES CXX)

set(sources ${CMAKE_CURRENT_SOURCE_DIR}/src/blurdetect.cpp)

if (WIN32)
    set(sources ${sources} ${CMAKE_CURRENT_SOURCE_DIR}/src/blurdetect.rc)
endif()

add_library(BlurDetect SHARED ${sources})

target_include_directories(BlurDetect PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if (UNIX)
    target_include_directories(BlurDetect PRIVATE /usr/local/include/avisynth)
else()
    target_include_directories(BlurDetect PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../AviSynthPlus/avs_core/include")
endif()

if (NOT CMAKE_GENERATOR MATCHES "Visual Studio")
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
    endif()

    string(TOLOWER ${CMAKE_BUILD_TYPE} build_type)
    if (build_type STREQUAL Debug)
        target_compile_definitions(BlurDetect PRIVATE DEBUG_BUILD)
    else (build_type STREQUAL Release)
        target_compile_definitions(BlurDetect PRIVATE RELEASE_BUILD)
    endif()
    
    target_compile_options(BlurDetect PRIVATE $<$<CONFIG:Release>:-s>)

    message(STATUS "Build type - ${CMAKE_BUILD_TYPE}")
endif()

target_compile_features(BlurDetect PRIVATE cxx_std_17)

target_link_libraries(BlurDetect PRIVATE avisynth)

if (MINGW)
    set_target_properties(BlurDetect PROPERTIES PREFIX "")

    target_link_libraries(BlurDetect PRIVATE -static-libstdc++ -static -lwinpthread -s)
endif()

if (UNIX)
    find_package (Git)

    if (GIT_FOUND)
        execute_process (COMMAND ${GIT_EXECUTABLE} describe --tags --abbrev=0
            OUTPUT_VARIABLE ver
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        set_target_properties(BlurDetect PROPERTIES OUTPUT_NAME "blurdetect.${ver}")
    else()
        message (STATUS "GIT not found")
    endif()

    include(GNUInstallDirs)

    INSTALL(TARGETS BlurDetect LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/avisynth")

    # uninstall target
    if(NOT TARGET uninstall)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
        IMMEDIATE @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
    endif()
endif()

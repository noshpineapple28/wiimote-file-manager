# 2009-2011 Ryan Pavlik <rpavlik@iastate.edu>
# http://academic.cleardefinition.com/
# Iowa State University HCI Graduate Program/VRAC

cmake_minimum_required(VERSION 3.0.0)

# Set package properties
project(WiimoteFileManager
    VERSION 0.2.0
    DESCRIPTION "Feature-complete cross-platform Wii Remote access library")

###
# Set up options
###

if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(SUBPROJECT YES)
endif()

option(BUILD_FILE_MANAGER "Should we build the file manager app?" YES)

option(CPACK_MONOLITHIC_INSTALL "Only produce a single component installer, rather than multi-component." NO)

###
# Perform build configuration of dependencies
###

# Locally-developed modules dist'ed with this app - always have this first.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
#include(UseBackportedModules)
include(DoxygenTargets)

if(NOT BUILD_SHARED_LIBS)
    add_definitions(-DWIIUSE_STATIC)
endif()

if(NOT WIN32 AND NOT APPLE)
    set(LINUX YES)
    find_package(Bluez REQUIRED)
    include_directories(${BLUEZ_INCLUDE_DIRS})

    include("GNUInstallDirs")
else()
    set(LINUX NO)
    set(CMAKE_INSTALL_LIBDIR "lib")
endif()

if(WIN32)
    find_package(WinHID REQUIRED)
    include_directories(${WINHID_INCLUDE_DIRS})
    add_definitions(-D_WIN32_WINNT=0x0501)
endif()

###
# Build the project
###

# The lib is in the "src" subdirectory
add_subdirectory(lib)

if(NOT SUBPROJECT)
    # Example apps
    if(BUILD_FILE_MANAGER)
        add_subdirectory(src)
    endif()
endif()

if(SUBPROJECT)
    set(WIIUSE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src" PARENT_SCOPE)
    set(WIIUSE_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src" PARENT_SCOPE)
    set(WIIUSE_LIBRARY wiiuse PARENT_SCOPE)
    set(WIIUSE_LIBRARIES wiiuse PARENT_SCOPE)
    set(WIIUSE_FOUND ON PARENT_SCOPE)
endif()

if(NOT SUBPROJECT)
###
# Set packaging options (for CPack)
###

    if(WIN32)
        set(DOC_DIR .)
    else()
        set(DOC_DIR share/doc/wiiuse)

        configure_file(wiiuse.pc.in
            ${CMAKE_CURRENT_BINARY_DIR}/wiiuse.pc
            @ONLY)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/wiiuse.pc
            DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
    endif()

    # Documentation
    add_doxygen(Doxyfile
        INSTALL_DESTINATION ${DOC_DIR}
        INSTALL_COMPONENT docs
        NO_PDF)

    install(FILES
        CHANGELOG.mkd
        LICENSE
        README.mkd
        DESTINATION ${DOC_DIR} COMPONENT docs)

    if(INSTALL_MANAGER)
        install(FILES src/main.c
            DESTINATION ${DOC_DIR}/src COMPONENT manager)
    endif()

    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.mkd")

    include(GetCompilerInfoString)
    get_compiler_info_string(_compiler)

    set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-${_compiler}")

    # Include the packaging system now that we have it all set up
    include(CPack)

    cpack_add_component(docs HIDDEN)

    cpack_add_component(development
        DISPLAY_NAME "Development Files")

    cpack_add_component(manager
        DISPLAY_NAME "Manager")

    cpack_add_component(runtime
        DISPLAY_NAME "Runtime Library"
        REQUIRED)
endif()

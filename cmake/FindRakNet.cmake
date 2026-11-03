# Locate RakNet/CrabNet or build the copy bundled with ArenaMP.
#
# Result variables:
#   RakNet_FOUND
#   RakNet_INCLUDES
#   RakNet_LIBRARY
#
# Accepted cache variables for externally built CrabNet:
#   RakNet_INCLUDE_DIR or RakNet_INCLUDES
#   RakNet_LIBRARY, or RakNet_LIBRARY_RELEASE/RakNet_LIBRARY_DEBUG

include_guard(GLOBAL)

# Accept the singular variable used by several CI scripts and package managers.
# CrabNet keeps public headers in include/raknet while older RakNet trees kept
# them directly in Source. Normalize both layouts and reject stale paths that
# do not actually contain the public API.
if(RakNet_INCLUDE_DIR AND NOT RakNet_INCLUDES)
    if(EXISTS "${RakNet_INCLUDE_DIR}/RakPeer.h" AND EXISTS "${RakNet_INCLUDE_DIR}/RakNetTypes.h")
        set(RakNet_INCLUDES "${RakNet_INCLUDE_DIR}")
    elseif(EXISTS "${RakNet_INCLUDE_DIR}/raknet/RakPeer.h" AND EXISTS "${RakNet_INCLUDE_DIR}/raknet/RakNetTypes.h")
        set(RakNet_INCLUDES "${RakNet_INCLUDE_DIR}/raknet")
    elseif(EXISTS "${RakNet_INCLUDE_DIR}/../include/raknet/RakPeer.h" AND EXISTS "${RakNet_INCLUDE_DIR}/../include/raknet/RakNetTypes.h")
        get_filename_component(_raknet_source_parent "${RakNet_INCLUDE_DIR}" DIRECTORY)
        set(RakNet_INCLUDES "${_raknet_source_parent}/include/raknet")
    endif()
endif()

# Prefer an explicitly supplied include directory, then look in common layouts.
if(NOT RakNet_INCLUDES)
    find_path(RakNet_INCLUDES
        NAMES RakPeer.h raknet/RakPeer.h
        HINTS
            "${PROJECT_SOURCE_DIR}/extern/raknet/include/raknet"
            "${PROJECT_SOURCE_DIR}/extern/raknet/include"
            "${PROJECT_SOURCE_DIR}/extern/raknet/Source"
        PATH_SUFFIXES raknet
    )
endif()

# Respect an explicitly supplied library first.
if(NOT RakNet_LIBRARY)
    if(NOT RakNet_LIBRARY_RELEASE)
        if(WIN32)
            set(_raknet_release_default "${PROJECT_SOURCE_DIR}/extern/raknet/RakNetLibStatic.lib")
        else()
            set(_raknet_release_default "${PROJECT_SOURCE_DIR}/extern/raknet/lib/libRakNetLibStatic.a")
        endif()
        if(EXISTS "${_raknet_release_default}")
            set(RakNet_LIBRARY_RELEASE "${_raknet_release_default}")
        endif()
    endif()

    if(NOT RakNet_LIBRARY_DEBUG)
        if(WIN32)
            set(_raknet_debug_default "${PROJECT_SOURCE_DIR}/extern/raknet/RakNetLibStaticd.lib")
        else()
            set(_raknet_debug_default "${PROJECT_SOURCE_DIR}/extern/raknet/lib/libRakNetLibStaticd.a")
        endif()
        if(EXISTS "${_raknet_debug_default}")
            set(RakNet_LIBRARY_DEBUG "${_raknet_debug_default}")
        endif()
    endif()

    if(RakNet_LIBRARY_RELEASE)
        if(CMAKE_CONFIGURATION_TYPES AND RakNet_LIBRARY_DEBUG)
            set(RakNet_LIBRARY
                optimized "${RakNet_LIBRARY_RELEASE}"
                debug "${RakNet_LIBRARY_DEBUG}"
            )
        else()
            set(RakNet_LIBRARY "${RakNet_LIBRARY_RELEASE}")
        endif()
    endif()
endif()

# If no usable prebuilt library was supplied, build the tracked bundled source.
if(NOT RakNet_LIBRARY)
    set(_raknet_bundled_dir "${PROJECT_SOURCE_DIR}/extern/raknet")
    if(EXISTS "${_raknet_bundled_dir}/CMakeLists.txt")
        message(STATUS "RakNet library not prebuilt; building bundled CrabNet")
        set(CRABNET_ENABLE_DLL OFF CACHE BOOL "Build CrabNet shared library" FORCE)
        set(CRABNET_ENABLE_SAMPLES OFF CACHE BOOL "Build CrabNet samples" FORCE)
        set(CRABNET_ENABLE_STATIC ON CACHE BOOL "Build CrabNet static library" FORCE)
        add_subdirectory(
            "${_raknet_bundled_dir}"
            "${CMAKE_BINARY_DIR}/extern/raknet"
            EXCLUDE_FROM_ALL
        )
        set(RakNet_INCLUDES "${_raknet_bundled_dir}/include/raknet")
        set(RakNet_LIBRARY RakNetLibStatic)
    endif()
endif()

if(WIN32 AND RakNet_LIBRARY AND NOT TARGET RakNetLibStatic)
    list(APPEND RakNet_LIBRARY ws2_32)
endif()

if(RakNet_INCLUDES AND RakNet_LIBRARY)
    set(RakNet_FOUND TRUE)
else()
    set(RakNet_FOUND FALSE)
endif()

if(RakNet_FIND_REQUIRED AND NOT RakNet_FOUND)
    message(FATAL_ERROR
        "RakNet/CrabNet was not found. Keep extern/raknet in the source tree, "
        "or provide RakNet_INCLUDE_DIR and RakNet_LIBRARY_RELEASE."
    )
endif()

mark_as_advanced(
    RakNet_INCLUDE_DIR
    RakNet_INCLUDES
    RakNet_LIBRARY
    RakNet_LIBRARY_RELEASE
    RakNet_LIBRARY_DEBUG
)

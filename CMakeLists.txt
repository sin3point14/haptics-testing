cmake_minimum_required(VERSION 3.20)

if(WIN32)
  file(GLOB _MSVC_LIB_DIRS
    "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/*/lib/x64")
  list(SORT _MSVC_LIB_DIRS COMPARE NATURAL ORDER DESCENDING)
  list(GET _MSVC_LIB_DIRS 0 _MSVC_LIB_DIR)
  get_filename_component(_MSVC_LIB_PARENT "${_MSVC_LIB_DIR}" DIRECTORY)
  get_filename_component(_MSVC_TOOLSET_DIR "${_MSVC_LIB_PARENT}" DIRECTORY)

  set(_WINDOWS_SDK_ROOT "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.26100.0")
  set(_WINDOWS_SDK_UM_LIB "${_WINDOWS_SDK_ROOT}/um/x64")
  set(_WINDOWS_SDK_UCRT_LIB "${_WINDOWS_SDK_ROOT}/ucrt/x64")

  set(ENV{VSINSTALLDIR} "C:/Program Files/Microsoft Visual Studio/2022/Community/")
  set(ENV{VCToolsInstallDir} "${_MSVC_TOOLSET_DIR}/")
  set(ENV{WindowsSdkDir} "C:/Program Files (x86)/Windows Kits/10/")
  set(ENV{WindowsSDKVersion} "10.0.26100.0/")
  set(ENV{LIB}
    "${_MSVC_LIB_DIR};${_WINDOWS_SDK_UM_LIB};${_WINDOWS_SDK_UCRT_LIB};$ENV{LIB}")

  find_program(_WINDOWS_RC_EXE
    NAMES rc.exe
    PATHS
      "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
      "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x86"
    NO_DEFAULT_PATH)
  if(_WINDOWS_RC_EXE)
    set(CMAKE_RC_COMPILER "${_WINDOWS_RC_EXE}" CACHE FILEPATH "Windows resource compiler" FORCE)
  endif()

  find_program(_WINDOWS_MT_EXE
    NAMES mt.exe
    PATHS
      "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64"
      "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x86"
    NO_DEFAULT_PATH)
  if(_WINDOWS_MT_EXE)
    set(CMAKE_MT "${_WINDOWS_MT_EXE}" CACHE FILEPATH "Windows manifest tool" FORCE)
  endif()
endif()

project(test_haptics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================================
# Options
# ============================================================================
option(ENABLE_OPENHAPTICS "Build with OpenHaptics (3DS Touch) support" ON)
option(ENABLE_HAPTION     "Build with Haption (Virtuose) support"      ON)

if(WIN32 AND ENABLE_HAPTION)
  message(STATUS "Haption support is disabled on Windows for this build.")
  set(ENABLE_HAPTION OFF CACHE BOOL "Build with Haption (Virtuose) support" FORCE)
endif()

set(VIRTUOSE_API_ROOT "/home/rupeshcsz228228/my_projs/sofa/Haption_data/VirtuoseAPI_v4_04"
    CACHE PATH "Root of the VirtuoseAPI SDK")

# Accumulate per-backend results into these lists
set(ALL_INCLUDE_DIRS "")
set(ALL_LINK_LIBS "")
set(ALL_COMPILE_DEFS "")
set(ALL_SOURCES src/main.cpp)
set(POSE_LIB_SOURCES src/haptics_pose.cpp)

# ============================================================================
# OpenHaptics backend
# ============================================================================
if(ENABLE_OPENHAPTICS)
  if(WIN32)
    set(OPENHAPTICS_ROOT "C:/OpenHaptics/Developer/3.5.0" CACHE PATH "OpenHaptics SDK root")

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(OH_ARCH "x64")
    else()
      set(OH_ARCH "Win32")
    endif()

    if(CMAKE_BUILD_TYPE MATCHES "Debug")
      set(OH_CFG "Debug")
    else()
      set(OH_CFG "Release")
    endif()

    set(OH_INCLUDE_HD "${OPENHAPTICS_ROOT}/include")
    set(OH_INCLUDE_UTIL "${OPENHAPTICS_ROOT}/utilities/include")
    set(OH_LIB_HD "${OPENHAPTICS_ROOT}/lib/${OH_ARCH}/${OH_CFG}")
    set(OH_LIB_HDU "${OPENHAPTICS_ROOT}/utilities/lib/${OH_ARCH}/${OH_CFG}")

    set(OH_LIB_HD_FILE "${OH_LIB_HD}/hd.lib")
    set(OH_LIB_HL_FILE "${OH_LIB_HD}/hl.lib")
    set(OH_LIB_HDU_FILE "${OH_LIB_HDU}/hdu.lib")
    set(OH_LIB_GLUT_FILE "${OH_LIB_HDU}/glut32.lib")

    foreach(path_check IN ITEMS
      "${OH_INCLUDE_HD}/HD/hd.h"
      "${OH_INCLUDE_UTIL}/HDU/hduVector.h"
      "${OH_INCLUDE_UTIL}/GL/glut.h"
      "${OH_LIB_HD_FILE}"
      "${OH_LIB_HL_FILE}"
      "${OH_LIB_HDU_FILE}"
      "${OH_LIB_GLUT_FILE}")
      if(NOT EXISTS "${path_check}")
        message(FATAL_ERROR "Missing OpenHaptics dependency: ${path_check}")
      endif()
    endforeach()

    list(APPEND ALL_INCLUDE_DIRS "${OH_INCLUDE_HD}" "${OH_INCLUDE_UTIL}")
    list(APPEND ALL_LINK_LIBS
      "${OH_LIB_HD_FILE}" "${OH_LIB_HL_FILE}"
      "${OH_LIB_HDU_FILE}" "${OH_LIB_GLUT_FILE}"
      opengl32 glu32)

  elseif(UNIX)
    set(OPENHAPTICS_ROOT "/opt/OpenHaptics/Developer/3.4-0" CACHE PATH "OpenHaptics SDK root")

    find_path(OH_INCLUDE_HD
      NAMES HD/hd.h
      PATHS
        "${OPENHAPTICS_ROOT}/include"
        "/usr/include"
        "/usr/local/include")

    find_path(OH_INCLUDE_HDU
      NAMES HDU/hduVector.h
      PATHS
        "${OPENHAPTICS_ROOT}/include"
        "${OPENHAPTICS_ROOT}/libsrc/include"
        "/usr/include"
        "/usr/local/include")

    find_path(OH_INCLUDE_GL
      NAMES GL/glut.h
      PATHS
        "/usr/include"
        "/usr/local/include")

    find_library(OH_LIB_HD_FILE
      NAMES HD hd
      PATHS
        "${OPENHAPTICS_ROOT}/lib"
        "/usr/lib"
        "/usr/local/lib")

    find_library(OH_LIB_HL_FILE
      NAMES HL hl
      PATHS
        "${OPENHAPTICS_ROOT}/lib"
        "/usr/lib"
        "/usr/local/lib")

    find_library(OH_LIB_HDU_FILE
      NAMES HDU hdu
      PATHS
        "${OPENHAPTICS_ROOT}/lib"
        "/usr/lib"
        "/usr/local/lib")

    find_library(OH_LIB_GLUT_FILE
      NAMES glut freeglut
      PATHS
        "/usr/lib"
        "/usr/local/lib"
        "/usr/lib/x86_64-linux-gnu")

    foreach(path_check IN ITEMS
      "${OH_INCLUDE_HD}"
      "${OH_INCLUDE_HDU}"
      "${OH_INCLUDE_GL}"
      "${OH_LIB_HD_FILE}"
      "${OH_LIB_HL_FILE}"
      "${OH_LIB_HDU_FILE}"
      "${OH_LIB_GLUT_FILE}")
      if(NOT path_check OR path_check MATCHES "-NOTFOUND")
        message(FATAL_ERROR "Missing Linux OpenHaptics dependency: ${path_check}")
      endif()
    endforeach()

    find_package(OpenGL REQUIRED)

    list(APPEND ALL_INCLUDE_DIRS "${OH_INCLUDE_HD}" "${OH_INCLUDE_HDU}" "${OH_INCLUDE_GL}")
    list(APPEND ALL_LINK_LIBS
      "${OH_LIB_HD_FILE}" "${OH_LIB_HL_FILE}"
      "${OH_LIB_HDU_FILE}" "${OH_LIB_GLUT_FILE}"
      OpenGL::GL OpenGL::GLU)
    list(APPEND ALL_COMPILE_DEFS linux)
  else()
    message(FATAL_ERROR "OpenHaptics: unsupported platform.")
  endif()

  list(APPEND ALL_COMPILE_DEFS HAVE_OPENHAPTICS)
  list(APPEND POSE_LIB_SOURCES)
  message(STATUS "OpenHaptics support: ENABLED")
endif()

# ============================================================================
# Haption (Virtuose) backend
# ============================================================================
if(ENABLE_HAPTION)
  if(WIN32)
    message(FATAL_ERROR "ENABLE_HAPTION is only supported on UNIX builds in this repository.")
  endif()

  # --- header ---
  find_path(VIRTUOSE_INCLUDE_DIR
    NAMES virtuoseAPI.h
    PATHS
      "${VIRTUOSE_API_ROOT}"
      "${VIRTUOSE_API_ROOT}/include"
    NO_DEFAULT_PATH)

  if(NOT VIRTUOSE_INCLUDE_DIR)
    message(FATAL_ERROR "Cannot find virtuoseAPI.h under VIRTUOSE_API_ROOT=${VIRTUOSE_API_ROOT}")
  endif()

  # --- static library ---
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_virt_arch "x64")
  else()
    set(_virt_arch "x86")
  endif()

  find_library(VIRTUOSE_LIBRARY
    NAMES virtuose
    PATHS
      "${VIRTUOSE_API_ROOT}/linux/lib/${_virt_arch}"
      "${VIRTUOSE_API_ROOT}/lib/linux/${_virt_arch}"
    NO_DEFAULT_PATH)

  if(NOT VIRTUOSE_LIBRARY)
    message(FATAL_ERROR "Cannot find libvirtuose.a under VIRTUOSE_API_ROOT=${VIRTUOSE_API_ROOT}")
  endif()

  # --- runtime shared library (libvirtuose.a is a dlopen stub) ---
  find_file(VIRTUOSE_RUNTIME_SO
    NAMES virtuoseAPI.so
    PATHS
      "${VIRTUOSE_API_ROOT}/linux/bin/${_virt_arch}"
      "${VIRTUOSE_API_ROOT}/bin/linux/${_virt_arch}"
    NO_DEFAULT_PATH)

  if(NOT VIRTUOSE_RUNTIME_SO)
    message(FATAL_ERROR "Cannot find virtuoseAPI.so under VIRTUOSE_API_ROOT=${VIRTUOSE_API_ROOT}")
  endif()

  message(STATUS "VirtuoseAPI header : ${VIRTUOSE_INCLUDE_DIR}")
  message(STATUS "VirtuoseAPI library: ${VIRTUOSE_LIBRARY}")
  message(STATUS "VirtuoseAPI runtime: ${VIRTUOSE_RUNTIME_SO}")

  # VirtuoseAPI depends on pthreads, dl and rt on Linux
  find_package(Threads REQUIRED)

  list(APPEND ALL_INCLUDE_DIRS "${VIRTUOSE_INCLUDE_DIR}")
  list(APPEND ALL_LINK_LIBS "${VIRTUOSE_LIBRARY}" Threads::Threads ${CMAKE_DL_LIBS} rt)
  list(APPEND ALL_COMPILE_DEFS HAVE_HAPTION)
  list(APPEND ALL_SOURCES src/haption_device.cpp)
  list(APPEND POSE_LIB_SOURCES src/haption_device.cpp)

  # Copy virtuoseAPI.so next to the binary so dlopen finds it
  set(VIRTUOSE_COPY_SO TRUE)

  message(STATUS "Haption (Virtuose) support: ENABLED")
endif()

# ============================================================================
# Ensure we have GLUT for the display even when OpenHaptics is off
# ============================================================================
if(NOT ENABLE_OPENHAPTICS)
  # We still need GLUT and OpenGL for the 3-D window
  find_package(OpenGL REQUIRED)
  find_package(GLUT REQUIRED)
  list(APPEND ALL_INCLUDE_DIRS ${OPENGL_INCLUDE_DIRS} ${GLUT_INCLUDE_DIRS})
  list(APPEND ALL_LINK_LIBS OpenGL::GL OpenGL::GLU GLUT::GLUT)
endif()

# ============================================================================
# Sanity: at least one backend must be enabled
# ============================================================================
if(NOT ENABLE_OPENHAPTICS AND NOT ENABLE_HAPTION)
  message(FATAL_ERROR "At least one device backend must be enabled (ENABLE_OPENHAPTICS or ENABLE_HAPTION).")
endif()

# ============================================================================
# Target
# ============================================================================
add_library(haptics_pose SHARED ${POSE_LIB_SOURCES})

target_include_directories(haptics_pose PRIVATE ${ALL_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(ALL_COMPILE_DEFS)
  target_compile_definitions(haptics_pose PRIVATE ${ALL_COMPILE_DEFS})
endif()

target_link_libraries(haptics_pose PRIVATE ${ALL_LINK_LIBS})

set_target_properties(haptics_pose PROPERTIES
  OUTPUT_NAME "haptics_pose")

add_executable(test_haptics ${ALL_SOURCES})

target_include_directories(test_haptics PRIVATE ${ALL_INCLUDE_DIRS})

if(ALL_COMPILE_DEFS)
  target_compile_definitions(test_haptics PRIVATE ${ALL_COMPILE_DEFS})
endif()

target_link_libraries(test_haptics PRIVATE ${ALL_LINK_LIBS})

# Copy virtuoseAPI.so next to the binary so dlopen can find it at runtime
if(VIRTUOSE_COPY_SO)
  add_custom_command(TARGET test_haptics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${VIRTUOSE_RUNTIME_SO}"
      "$<TARGET_FILE_DIR:test_haptics>/virtuoseAPI.so"
    COMMENT "Copying virtuoseAPI.so to build directory")

  # Also set RPATH so the loader checks the binary's own directory
  set_target_properties(test_haptics PROPERTIES
    BUILD_RPATH "$ORIGIN"
    INSTALL_RPATH "$ORIGIN")
endif()

cmake_minimum_required(VERSION 3.20)
project(test_haptics LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
  message(FATAL_ERROR "This sample is configured for Windows + OpenHaptics only.")
endif()

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

add_executable(test_haptics src/main.cpp)

target_include_directories(test_haptics PRIVATE
  "${OH_INCLUDE_HD}"
  "${OH_INCLUDE_UTIL}")

target_link_libraries(test_haptics PRIVATE
  "${OH_LIB_HD_FILE}"
  "${OH_LIB_HL_FILE}"
  "${OH_LIB_HDU_FILE}"
  "${OH_LIB_GLUT_FILE}"
  opengl32
  glu32)

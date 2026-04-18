cmake_minimum_required(VERSION 3.15)
project(librdata LANGUAGES C)

set(CMAKE_C_STANDARD 11)
option(BUILD_SHARED_LIBS "Build shared rdata library" OFF)
option(WITH_ZLIB "Enable zlib compression support" ON)
option(WITH_BZIP2 "Enable bzip2 compression support" ON)
option(WITH_LZMA "Enable lzma/xz compression support" ON)

if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  # set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

file(GLOB RDATA_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
file(GLOB RDATA_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")

if(NOT RDATA_SOURCES)
  message(FATAL_ERROR "No source files found in src/. Ensure .c files are present.")
endif()

add_library(rdata ${RDATA_SOURCES} ${RDATA_HEADERS})
target_include_directories(rdata PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:include>
)

# zlib
if(WITH_ZLIB)
  find_package(ZLIB QUIET)
  if(ZLIB_FOUND)
    target_compile_definitions(rdata PRIVATE HAVE_ZLIB=1)
    target_link_libraries(rdata PUBLIC ZLIB::ZLIB)
  else()
    message(STATUS "zlib not found; building without zlib support")
  endif()
endif()

# bzip2
if(WITH_BZIP2)
  find_package(BZip2 QUIET)
  if(BZIP2_FOUND)
    target_compile_definitions(rdata PRIVATE HAVE_BZIP2=1)
    if(TARGET BZip2::BZip2)
      target_link_libraries(rdata PUBLIC BZip2::BZip2)
    else()
      target_include_directories(rdata PUBLIC ${BZIP2_INCLUDE_DIR})
      target_link_libraries(rdata PUBLIC ${BZIP2_LIBRARIES})
    endif()
  else()
    message(STATUS "bzip2 not found; building without bzip2 support")
  endif()
endif()

# liblzma / xz
if(WITH_LZMA)
  find_path(LZMA_INCLUDE NAMES lzma.h)
  find_library(LZMA_LIBRARY NAMES lzma)
  if(LZMA_INCLUDE AND LZMA_LIBRARY)
    target_compile_definitions(rdata PRIVATE HAVE_LZMA=1)
    target_include_directories(rdata PUBLIC ${LZMA_INCLUDE})
    target_link_libraries(rdata PUBLIC ${LZMA_LIBRARY})
  else()
    message(STATUS "liblzma not found; building without lzma support")
  endif()
endif()

# Installation
install(TARGETS rdata
  EXPORT librdataTargets
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
  RUNTIME DESTINATION bin
)
install(DIRECTORY src/ DESTINATION include FILES_MATCHING PATTERN "*.h")

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/librdataConfigVersion.cmake"
  VERSION 1.0.0 COMPATIBILITY AnyNewerVersion
)
install(EXPORT librdataTargets DESTINATION lib/cmake/librdata NAMESPACE librdata::)

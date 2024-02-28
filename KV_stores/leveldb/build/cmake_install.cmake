# Install script for directory: /home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/build/libleveldb.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/leveldb" TYPE FILE FILES
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/c.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/cache.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/comparator.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/db.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/dumpfile.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/env.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/export.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/filter_policy.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/iterator.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/options.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/slice.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/status.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/table_builder.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/table.h"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/include/leveldb/write_batch.h"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/leveldb/leveldbTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/leveldb/leveldbTargets.cmake"
         "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/build/CMakeFiles/Export/f90a79f6c24c38ae6b0a9cccec147da8/leveldbTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/leveldb/leveldbTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/leveldb/leveldbTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/leveldb" TYPE FILE FILES "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/build/CMakeFiles/Export/f90a79f6c24c38ae6b0a9cccec147da8/leveldbTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/leveldb" TYPE FILE FILES "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/build/CMakeFiles/Export/f90a79f6c24c38ae6b0a9cccec147da8/leveldbTargets-debug.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/leveldb" TYPE FILE FILES
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/build/cmake/leveldbConfig.cmake"
    "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/build/cmake/leveldbConfigVersion.cmake"
    )
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/build/third_party/googletest/cmake_install.cmake")
  include("/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/build/third_party/benchmark/cmake_install.cmake")

endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/home/wangzizhao/WorkloadAnalysis/KV_stores/leveldb/build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")

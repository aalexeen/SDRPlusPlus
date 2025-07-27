# Install script for directory: /home/alex_jd/IdeaProjects/ham/SDRPlusPlus

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr")
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
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/sdrpp")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp")
    file(RPATH_CHANGE
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp"
         OLD_RPATH "/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/core:"
         NEW_RPATH "")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/sdrpp")
    endif()
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/CMakeFiles/sdrpp.dir/install-cxx-module-bmi-Debug.cmake" OPTIONAL)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/root/res/bandplans")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/root/res/colormaps")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/root/res/fonts")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/root/res/icons")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/sdrpp" TYPE DIRECTORY FILES "/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/root/res/themes")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/applications" TYPE FILE FILES "/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/sdrpp.desktop")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for each subdirectory.
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/core/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/airspy_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/airspyhf_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/audio_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/file_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/hackrf_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/hermes_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/network_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/plutosdr_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/rfspace_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/rtl_sdr_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/rtl_tcp_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/sdrpp_server_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/spectran_http_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/source_modules/spyserver_source/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/sink_modules/audio_sink/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/sink_modules/network_sink/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/decoder_modules/meteor_demodulator/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/decoder_modules/pager_decoder/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/decoder_modules/radio/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/misc_modules/discord_integration/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/misc_modules/frequency_manager/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/misc_modules/iq_exporter/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/misc_modules/recorder/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/misc_modules/rigctl_client/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/misc_modules/rigctl_server/cmake_install.cmake")
  include("/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/misc_modules/scanner/cmake_install.cmake")

endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/home/alex_jd/IdeaProjects/ham/SDRPlusPlus/cmake-build-debug/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")

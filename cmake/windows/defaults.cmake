# CMake Windows defaults module

include_guard(GLOBAL)

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

include(buildspec)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  # ALLUSERSPROFILE is "C:\ProgramData": convert it, or cmake_install.cmake reads "\P" as an
  # invalid escape and `cmake --install` fails.
  file(TO_CMAKE_PATH "$ENV{ALLUSERSPROFILE}/obs-studio/plugins" _phonecam_install_prefix)
  set(CMAKE_INSTALL_PREFIX "${_phonecam_install_prefix}" CACHE PATH "Default plugin installation directory" FORCE)
endif()

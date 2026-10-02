cmake_minimum_required(VERSION 3.10...3.27)

include(GNUInstallDirs)

# Customize binaries for specific distributions
set(KNOWN_DISTRIBUTIONS debian gentoo generic)
option(DISTRIBUTION "Enable distribution specific functions" debian) # debian is the base of raspberry pi os
if(NOT DISTRIBUTION IN_LIST KNOWN_DISTRIBUTIONS)
  message(STATUS "specified distribution not known - assuming generic distribution")
endif()

#defaults
set(KMS_NOTFOUND_MSG "kmsprint is not found.")
set(PACTL_NOTFOUND_MSG "pactl is not found.")
set(ETHTOOL_NOTFOUND_MSG "ethtool is not found.")
set(PINCTRL_NOTFOUND_MSG "pinctrl/raspi-gpio is not found.")
set(VCDBG_NOTFOUND_MSG "vcdbg is not found.")
set(RPI_EEPROM_UPDATE_NOTFOUND_MSG "rpi-eeprom-update is not found.")

set(PKG_VERSION_CMD "echo \"current distribution is unknown - please check your package manager's instructions to check versions of applications\"")

if(DISTRIBUTION STREQUAL debian)
  set(KMS_NOTFOUND_MSG "kmsprint is not found. It can be installed with: sudo apt install kms++-utils")
  set(PKG_VERSION_CMD "apt-cache policy raspberrypi-ui-mods | head -2
apt-cache policy raspberrypi-sys-mods | head -2
apt-cache policy openbox | head -2
apt-cache policy lxpanel | head -2
apt-cache policy pcmanfm | head -2
apt-cache policy rpd-plym-splash | head -2")
elseif(DISTRIBUTION STREQUAL gentoo)
  set(PACTL_NOTFOUND_MSG "pactl is not found. It can be installed with media-libs/libpulse")
  set(ETHTOOL_NOTFOUND_MSG "ethtool is not found. It can be installed with sys-apps/ethtool")
  set(RPI_EEPROM_UPDATE_NOTFOUND_MSG "rpi-eeprom-update is not found. It can be installed with dev-embedded/rpi-eeprom")
  set(PKG_VERSION_CMD "emerge --info dev-embedded/raspberrypi-utils::gentoo | grep --color=never -A 1 \"^dev-embedded/raspberrypi-utils\"")
endif()

#set project name
project(raspinfo)

#configure executable
configure_file(raspinfo.in raspinfo @ONLY)

#add executables
install(PROGRAMS raspinfo DESTINATION ${CMAKE_INSTALL_BINDIR} PERMISSIONS WORLD_EXECUTE)

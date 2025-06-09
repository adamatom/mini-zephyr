cmake_minimum_required(VERSION 3.20)

project(zephyr_mini C)
find_package(Python3 REQUIRED)

if(NOT DEFINED ENV{ZEPHYR_BASE})
    set(ZEPHYR_BASE ${CMAKE_CURRENT_LIST_DIR}/../zephyr)
else()
    set(ZEPHYR_BASE $ENV{ZEPHYR_BASE})
endif()
message(STATUS "Using Zephyr base: ${ZEPHYR_BASE}")

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/include/generated)

#
# Run GEN_EDT_SCRIPT.
#
# creates edt.pickle, which encodes the dts information with the bindings yaml (for tooling)
set(EDT_SCRIPT ${ZEPHYR_BASE}/scripts/dts/gen_edt.py)
set(DTS_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/dts/mini.dts)
set(EDT_PICKLE ${CMAKE_BINARY_DIR}/edt.pickle)
set(DTS_OUT ${CMAKE_BINARY_DIR}/mini_out.dts)
set(BINDINGS_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/dts/bindings
  # ${ZEPHYR_BASE}/dts/bindings
)
message(STATUS "Generating devicetree pickle")
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${EDT_SCRIPT}
      --dts ${DTS_INPUT}
      --bindings-dirs ${BINDINGS_DIRS}
      --dts-out ${DTS_OUT}
      --edt-pickle-out ${EDT_PICKLE}
      --dtc-flags ""
  COMMAND_ERROR_IS_FATAL ANY
)

#
# Run GEN_DEFINES_SCRIPT.
#
# create devicetree_generated.h, which encodes the dts for C
set(DEF_SCRIPT ${ZEPHYR_BASE}/scripts/dts/gen_defines.py)
set(DT_HEADER_OUT ${CMAKE_BINARY_DIR}/include/generated/devicetree_generated.h)
message(STATUS "Generating devicetree_generated.h")
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${DEF_SCRIPT}
      --edt-pickle ${EDT_PICKLE}
      --header-out ${DT_HEADER_OUT}
  COMMAND_ERROR_IS_FATAL ANY
)

#
# Run GEN_DRIVER_KCONFIG_SCRIPT.
#
# creates Kconfig.dts, which the top-level Kconfig includes for Kconfig
# (in Zephyr, Kconfig.zephyr -> dts/Kconfig -> ${BUILDDIR}/Kconfig.dts)
# TODO: where does most of the content of this file come from, must be the bindings yaml?
set(GEN_DRIVER_KCONFIG_SCRIPT ${ZEPHYR_BASE}/scripts/dts/gen_driver_kconfig_dts.py)
set(DTS_KCONFIG ${CMAKE_BINARY_DIR}/Kconfig.dts)
message(STATUS "Converting devicetree into Kconfig symbols")
execute_process(
  COMMAND ${Python3_EXECUTABLE} ${GEN_DRIVER_KCONFIG_SCRIPT}
      --kconfig-out ${DTS_KCONFIG}
      --bindings-dirs ${BINDINGS_DIRS}
  COMMAND_ERROR_IS_FATAL ANY
)

#
# Run GEN_DTS_CMAKE_SCRIPT.
#
set(GEN_DTS_CMAKE_SCRIPT        ${ZEPHYR_BASE}/scripts/dts/gen_dts_cmake.py)
# The generated information itself, which we include() after creating it.
set(DTS_CMAKE                   ${CMAKE_BINARY_DIR}/dts.cmake)

message(STATUS "Converting devicetree into CMake symbols")
execute_process(
  COMMAND ${PYTHON_EXECUTABLE} ${GEN_DTS_CMAKE_SCRIPT}
  --edt-pickle ${EDT_PICKLE}
  --cmake-out ${DTS_CMAKE}
  COMMAND_ERROR_IS_FATAL ANY
  )

include(${DTS_CMAKE})

# Run KCONFIG_SCRIPT
# 
set(KCONFIG_SCRIPT ${ZEPHYR_BASE}/scripts/kconfig/kconfig.py)
set(KCONFIG_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/Kconfig)
set(KCONFIG_CONFIG_OUT ${CMAKE_BINARY_DIR}/.config)
set(KCONFIG_HEADER_OUT ${CMAKE_BINARY_DIR}/include/generated/autoconf.h)
set(KCONFIG_LIST_OUT ${CMAKE_BINARY_DIR}/sources.txt)
set(KCONFIG_CONF ${CMAKE_CURRENT_SOURCE_DIR}/prj.conf)

# kconfig.py and its library will check for this environment variable in order to generate the
# "CONFIG_DT_HAS_...." defines in .config. It is quite important for populating the .config with
# the devicetree hardware description
set(ENV{EDT_PICKLE} ${EDT_PICKLE})
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env KCONFIG_BINARY_DIR=${CMAKE_BINARY_DIR}
  ${Python3_EXECUTABLE} ${KCONFIG_SCRIPT}
    ${KCONFIG_INPUT}
    ${KCONFIG_CONFIG_OUT}
    ${KCONFIG_HEADER_OUT}
    ${KCONFIG_LIST_OUT}
    ${KCONFIG_CONF}
  COMMAND_ERROR_IS_FATAL ANY
)

# --- Parse .config into CMake variables ---
# import_kconfig(<prefix> <kconfig_fragment> [<keys>] [TARGET <target>])
#
# Parse a KConfig fragment (typically with extension .config) and
# introduce all the symbols that are prefixed with 'prefix' into the
# CMake namespace. List all created variable names in the 'keys'
# output variable if present.
#
# <prefix>          : symbol prefix of settings in the Kconfig fragment.
# <kconfig_fragment>: absolute path to the config fragment file.
# <keys>            : output variable which will be populated with variable
#                     names loaded from the kconfig fragment.
# TARGET <target>   : set all symbols on <target> instead of adding them to the
#                     CMake namespace.
function(import_kconfig prefix kconfig_fragment)
  cmake_parse_arguments(IMPORT_KCONFIG "" "TARGET" "" ${ARGN})
  file(
    STRINGS
    ${kconfig_fragment}
    DOT_CONFIG_LIST
    ENCODING "UTF-8"
  )

  foreach (LINE ${DOT_CONFIG_LIST})
    if("${LINE}" MATCHES "^(${prefix}[^=]+)=([ymn]|.+$)")
      # Matched a normal value assignment, like: CONFIG_NET_BUF=y
      # Note: if the value starts with 'y', 'm', or 'n', then we assume it's a
      # bool or tristate (we don't know the type from <kconfig_fragment> alone)
      # and we only match the first character. This is to align with Kconfiglib.
      set(CONF_VARIABLE_NAME "${CMAKE_MATCH_1}")
      set(CONF_VARIABLE_VALUE "${CMAKE_MATCH_2}")
    elseif("${LINE}" MATCHES "^# (${prefix}[^ ]+) is not set")
      # Matched something like: # CONFIG_FOO is not set
      # This is interpreted as: CONFIG_FOO=n
      set(CONF_VARIABLE_NAME "${CMAKE_MATCH_1}")
      set(CONF_VARIABLE_VALUE "n")
    else()
      # Ignore this line.
      # Note: we also ignore assignments which don't have the desired <prefix>.
      continue()
    endif()

    # If the provided value is n, then the corresponding CMake variable or
    # target property will be unset.
    if("${CONF_VARIABLE_VALUE}" STREQUAL "n")
      if(DEFINED IMPORT_KCONFIG_TARGET)
        set_property(TARGET ${IMPORT_KCONFIG_TARGET} PROPERTY "${CONF_VARIABLE_NAME}")
      else()
        unset("${CONF_VARIABLE_NAME}" PARENT_SCOPE)
      endif()
      list(REMOVE_ITEM keys "${CONF_VARIABLE_NAME}")
      continue()
    endif()

    # Otherwise, the variable/property will be set to the provided value.
    # For string values, we also remove the surrounding quotation marks.
    if("${CONF_VARIABLE_VALUE}" MATCHES "^\"(.*)\"$")
      set(CONF_VARIABLE_VALUE ${CMAKE_MATCH_1})
    endif()

    if(DEFINED IMPORT_KCONFIG_TARGET)
      set_property(TARGET ${IMPORT_KCONFIG_TARGET} PROPERTY "${CONF_VARIABLE_NAME}" "${CONF_VARIABLE_VALUE}")
    else()
      set("${CONF_VARIABLE_NAME}" "${CONF_VARIABLE_VALUE}" PARENT_SCOPE)
    endif()
    list(APPEND keys "${CONF_VARIABLE_NAME}")
  endforeach()

  if(DEFINED IMPORT_KCONFIG_TARGET)
    set_property(TARGET ${IMPORT_KCONFIG_TARGET} PROPERTY "kconfigs" "${keys}")
  endif()

  list(LENGTH IMPORT_KCONFIG_UNPARSED_ARGUMENTS unparsed_length)
  if(unparsed_length GREATER 0)
    if(unparsed_length GREATER 1)
    # Two mandatory arguments and one optional, anything after that is an error.
      list(GET IMPORT_KCONFIG_UNPARSED_ARGUMENTS 1 first_invalid)
      message(FATAL_ERROR "Unexpected argument after '<keys>': import_kconfig(... ${first_invalid})")
    endif()
    set(${IMPORT_KCONFIG_UNPARSED_ARGUMENTS} "${keys}" PARENT_SCOPE)
  endif()
endfunction()

import_kconfig(CONFIG_ ${KCONFIG_CONFIG_OUT})

# --- Define the Application Executable ---
add_executable(app src/main.c)

if(CONFIG_DEMO_I2C_APP)
  message(STATUS "I2C Demo App is enabled by Kconfig, including source file.")
  target_sources(app PRIVATE src/demo_i2c_app.c)
else()
  message(STATUS "I2C Demo App is disabled by Kconfig.")
endif()

target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_BINARY_DIR}/include/generated
)

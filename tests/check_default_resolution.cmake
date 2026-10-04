# SPDX-License-Identifier: GPL-3.0-or-later
#
# The M12.6 spec moves the capture default resolution to 1920x1080 and adds
# `--width`/`--height`. This renders a default `--shot` and an explicit-size `--shot` and
# reads each PPM header, so a silent regression to the old 1440x900 (or to a fixed size
# that ignores the flags) is caught. Invoked as
# `cmake -DGUI=... -DWORKDIR=... -DOUT=... -P this.cmake`.

function(shot_and_check out want_w want_h)
  execute_process(COMMAND "${GUI}" standard --shot "${out}" --screen board ${ARGN}
                  WORKING_DIRECTORY "${WORKDIR}"
                  RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "shot ${out} failed (${rc}): ${err}")
  endif()
  file(READ "${out}" header LIMIT 32)
  string(REGEX MATCH "^P6[ \t\r\n]+([0-9]+)[ \t\r\n]+([0-9]+)" _ "${header}")
  if(NOT CMAKE_MATCH_1 OR NOT CMAKE_MATCH_2)
    message(FATAL_ERROR "not a binary PPM: '${header}'")
  endif()
  if(NOT CMAKE_MATCH_1 EQUAL want_w OR NOT CMAKE_MATCH_2 EQUAL want_h)
    message(FATAL_ERROR "${out} is ${CMAKE_MATCH_1}x${CMAKE_MATCH_2}, want ${want_w}x${want_h}")
  endif()
  message(STATUS "${out}: ${CMAKE_MATCH_1}x${CMAKE_MATCH_2}")
endfunction()

shot_and_check("${OUT}" 1920 1080)
shot_and_check("${OUT}.small" 640 480 --width 640 --height 480)

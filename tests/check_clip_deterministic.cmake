# SPDX-License-Identifier: GPL-3.0-or-later
#
# Assert a `--play FILE --clip DIR` export is byte-identical on a second run (M12.6). The
# runner must read no wall clock, so this is the end-to-end determinism contract. Invoked
# as:
#   cmake -DGUI=... -DWORKDIR=... -DCOMMON=a|b|c -DOUT_A=... -DOUT_B=... -P this.cmake
# COMMON is `|`-separated because CMake lists split on `;`.

string(REPLACE "|" ";" common_args "${COMMON}")

function(run_clip out)
  file(REMOVE_RECURSE "${out}")
  execute_process(COMMAND "${GUI}" ${common_args} --clip "${out}"
                  WORKING_DIRECTORY "${WORKDIR}"
                  RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "clip run into ${out} failed (${rc}): ${err}")
  endif()
endfunction()

run_clip("${OUT_A}")
run_clip("${OUT_B}")

file(GLOB frames_a "${OUT_A}/frame_*.ppm")
file(GLOB frames_b "${OUT_B}/frame_*.ppm")
list(LENGTH frames_a count_a)
list(LENGTH frames_b count_b)
if(NOT count_a EQUAL count_b OR count_a EQUAL 0)
  message(FATAL_ERROR "frame counts differ or are zero: ${count_a} vs ${count_b}")
endif()

foreach(frame IN LISTS frames_a)
  get_filename_component(name "${frame}" NAME)
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files
                          "${frame}" "${OUT_B}/${name}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "frame ${name} is not byte-identical across runs")
  endif()
endforeach()

message(STATUS "clip reproduced byte-for-byte over ${count_a} frames")

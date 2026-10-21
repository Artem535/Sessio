# Standalone compiler/link driver for scripts/build-litert-lm-gpu.sh.
# Reads existing flags and link metadata; never configures or mutates CPU builds.
cmake_minimum_required(VERSION 3.28)
foreach(_required SESSIO_GPU_ROOT SESSIO_CPU_BUILD SESSIO_REPO)
  if(NOT DEFINED ${_required})
    message(FATAL_ERROR "Missing ${_required}")
  endif()
endforeach()
set(_hook "${SESSIO_REPO}/src/call_prompter/litert")
set(_out "${SESSIO_GPU_ROOT}/build")
set(_lib "${SESSIO_GPU_ROOT}/install/lib")
file(READ "${SESSIO_CPU_BUILD}/CMakeFiles/sessio_litert.dir/flags.make" _flags)
set(_compile)
foreach(_variable CXX_DEFINES CXX_INCLUDES CXX_FLAGS)
  string(REGEX MATCH "${_variable} = ([^\n]*)" _match "${_flags}")
  separate_arguments(_parts UNIX_COMMAND "${CMAKE_MATCH_1}")
  list(APPEND _compile ${_parts})
endforeach()
file(READ "${SESSIO_CPU_BUILD}/CMakeFiles/sessio_litert.dir/link.txt" _original_link)
separate_arguments(_original UNIX_COMMAND "${_original_link}")
list(GET _original 0 _compiler)
function(_compile_object _source _object)
  execute_process(COMMAND "${_compiler}" ${_compile} -DSESSIO_LITERT_VULKAN=1 -DLITERT_USE_WEBGPU_ACCELERATOR=1
    "-I${_hook}" -c "${_source}" -o "${_object}"
    WORKING_DIRECTORY "${SESSIO_CPU_BUILD}" COMMAND_ERROR_IS_FATAL ANY)
endfunction()
_compile_object("${_hook}/sessio_litert.cpp" "${_out}/sessio_litert.o")
# Reuse all expensive archives, but explicitly force the compiled-model options
# to WebGPU (not upstream's platform auto-selection / possible OpenCL backend).
_compile_object("${SESSIO_CPU_BUILD}/runtime/executor/llm_executor_settings_utils.cc" "${_out}/webgpu_options.o")
set(_link)
set(_skip FALSE)
foreach(_arg IN LISTS _original)
  if(_skip)
    set(_skip FALSE)
    continue()
  endif()
  if(_arg STREQUAL "-o")
    list(APPEND _link -o "${_lib}/libsessio_litert.so")
    set(_skip TRUE)
  elseif(_arg MATCHES "^CMakeFiles/sessio_litert.dir/.*\\.o$")
    list(APPEND _link "${_out}/sessio_litert.o" "${_out}/webgpu_options.o"
      -Wl,--no-as-needed "${_lib}/libLiteRt.so" -Wl,--as-needed)
  elseif(_arg MATCHES "^-Wl,--dependency-file=" OR _arg STREQUAL "-Wl,--whole-archive" OR _arg STREQUAL "-Wl,--no-whole-archive")
    continue()
  elseif(_arg MATCHES "[/\\]vendors[/\\]samsung[/\\]" OR _arg MATCHES "libpython" OR _arg MATCHES "^-Wl,-rpath,")
    continue()
  elseif(_arg MATCHES "libruntime_core_engine_advanced_impl\\.a$")
    list(APPEND _link -Wl,--whole-archive "${_arg}" -Wl,--no-whole-archive)
  else()
    list(APPEND _link "${_arg}")
  endif()
endforeach()
list(APPEND _link -ldl "-Wl,-rpath,$ORIGIN")
file(WRITE "${_out}/link-arguments.txt" "${_link}\n")
execute_process(COMMAND ${_link} WORKING_DIRECTORY "${SESSIO_CPU_BUILD}" COMMAND_ERROR_IS_FATAL ANY)
# Standalone smoke links only our public C++ boundary, with its own $ORIGIN
# relative RUNPATH; neither LD_LIBRARY_PATH nor an external inference process.
execute_process(COMMAND "${_compiler}" -std=c++20 -O2 -pthread "-I${_hook}"
  "-I${SESSIO_CPU_BUILD}/_deps/json-src/include" "${SESSIO_REPO}/test/litert_gpu_runtime_smoke.cpp"
  "-L${_lib}" -lsessio_litert "-Wl,-rpath-link,${_lib}" "-Wl,-rpath,$ORIGIN/../install/lib" -o "${_out}/litert_gpu_runtime_smoke"
  COMMAND_ERROR_IS_FATAL ANY)

include_directories(..)

# Runtime library sources and build flags.
set(MSAN_RTL_SOURCES
  msan.cc
  msan_allocator.cc
  msan_interceptors.cc
  msan_linux.cc
  msan_new_delete.cc
  msan_report.cc
  msan_thread.cc
  )

set(MSAN_RTL_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_no_rtti_flag(MSAN_RTL_CFLAGS)
append_if(COMPILER_RT_HAS_FPIE_FLAG -fPIE MSAN_RTL_CFLAGS)
# Prevent clang from generating libc calls.
append_if(COMPILER_RT_HAS_FFREESTANDING_FLAG -ffreestanding MSAN_RTL_CFLAGS)

# Static runtime library.
add_custom_target(msan)
set(arch "x86_64")
if(CAN_TARGET_${arch})
  add_compiler_rt_runtime(clang_rt.msan-${arch} ${arch} STATIC
    SOURCES ${MSAN_RTL_SOURCES}
            $<TARGET_OBJECTS:RTInterception.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
    CFLAGS ${MSAN_RTL_CFLAGS})
  add_dependencies(msan clang_rt.msan-${arch})
  if(UNIX)
    add_sanitizer_rt_symbols(clang_rt.msan-${arch} msan.syms.extra)
    add_dependencies(msan clang_rt.msan-${arch}-symbols)
  endif()
endif()

add_compiler_rt_resource_file(msan_blacklist msan_blacklist.txt)
add_dependencies(msan msan_blacklist)
add_dependencies(compiler-rt msan)

if(COMPILER_RT_INCLUDE_TESTS)
  add_subdirectory(tests)
endif()

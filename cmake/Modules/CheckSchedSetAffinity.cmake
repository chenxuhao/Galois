include(CheckSymbolExists)

if(SCHED_SETAFFINITY_FOUND)

else()
  CHECK_SYMBOL_EXISTS(sched_setaffinity sched.h HAVE_SCHED_SETAFFINITY_INTERNAL)
  if(HAVE_SCHED_SETAFFINITY_INTERNAL)
    message(STATUS "sched_setaffinity found")
    set(SCHED_SETAFFINITY_FOUND "${HAVE_SCHED_SETAFFINITY_INTERNAL}" CACHE BOOL "Have sched_setaffinity")
    set(SCHED_SETAFFINITY_LIBRARIES rt)
  endif()
endif()
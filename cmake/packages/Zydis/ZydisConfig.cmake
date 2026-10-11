if(NOT TARGET Zydis)
    message(FATAL_ERROR "Add the external/zydis submodule before SafetyHook")
endif()
set(Zydis_FOUND TRUE)

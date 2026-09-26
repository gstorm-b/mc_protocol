# mc_warnings.cmake — mc_apply_warnings(target): the warning level and C++ baseline
# shared by every mc_protocol target (library and tests).
#
# MSVC: /W4, plus /WX when MC_WARNINGS_AS_ERRORS.
# GCC/Clang: -Wall -Wextra -Wpedantic, plus -Werror when MC_WARNINGS_AS_ERRORS.
# Every target: cxx_std_17, CXX_EXTENSIONS OFF.
function(mc_apply_warnings target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(MC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(MC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

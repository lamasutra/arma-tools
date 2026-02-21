# Strict warnings for all targets
function(armatools_set_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:
            $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:
                -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
                -Wnon-virtual-dtor -Woverloaded-virtual -Wcast-align
            >
            $<$<CXX_COMPILER_ID:MSVC>:
                /W4 /permissive-
            >
        >
        $<$<COMPILE_LANGUAGE:C>:
            $<$<C_COMPILER_ID:GNU,Clang,AppleClang>:
                -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
                -Wcast-align
            >
        >
    )
endfunction()

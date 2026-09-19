# Windows CMake file(WRITE) emits CRLF. Verify the exact canonical source text
# so chained patches and repeated configurations accept only line-ending changes.
include_guard(GLOBAL)
function(carnivores_read_sdl3_patch_source path output_source output_sha)
    file(READ "${path}" patch_source)
    string(REPLACE "\r\n" "\n" patch_source "${patch_source}")
    string(SHA256 patch_sha "${patch_source}")
    set(${output_source} "${patch_source}" PARENT_SCOPE)
    set(${output_sha} "${patch_sha}" PARENT_SCOPE)
endfunction()

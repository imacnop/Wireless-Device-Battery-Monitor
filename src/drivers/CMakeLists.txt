# Drivers independent of any windowing system. Shared by the Linux port and
# the Windows port; the Linux target links only this subset.

set(WB_PORTABLE_DRIVERS
    glorious.cpp
    kysona.cpp
    logitech.cpp
    razer.cpp
    steelseries.cpp
    corsair.cpp
    hyperx.cpp
    playstation.cpp
    coolermaster.cpp
    finalmouse.cpp
    # Also called by src/linux/devices_linux.cpp: without them the Linux link failed (since v1.2.0).
    zebronics.cpp
    keychron.cpp
    lamzu.cpp
    aula.cpp
    ninjutso.cpp
    rapoo.cpp)

list(TRANSFORM WB_PORTABLE_DRIVERS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

add_library(wb_drivers STATIC ${WB_PORTABLE_DRIVERS})
target_link_libraries(wb_drivers PUBLIC wb_headers)
target_compile_options(wb_drivers PRIVATE
    -Wall -Wno-missing-field-initializers)

include(ExternalProject)

set(elfin_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/libelfin/dwarf ${CMAKE_CURRENT_BINARY_DIR}/libelfin/src)

set(elfin_URL https://github.com/TartanLlama/libelfin/archive/refs/heads/fbreg.tar.gz)

set(elfin_BUILD ${CMAKE_CURRENT_BINARY_DIR}/libelfin/src/elfin)

ExternalProject_Add(
        elfin
        PREFIX libelfin
        URL ${elfin_URL}
        DOWNLOAD_DIR "${DOWNLOAD_LOCATION}"
        DOWNLOAD_EXTRACT_TIMESTAMP true
        BUILD_IN_SOURCE 1
        BUILD_BYPRODUCTS elfin_STATIC dwarf_STATIC
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E echo "Skipping configure"
        BUILD_COMMAND make -C elf libelf++.a && make -C dwarf libdwarf++.a
        INSTALL_COMMAND ${CMAKE_COMMAND} -E echo "Skipping install"
)

ExternalProject_Get_Property(elfin INSTALL_DIR)

set(elfin_STATIC ${elfin_BUILD}/elf/libelf++.a)
set(dwarf_STATIC ${elfin_BUILD}/dwarf/libdwarf++.a)
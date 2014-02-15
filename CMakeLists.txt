project (pulse-app-manager)
cmake_minimum_required (VERSION 2.8)
cmake_policy (VERSION 2.6)

set (SOURCES
	pa.c
	application.c
	sink.c
)

add_definitions (-g)

find_package (PkgConfig)
pkg_check_modules (DEPS libpulse-mainloop-glib gtk+-3.0)
add_definitions(${DEPS_CFLAGS})
link_libraries(${DEPS_LIBRARIES})
link_directories(${DEPS_LIBRARY_DIRS})

add_executable (pa ${SOURCES})


cmake_minimum_required(VERSION 3.5)

project(libvirtiolo VERSION 0.0.1 DESCRIPTION "Library to interface virtio loopback kernel module" LANGUAGES C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS       "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Werror")

include(FindPkgConfig)
pkg_check_modules(extlibs REQUIRED libnl-3.0 libnl-genl-3.0)

#
# libvirtiolo
#
add_library(virtiolo SHARED libvirtiolo.c)

set(LIBVIRTIOLO_VERSION   0.1.0)
set(LIBVIRTIOLO_SOVERSION 0)

set_target_properties(virtiolo
	PROPERTIES
	VERSION    ${LIBVIRTIOLO_VERSION}
	SOVERSION  ${LIBVIRTIOLO_SOVERSION}
	)

#
# test
#
add_executable(test test.c epoll_scheduler.c stream_gen.c)
target_link_libraries(test PRIVATE virtiolo)

#
# test_vduse
#
add_executable(test_vduse test_vduse.c)
target_include_directories(test_vduse PRIVATE ${extlibs_INCLUDE_DIRS})
target_link_libraries(test_vduse PUBLIC ${extlibs_LIBRARIES})

#
# install
#
include(GNUInstallDirs)

configure_file(libvirtiolo.pc.in libvirtiolo.pc @ONLY)

install(TARGETS virtiolo test test_vduse)
install(FILES libvirtiolo.h  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libvirtiolo.pc DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)

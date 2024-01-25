cmake_minimum_required(VERSION 3.5)

project(libvirtiolo VERSION 0.0.1 DESCRIPTION "Library to interface virtio loopback kernel module" LANGUAGES C)

set(CMAKE_C_STANDARD 11)

set(CMAKE_C_FLAGS       "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Werror")

include(FindPkgConfig)
pkg_check_modules(gtk_test_modules REQUIRED gtk+-3.0 glm epoxy)
pkg_check_modules(test_virglrenderer_modules REQUIRED virglrenderer epoxy gbm libdrm egl)

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
# libmisc
#
add_library(misc STATIC libvduse.c epoll_scheduler.c device.c)

#
# test_lo
#
add_executable(test_lo test_lo.c)
target_link_libraries(test_lo PRIVATE virtiolo misc)

#
# test_vduse
#
add_executable(test_vduse test_vduse.c)
target_link_libraries(test_vduse PRIVATE misc)

#
# test_vduse_gpu
#
add_executable(test_vduse_gpu test_vduse_gpu.c)
target_link_libraries(test_vduse_gpu PRIVATE misc)

#
# gtk_test
#

add_executable(gtk_test gtk_test.c es2gears.c)
target_include_directories(gtk_test PRIVATE ${gtk_test_modules_INCLUDE_DIRS})
target_link_libraries(gtk_test PRIVATE ${gtk_test_modules_LIBRARIES} m)

#
# test_virglrenderer
#

add_executable(test_virglrenderer test_virglrenderer.c virtio_thread.c epoll_scheduler.c virtio_request.c iov.c drm_state.c egl_helpers.c)
target_include_directories(test_virglrenderer PRIVATE ${test_virglrenderer_modules_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_virglrenderer PRIVATE ${test_virglrenderer_modules_LIBRARIES} virtiolo)

#
# install
#
include(GNUInstallDirs)

configure_file(libvirtiolo.pc.in libvirtiolo.pc @ONLY)

install(TARGETS virtiolo test_lo test_vduse test_vduse_gpu gtk_test test_virglrenderer)
install(FILES libvirtiolo.h  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/libvirtiolo.pc DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pkgconfig)

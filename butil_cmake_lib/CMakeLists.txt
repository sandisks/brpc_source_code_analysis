cmake_minimum_required(VERSION 3.16)
project(butil_lib LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =====================================================================
# butil（brpc 的基础工具库）独立 CMake 静态库。
# butil 源码已随本仓库管理（src/butil/，来自 brpc 1.17.0 原代码），
# 不依赖外部 brpc 源码树。
#
# 用法：
#   cmake -B build
#   cmake --build build -j$(nproc)
#
# butil 是 brpc 的底座，仅依赖 gflags / openssl / protobuf / pthread，
# 完全不依赖 bthread、bvar、brpc 本体，因此可以独立编译成库。
# 源文件列表与官方顶层 CMakeLists.txt 中的 BUTIL_SOURCES 对齐。
# =====================================================================

# ---- butil 源码目录（默认仓库内 src/butil，可覆盖为其他路径）----
set(BUTIL_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/butil" CACHE PATH
    "Directory that contains butil sources (must contain iobuf.h)")
if(NOT EXISTS "${BUTIL_SRC_DIR}/iobuf.h")
    message(FATAL_ERROR "butil source not found under ${BUTIL_SRC_DIR}")
endif()
set(BUTIL_DIR "${BUTIL_SRC_DIR}")

# ---- 外部依赖 ----
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(GFLAGS_INCLUDE_DIR NAMES gflags/gflags.h)
find_library(GFLAGS_LIBRARY NAMES gflags)
if(NOT GFLAGS_INCLUDE_DIR OR NOT GFLAGS_LIBRARY)
    message(FATAL_ERROR "gflags not found (apt install libgflags-dev)")
endif()

# butil 的 IOBuf 继承并实现 protobuf 的 ZeroCopyInput/OutputStream 接口，
# 因此既需要头文件也需要链接 libprotobuf
find_path(PROTOBUF_INCLUDE_DIR NAMES google/protobuf/io/zero_copy_stream.h)
find_library(PROTOBUF_LIBRARY NAMES protobuf)
if(NOT PROTOBUF_INCLUDE_DIR OR NOT PROTOBUF_LIBRARY)
    message(FATAL_ERROR "protobuf not found (apt install libprotobuf-dev)")
endif()

# ---- 生成 butil/config.h（官方在顶层 CMakeLists.txt 的 configure_file 步骤）----
set(WITH_GLOG_VAL "0")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
               ${CMAKE_CURRENT_BINARY_DIR}/generated/butil/config.h @ONLY)

# ---- 编译宏：与官方 CMakeLists.txt 保持一致 ----
add_compile_definitions(
    _GNU_SOURCE
    __const__=__unused__
    USE_SYMBOLIZE
    NO_TCMALLOC
    __STDC_FORMAT_MACROS __STDC_LIMIT_MACROS __STDC_CONSTANT_MACROS
    BTHREAD_USE_FAST_PTHREAD_MUTEX
    __STRICT_ANSI__
)

# ---- 收集 butil 源文件（对照官方 BUTIL_SOURCES 列表）----
file(GLOB_RECURSE BUTIL_SOURCES CONFIGURE_DEPENDS
    "${BUTIL_DIR}/*.cpp" "${BUTIL_DIR}/*.cc" "${BUTIL_DIR}/*.c")

# 平台过滤：官方仅在 Darwin 分支引入 *.mm 与 mac/ 目录；
# FreeBSD 文件（platform_thread_freebsd.cc）官方未在 Linux/Darwin 分支编译
if(NOT APPLE)
    list(FILTER BUTIL_SOURCES EXCLUDE REGEX "/mac/|\\.mm$|_freebsd\\.cc$|_mac\\.cc$")
endif()

# 官方未编译的文件（黑名单，防止 GLOB 误收）：
# 1. dmg_fp/dtoa.cc —— 其实现由 dtoa_wrapper.cc 以 #include 方式引入
# 2. comlog_sink.cc  —— 依赖百度 comlog 库，官方 BUTIL_SOURCES 不含它
list(FILTER BUTIL_SOURCES EXCLUDE REGEX "third_party/dmg_fp/dtoa\\.cc$")
list(FILTER BUTIL_SOURCES EXCLUDE REGEX "comlog_sink\\.cc$")

# ---- 静态库 ----
add_library(butil STATIC ${BUTIL_SOURCES})
target_include_directories(butil PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${GFLAGS_INCLUDE_DIR}
    ${PROTOBUF_INCLUDE_DIR}
)
target_compile_options(butil PRIVATE
    -Wall -W -Wno-invalid-offsetof -Wno-unused-parameter)
target_link_libraries(butil PUBLIC
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    ${GFLAGS_LIBRARY}
    ${PROTOBUF_LIBRARY}
    ${CMAKE_DL_LIBS}
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(butil PUBLIC rt)
endif()

# ---- 安装 ----
include(GNUInstallDirs)
install(TARGETS butil ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY ${BUTIL_DIR}/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/butil
        FILES_MATCHING PATTERN "*.h")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/generated/butil/config.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/butil)

# ---- demo ----
option(BUILD_DEMO "Build demo" ON)
if(BUILD_DEMO)
    add_subdirectory(demo)
endif()

# ---- 打包 deb (CPack) ----
set(CPACK_PACKAGE_NAME "libbutil")
set(CPACK_PACKAGE_VERSION "1.17.0")
set(CPACK_PACKAGE_VENDOR "brpc-source-analysis")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "butil base library extracted from brpc 1.17.0")
set(CPACK_PACKAGE_CONTACT "wdidada")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "wdidada <wdidada@localhost>")
set(CPACK_DEBIAN_PACKAGE_SECTION "libdevel")
set(CPACK_GENERATOR "DEB")
include(CPack)

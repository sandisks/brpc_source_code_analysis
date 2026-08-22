cmake_minimum_required(VERSION 3.10)
project(brpc_examples LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# 0. 依赖前缀：默认 /tmp/brpc_local/usr（本仓库构建脚本安装 brpc 的位置）
#    可用 -DBRPC_ROOT=<前缀> 覆盖
# ---------------------------------------------------------------------------
if(NOT DEFINED BRPC_ROOT)
    set(BRPC_ROOT "/tmp/brpc_local/usr")
endif()
set(CMAKE_PREFIX_PATH ${BRPC_ROOT} ${CMAKE_PREFIX_PATH})

# ---------------------------------------------------------------------------
# 1. 找依赖：brpc / protobuf / gflags / ssl
# ---------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(gflags REQUIRED)

# brpc 库与头文件（支持源码树输出目录）
find_path(BRPC_INCLUDE_DIR NAMES brpc/channel.h
    PATHS ${BRPC_ROOT}/include /usr/local/include /usr/include)
find_library(BRPC_LIBRARY NAMES brpc
    PATHS ${BRPC_ROOT}/lib ${BRPC_ROOT}/lib64 /usr/local/lib /usr/lib/x86_64-linux-gnu)

if(NOT BRPC_INCLUDE_DIR OR NOT BRPC_LIBRARY)
    message(FATAL_ERROR
        "brpc not found. 先构建/安装 brpc 到 ${BRPC_ROOT}，见 scripts/build_brpc.sh")
endif()

message(STATUS "BRPC_INCLUDE_DIR = ${BRPC_INCLUDE_DIR}")
message(STATUS "BRPC_LIBRARY     = ${BRPC_LIBRARY}")
message(STATUS "PROTOC           = ${Protobuf_PROTOC_EXECUTABLE}")

# ---------------------------------------------------------------------------
# 2. 用 protoc 生成公共 proto（examples/proto/echo.proto）
# ---------------------------------------------------------------------------
set(PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/examples/proto)
set(PROTO_FILE ${PROTO_DIR}/echo.proto)
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${GEN_DIR})

add_custom_command(
    OUTPUT ${GEN_DIR}/echo.pb.cc ${GEN_DIR}/echo.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${GEN_DIR}
        -I${PROTO_DIR}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
    COMMENT "Generating C++ from echo.proto"
)
add_library(echo_proto STATIC ${GEN_DIR}/echo.pb.cc)
target_include_directories(echo_proto PUBLIC
    ${GEN_DIR}
    ${Protobuf_INCLUDE_DIRS})
target_link_libraries(echo_proto PUBLIC ${Protobuf_LIBRARIES})

# ---------------------------------------------------------------------------
# 3. 每个 example 一个可执行文件
# ---------------------------------------------------------------------------
# ssl/crypto/leveldb：优先从 BRPC_ROOT 的多架构 lib 目录找
find_library(SSL_LIBRARY NAMES ssl
    PATHS ${BRPC_ROOT}/lib/x86_64-linux-gnu ${BRPC_ROOT}/lib /usr/lib/x86_64-linux-gnu)
find_library(CRYPTO_LIBRARY NAMES crypto
    PATHS ${BRPC_ROOT}/lib/x86_64-linux-gnu ${BRPC_ROOT}/lib /usr/lib/x86_64-linux-gnu)
find_library(LEVELDB_LIBRARY NAMES leveldb
    PATHS ${BRPC_ROOT}/lib/x86_64-linux-gnu ${BRPC_ROOT}/lib /usr/lib/x86_64-linux-gnu)
find_library(SNAPPY_LIBRARY NAMES snappy
    PATHS ${BRPC_ROOT}/lib/x86_64-linux-gnu ${BRPC_ROOT}/lib /usr/lib/x86_64-linux-gnu)
message(STATUS "SSL_LIBRARY = ${SSL_LIBRARY}")
message(STATUS "CRYPTO_LIBRARY = ${CRYPTO_LIBRARY}")
message(STATUS "LEVELDB_LIBRARY = ${LEVELDB_LIBRARY}")
message(STATUS "SNAPPY_LIBRARY = ${SNAPPY_LIBRARY}")

set(COMMON_LIBS
    echo_proto
    ${BRPC_LIBRARY}
    ${Protobuf_LIBRARIES}
    gflags
    ${SSL_LIBRARY} ${CRYPTO_LIBRARY} ${LEVELDB_LIBRARY} ${SNAPPY_LIBRARY}
    Threads::Threads
    dl
)

function(add_brpc_example name)
    add_executable(${name} examples/${name}/main.cpp)
    target_include_directories(${name} PRIVATE
        ${BRPC_INCLUDE_DIR}
        ${GEN_DIR}
        ${Protobuf_INCLUDE_DIRS})
    target_link_libraries(${name} PRIVATE ${COMMON_LIBS})
    # 运行时库搜索路径：保证直接运行可执行文件能加载 protobuf/ssl/gflags
    set_target_properties(${name} PROPERTIES
        BUILD_RPATH "${BRPC_ROOT}/lib/x86_64-linux-gnu;${BRPC_ROOT}/lib"
        INSTALL_RPATH "${BRPC_ROOT}/lib/x86_64-linux-gnu;${BRPC_ROOT}/lib")
endfunction()

add_brpc_example(echo_server)
add_brpc_example(echo_client)
add_brpc_example(bvar_demo)
add_brpc_example(bthread_demo)
add_brpc_example(iobuf_demo)
add_brpc_example(selective_echo)
add_brpc_example(backup_request)
add_brpc_example(load_balancing)

# ---------------------------------------------------------------------------
# 4. 构建日志：把配置信息写入 build_logs/
# ---------------------------------------------------------------------------
set(LOG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build_logs)
file(MAKE_DIRECTORY ${LOG_DIR})
set(BUILD_LOG ${LOG_DIR}/build.log)

add_custom_target(log_build_info ALL
    COMMAND ${CMAKE_COMMAND} -E echo
        "[${CMAKE_BUILD_TYPE}] examples=8 brpc=${BRPC_LIBRARY}"
        >> ${BUILD_LOG}
    COMMENT "Append build info to ${BUILD_LOG}"
)
message(STATUS "Build log -> ${BUILD_LOG}")

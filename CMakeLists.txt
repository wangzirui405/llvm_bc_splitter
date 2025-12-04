cmake_minimum_required(VERSION 3.10)
project(BCSplitter LANGUAGES CXX)

# 基本设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 使用 Xcode 工具链
set(CMAKE_C_COMPILER "/Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++")

# 设置系统根目录（sysroot）
set(CMAKE_OSX_SYSROOT "/Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -stdlib=libc++ -target arm64-apple-macosx15.2.0")

# 预处理器定义
add_definitions(-D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS)

# 包含路径
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# LLVM 相关路径
set(LLVM_INCLUDE_DIR "/opt/homebrew/Cellar/llvm/21.1.5/include")
set(LLVM_LIB_DIR "/opt/homebrew/Cellar/llvm/21.1.5/lib")

# 系统头文件路径
set(SYSTEM_INCLUDE_DIRS
    ${LLVM_INCLUDE_DIR}
    /Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/v1
    /Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/16/include
    /Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include
)

# 添加系统包含目录
foreach(dir ${SYSTEM_INCLUDE_DIRS})
    if(EXISTS ${dir})
        include_directories(SYSTEM ${dir})
    endif()
endforeach()

# 链接选项
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${LLVM_LIB_DIR} -lLLVM-21 -stdlib=libc++")

# 设置源码文件
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
)

# 设置头文件（可选，通常用于IDE显示）
file(GLOB_RECURSE HEADERS
    "include/*.h"
)

# 打印源文件列表用于调试
message(STATUS "源文件列表: ${SOURCES}")
message(STATUS "头文件列表: ${HEADERS}")

# 添加可执行文件
add_executable(bc_splitter ${SOURCES} ${HEADERS})

# 链接系统库
target_link_libraries(bc_splitter PRIVATE c++ System)

# 添加目标包含目录
target_include_directories(bc_splitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SYSTEM_INCLUDE_DIRS}
)

# 添加编译特性
target_compile_features(bc_splitter PRIVATE cxx_std_17)

# 设置输出目录（可选）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 安装目标（可选）
install(TARGETS bc_splitter DESTINATION bin)
install(DIRECTORY include/ DESTINATION include/BCSplitter)
cmake_minimum_required(VERSION 3.10)
project(BCSplitter)

# 基本设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 使用 Xcode 工具链
set(CMAKE_C_COMPILER "/Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang")
set(CMAKE_CXX_COMPILER "/Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++")

# 设置系统根目录（sysroot）
set(CMAKE_OSX_SYSROOT "/Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk")

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O2 -stdlib=libc++ -target arm64-apple-macosx15.2.0")

# 预处理器定义
add_definitions(-D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS)

# 包含路径 - 添加系统头文件路径
include_directories(
    /opt/homebrew/Cellar/llvm/21.1.5/include
    /Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/c++/v1
    /Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/16/include
    /Users/wangzirui/Downloads/Xcode-2.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include
)

# 链接选项
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L/opt/homebrew/Cellar/llvm/21.1.5/lib -lLLVM-21 -stdlib=libc++")

# 添加可执行文件
add_executable(bc_splitter bc_splitter.cpp)

# 链接系统库
target_link_libraries(bc_splitter c++ System)
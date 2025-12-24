cmake_minimum_required(VERSION 3.20.0)
project(BCSplitter LANGUAGES C CXX)

# C++ standard configuration
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Add LLVM include directories and definitions
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Collect source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

# Collect header files for IDE display
file(GLOB_RECURSE HEADERS "include/*.h")

# Create executable
add_executable(bc_splitter ${SOURCES} ${HEADERS})

# Set C++ standard for the target
target_compile_features(bc_splitter PRIVATE cxx_std_17)

# Add include directories
target_include_directories(bc_splitter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LLVM_INCLUDE_DIRS}
)

# Link LLVM libraries
llvm_config(bc_splitter USE_SHARED)

# Additional system libraries
target_link_libraries(bc_splitter PRIVATE ${LLVM_LIBRARIES})

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Install targets
install(TARGETS bc_splitter DESTINATION bin)
install(DIRECTORY include/ DESTINATION include/BCSplitter)

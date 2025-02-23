cmake_minimum_required(VERSION 3.5.1)
project(ANALYZER)

find_package(LLVM REQUIRED PATHS ${LLVM_INSTALL_DIR})
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})

# Set your project compile flags.
# E.g. if using the C++ header files
# you will need to enable C++14 support
# for your compiler.
# Check for C++14 support and set the compilation flag
include(CheckCXXCompilerFlag)

# Set your openmp include path here
# For Mac OS with openmp installed by brew, using the following flags
set(OMP_INCLUDE_DRIS "/opt/homebrew/Cellar/libomp/19.1.3/include")
set(OMP_LIB_DRIS "/opt/homebrew/Cellar/libomp/19.1.3/lib")
include_directories(${OMP_INCLUDE_DRIS})
link_directories(${OMP_LIB_DRIS})

# Set your mysql include path here
# For Mac OS with mysql installed by brew, using the following flags
set(MYSQL_INCLUDE_DIRS "/opt/homebrew/Cellar/mysql-client/9.2.0/include")
set(MYSQL_LIB_DRIS "/opt/homebrew/Cellar/mysql-client/9.2.0/lib")
include_directories(${MYSQL_INCLUDE_DIRS})
link_directories(${MYSQL_LIB_DRIS})

add_definitions(${LLVM_DEFINITIONS})
add_subdirectory(lib)


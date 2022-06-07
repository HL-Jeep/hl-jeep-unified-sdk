# the name of the target operating system
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i386)

# which compilers to use for C and C++
set(CMAKE_C_COMPILER gcc-11)
set(CMAKE_C_FLAGS -m32)
set(CMAKE_CXX_COMPILER g++-11)
set(CMAKE_CXX_FLAGS -m32)

# here is the target environment located
#set(CMAKE_FIND_ROOT_PATH /usr/lib/i386-linux-gnu)

# adjust the default behaviour of the FIND_XXX() commands:
# search headers and libraries in the target environment, search
# programs in the host environment
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

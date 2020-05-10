cmake_minimum_required(VERSION 3.14)

project(fpsprof)

source_group(src REGULAR_EXPRESSION ".*\\.[ch].*")

# prefer minimal size by default
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_MINSIZEREL}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_MINSIZEREL}")
if(CMAKE_CONFIGURATION_TYPES) # https://stackoverflow.com/questions/31661264/cmake-generators-for-visual-studio-do-not-set-cmake-configuration-types
    set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Debug/Release only" FORCE)
endif()
if (UNIX)
	add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Wall>)
else()
   	add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    if (MSVC)
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/W3>)
	else() # clang, mingw
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:-Wall>)
	endif()
endif()

add_library(libfpsprof STATIC)
file(GLOB_RECURSE libfpsprof_SRC "src/*.[ch]*" "include/*.h*")
target_sources(libfpsprof PRIVATE ${libfpsprof_SRC})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${libfpsprof_SRC})
target_include_directories(libfpsprof PUBLIC include)

# Tests
if(${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME})

    set(test_cpp_SRC test/test_cpp.cc)
    set(test_c_SRC test/test_c.c)
    set(fpsprof_SRC test/fpsprof.cc)

    foreach(X IN ITEMS
        test_cpp
        test_c
        fpsprof
    )
        add_executable(${X})
        target_sources(${X} PRIVATE ${${X}_SRC})
        target_link_libraries(${X} libfpsprof)
        if (UNIX)
            target_link_libraries(${X} m stdc++)
        else()
            target_include_directories(${X} PRIVATE test/win32)
        endif()
    endforeach()

    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT test_cpp)
endif()

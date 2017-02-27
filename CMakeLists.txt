cmake_minimum_required(VERSION 3.2 FATAL_ERROR)

project(epoll_checker)

add_compile_options(-Wall -Wextra -Weffc++)

if (${SANITIZER_TYPE} STREQUAL "Address")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
endif()

add_library(dynamic_library dynamic_library.cpp)
target_link_libraries(dynamic_library -ldl)
if (${SANITIZER_TYPE} STREQUAL "Address")
    target_link_libraries(dynamic_library -fsanitize=address)
endif()

add_library(epoll_checker SHARED epoll_checker.cpp)
target_compile_features(epoll_checker PUBLIC cxx_auto_type)
target_link_libraries(epoll_checker dynamic_library)
if (${SANITIZER_TYPE} STREQUAL "Address")
    target_link_libraries(epoll_checker -fsanitize=address)
endif()

add_executable(demo demo.cpp)
target_compile_features(demo PUBLIC cxx_auto_type)
target_link_libraries(demo dynamic_library)
if (${SANITIZER_TYPE} STREQUAL "Address")
    target_link_libraries(demo -fsanitize=address)
endif()

add_executable(demo_err demo_err.cpp)
target_compile_features(demo_err PUBLIC cxx_auto_type)
target_link_libraries(demo_err dynamic_library)
if (${SANITIZER_TYPE} STREQUAL "Address")
    target_link_libraries(demo_err -fsanitize=address)
endif()

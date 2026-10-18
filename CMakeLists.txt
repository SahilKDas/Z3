cmake_minimum_required(VERSION 3.25)
project(Z3 LANGUAGES CXX)
find_package(Vulkan REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_library(Z3_GLFW_STATIC NAMES libglfw3.a REQUIRED)
add_executable(z3 WIN32 src/main.cpp src/renderer.cpp ${CMAKE_BINARY_DIR}/embedded_resources.cpp)
target_include_directories(z3 PRIVATE ${Vulkan_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(z3 PRIVATE ${Z3_GLFW_STATIC} ${Vulkan_LIBRARIES} windowscodecs ole32 gdi32 winmm comdlg32 user32 shell32)
target_compile_definitions(z3 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(z3 PRIVATE -std=c++26 -O2 -Wall -Wextra -Wno-missing-field-initializers)
target_link_options(z3 PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--strip-all)
foreach(stage vert frag)
 add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/world.${stage}.spv COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${CMAKE_SOURCE_DIR}/shaders/world.${stage} -o ${CMAKE_BINARY_DIR}/world.${stage}.spv DEPENDS shaders/world.${stage})
endforeach()
add_custom_target(shaders ALL DEPENDS ${CMAKE_BINARY_DIR}/world.vert.spv ${CMAKE_BINARY_DIR}/world.frag.spv)
add_dependencies(z3 shaders)
file(GLOB_RECURSE Z3_ASSETS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/*.png")
add_custom_command(OUTPUT ${CMAKE_BINARY_DIR}/embedded_resources.cpp
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/embed_resources.py ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}
 DEPENDS tools/embed_resources.py ${Z3_ASSETS} fonts/english.png fonts/default-skin.png ${CMAKE_BINARY_DIR}/world.vert.spv ${CMAKE_BINARY_DIR}/world.frag.spv)
add_custom_command(TARGET z3 POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/dist
 COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:z3> ${CMAKE_SOURCE_DIR}/dist/Z3.exe
 COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/check_single_exe.py ${CMAKE_SOURCE_DIR}/dist ${CMAKE_OBJDUMP})

enable_testing()
add_test(NAME simulation COMMAND z3 --test)
set_tests_properties(simulation PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

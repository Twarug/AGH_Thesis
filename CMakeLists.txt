cmake_minimum_required(VERSION 4.1)
project(thesis_tester)

set(CMAKE_CXX_STANDARD 23)

# Znajdź Vulkan
find_package(Vulkan REQUIRED)

# Znajdź GLFW (dla okna)
set(GLFW_INCLUDE_DIR "A:/.cache/glfw-3.4/include")
set(GLFW_LIBRARY "A:/.cache/glfw-3.4/lib-vc2022/glfw3.lib")

include_directories("src")
include_directories(${GLFW_INCLUDE_DIR})
add_compile_definitions(VK_ENABLE_BETA_EXTENSIONS)
add_compile_definitions(GLFW_INCLUDE_VULKAN)

# Pliki źródłowe
file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h")

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME}
        Vulkan::Vulkan
        ${GLFW_LIBRARY}
)

target_include_directories(${PROJECT_NAME} PRIVATE
        src_old/
        ${Vulkan_INCLUDE_DIRS}
)


# Kompilacja shaderów
find_program(GLSL_VALIDATOR glslangValidator HINTS
        ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}
        /usr/bin
        /usr/local/bin
        ${VULKAN_SDK_PATH}/Bin
        ${VULKAN_SDK_PATH}/Bin32
        $ENV{VULKAN_SDK}/Bin/
        $ENV{VULKAN_SDK}/Bin32/
)

file(GLOB_RECURSE GLSL_SOURCE_FILES
        "shaders/*.frag"
        "shaders/*.vert"
        "shaders/*.comp"
)

foreach(GLSL ${GLSL_SOURCE_FILES})
    get_filename_component(FILE_NAME ${GLSL} NAME)
    set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
    add_custom_command(
            OUTPUT ${SPIRV}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/shaders/"
            COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
            DEPENDS ${GLSL}
    )
    list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)


add_custom_target(
        Shaders
        DEPENDS ${SPIRV_BINARY_FILES}
)

add_dependencies(${PROJECT_NAME} Shaders)
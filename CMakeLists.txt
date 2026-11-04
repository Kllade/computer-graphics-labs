cmake_minimum_required(VERSION 3.20)
include(FetchContent)

project(vulkan-starter-app LANGUAGES C CXX)

# Одна общая инфраструктура, отдельный код и шейдеры для каждой работы.
set(LAB_NUMBER "3" CACHE STRING "Laboratory number: 1, 2 or 3")
if(NOT LAB_NUMBER MATCHES "^[123]$")
	message(FATAL_ERROR "LAB_NUMBER must be 1, 2 or 3")
endif()
if(LAB_NUMBER STREQUAL "3")
	set(LAB_SOURCE source/application.cpp)
	set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
else()
	set(LAB_SOURCE labs/lab${LAB_NUMBER}/source/application.cpp)
	set(SHADER_DIR ${CMAKE_SOURCE_DIR}/labs/lab${LAB_NUMBER}/shaders)
endif()

add_executable(${PROJECT_NAME} source/main.cpp source/graphics_internal.cpp ${LAB_SOURCE})
set_source_files_properties(source/main.cpp PROPERTIES COMPILE_DEFINITIONS "LAB_NUMBER=${LAB_NUMBER}")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/source)

if(LINUX)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(APPLE)
	set_target_properties(${PROJECT_NAME} PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_GUI_IDENTIFIER "local.cg.vulkan-starter-app"
		MACOSX_BUNDLE_BUNDLE_NAME "Vulkan Starter App"
	)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
elseif(MSVC)
	target_compile_options(${PROJECT_NAME} PRIVATE /W4 /wd4201)
	target_link_options(${PROJECT_NAME} PRIVATE /subsystem:windows /entry:mainCRTStartup)
	target_compile_definitions(${PROJECT_NAME} PRIVATE -D_USE_MATH_DEFINES)
else()
	message(FATAL_ERROR "${CMAKE_SYSTEM_NAME} is not supported")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD_REQUIRED TRUE CXX_STANDARD 20)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.5.1
)

FetchContent_Declare(
	vk-bootstrap
	GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
	GIT_TAG v1.4.321
)

FetchContent_Declare(
	VulkanMemoryAllocator
	GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
	GIT_TAG v3.4.0
)

FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.92.9
)

FetchContent_Declare(
	glm
	GIT_REPOSITORY https://github.com/g-truc/glm.git
	GIT_TAG 1.0.3
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
	lodepng
	GIT_REPOSITORY https://github.com/lvandeve/lodepng.git
	GIT_TAG 7b2893e35891c0c85bee5595caafaf3c2044b409
)

find_package(Vulkan REQUIRED)

set(GLFW_LIBRARY_TYPE STATIC)
set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)

FetchContent_MakeAvailable(
	glfw
	vk-bootstrap
	VulkanMemoryAllocator
	imgui
	glm
	lodepng
)

# декодирование PNG в массив пикселей RGBA8
target_sources(${PROJECT_NAME} PRIVATE ${lodepng_SOURCE_DIR}/lodepng.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${lodepng_SOURCE_DIR})

target_link_libraries(${PROJECT_NAME} PRIVATE
	glfw
	Vulkan::Vulkan
	vk-bootstrap::vk-bootstrap
	GPUOpen::VulkanMemoryAllocator
	glm::glm
)

target_include_directories(${PROJECT_NAME} PRIVATE ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_sources(${PROJECT_NAME} PRIVATE
	${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
	${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

# compile shaders
find_program(GLSLC_FOUND glslc HINTS "$ENV{VULKAN_SDK}/bin" REQUIRED)
if(GLSLC_FOUND)
	set(_SHADER_BINARIES)

	macro(compile_shader SHADER_FILE)
		set(SHADER_SOURCE ${SHADER_DIR}/${SHADER_FILE})
		set(SHADER_BINARY ${SHADER_FILE}.spv)
		set(SHADER_BINARY_PATH ${SHADER_DIR}/${SHADER_BINARY})

		add_custom_command(
			OUTPUT ${SHADER_BINARY_PATH}
			COMMAND "${GLSLC_FOUND}" "${SHADER_SOURCE}" -o "${SHADER_BINARY_PATH}"
			VERBATIM
			DEPENDS ${SHADER_SOURCE}
			COMMENT "Compiling ${SHADER_FILE} shader"
		)

		list(APPEND _SHADER_BINARIES ${SHADER_BINARY_PATH})
	endmacro()

	# to compile shader file, use compile_shader function with a file name
	# of a shader inside shaders directory. See example below:

	compile_shader(icosahedron.vert)
	compile_shader(icosahedron.frag)

	add_custom_target(shaders DEPENDS ${_SHADER_BINARIES})
	add_dependencies(${PROJECT_NAME} shaders)
endif()

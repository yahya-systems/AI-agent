# Tools/CMakeLists.txt

cmake_minimum_required(VERSION 3.20)

project(JarvisTools)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find common dependencies
find_package(PkgConfig REQUIRED)

# Use IMPORTED_TARGET for each dependency
pkg_check_modules(pugixml IMPORTED_TARGET REQUIRED pugixml)
pkg_check_modules(CURL IMPORTED_TARGET REQUIRED libcurl)

# Function to build a tool
function(add_tool TOOL_NAME LINK_TARGETS)
    # Collect all .cpp files in tools_library/${TOOL_NAME}/src
    file(GLOB_RECURSE TOOL_SOURCES 
        "${CMAKE_CURRENT_SOURCE_DIR}/tool_library/${TOOL_NAME}/src/*.cpp"
    )
    
    if(NOT TOOL_SOURCES)
        message(FATAL_ERROR "No source files found for tool: ${TOOL_NAME}")
    endif()
    
    # Create module library
    add_library(${TOOL_NAME} MODULE ${TOOL_SOURCES})
    
    # Link against imported targets (handles both libraries and include paths)
    target_link_libraries(${TOOL_NAME} PRIVATE ${LINK_TARGETS})
    
    # Set output to tools_library/${TOOL_NAME}/
    set_target_properties(${TOOL_NAME} PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tool_library/${TOOL_NAME}"
    )
    
    # macOS: use .dylib extension
    if(APPLE)
        set_target_properties(${TOOL_NAME} PROPERTIES
            SUFFIX ".dylib"
            PREFIX ""
        )
    endif()
endfunction()

# Build each tool with imported targets
add_tool(run_applescript "PkgConfig::pugixml")
add_tool(run_command "PkgConfig::pugixml")
add_tool(run_python "PkgConfig::pugixml;")
add_tool(web_search "PkgConfig::pugixml;PkgConfig::CURL")

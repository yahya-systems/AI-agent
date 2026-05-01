cmake_minimum_required(VERSION 3.20)
project(JarvisTTS)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL IMPORTED_TARGET REQUIRED libcurl)

add_subdirectory(third_party/whisper.cpp)

add_library(llm STATIC LLM/llm_query.cpp)
target_include_directories(llm PUBLIC third_party/ LLM/include/)

add_library(stt STATIC STT/stt.cpp)
target_link_libraries(stt PRIVATE whisper "-framework CoreAudio" "-framework AudioToolbox")
target_include_directories(stt PUBLIC STT/include/ third_party/ third_party/whisper.cpp/include/ third_party/whisper.cpp/ggml/include/)
    
target_link_libraries(stt PRIVATE whisper)

add_library(pugixml STATIC third_party/xml/src/pugixml.cpp)
target_include_directories(pugixml PUBLIC third_party/xml/include/)

add_library(tools STATIC Tools/tools.cpp)
target_link_libraries(tools PRIVATE pugixml)
target_include_directories(tools PUBLIC third_party/ Tools/include/)

add_library(base64 STATIC third_party/base-cpp/base64.cpp)
target_include_directories(base64 PUBLIC third_party/base-cpp/include)

add_executable(agent main.cpp)
target_link_libraries(agent PRIVATE PkgConfig::CURL llm stt tools base64 pugixml)


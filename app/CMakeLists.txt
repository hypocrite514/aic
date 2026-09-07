cmake_minimum_required(VERSION 3.20)
project(boardnote CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(MSVC)
  add_compile_options(/utf-8 /W3 /EHsc)
endif()

set(OPENCV_ROOT "D:/opencv/opencv/build" CACHE PATH "OpenCV root directory")
set(OpenCV_DIR "${OPENCV_ROOT}" CACHE PATH "" FORCE)
find_package(OpenCV REQUIRED)

add_executable(boardnote
  src/main.cpp
  src/pipeline.cpp
  src/net_http.cpp
  src/config.cpp
  src/ocr_client.cpp
  src/llm_client.cpp
)

target_include_directories(boardnote PRIVATE
  third_party
  src
  ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(boardnote PRIVATE ${OpenCV_LIBS} ws2_32 winhttp)

if(MSVC AND EXISTS "${OPENCV_ROOT}/x64/vc16/bin/opencv_world4140.dll")
  add_custom_command(TARGET boardnote POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENCV_ROOT}/x64/vc16/bin/opencv_world4140.dll"
            $<TARGET_FILE_DIR:boardnote>
  )
endif()
if(MSVC AND EXISTS "${OPENCV_ROOT}/x64/vc16/bin/opencv_world4140d.dll")
  add_custom_command(TARGET boardnote POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${OPENCV_ROOT}/x64/vc16/bin/opencv_world4140d.dll"
            $<TARGET_FILE_DIR:boardnote>
  )
endif()

add_custom_command(TARGET boardnote POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/web"
          $<TARGET_FILE_DIR:boardnote>/web
)

if(EXISTS "${CMAKE_SOURCE_DIR}/config.json")
  add_custom_command(TARGET boardnote POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/config.json"
            $<TARGET_FILE_DIR:boardnote>/config.json
  )
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/models")
  add_custom_command(TARGET boardnote POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/models"
            $<TARGET_FILE_DIR:boardnote>/models
  )
endif()

set                   (PROJECT_NAME encode2mp3)
cmake_minimum_required(VERSION 2.6)
project               (${PROJECT_NAME})
add_executable        (${PROJECT_NAME} encode2mp3.cpp filesystem.cpp)
include_directories   (${PROJECT_SOURCE_DIR})
file                  (GLOB_RECURSE LibFiles "./*.hpp" "includes/*.hpp" "./*.h" "includes/*.h" "./*.hxx" "includes/*.hxx")
add_custom_target     (headers SOURCES ${LibFiles})

#pthreads
set                   (THREADS_PREFER_PTHREAD_FLAG ON)
find_package          (Threads REQUIRED)
target_link_libraries (${PROJECT_NAME} Threads::Threads)

#lame
add_library           (mp3lame STATIC IMPORTED)
if (UNIX)
  message             ("OS: UNIX")
  set_property        (TARGET mp3lame PROPERTY IMPORTED_LOCATION /usr/local/lib/libmp3lame.a)
endif (UNIX)

if (WIN32)
  message             ("OS: Windows")
  include_directories (${PROJECT_SOURCE_DIR}/includes)
  set_property        (TARGET mp3lame PROPERTY IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/libs/libmp3lame.a)
endif (WIN32)

target_link_libraries (${PROJECT_NAME} mp3lame)


enable_testing()

set(TEST_FILE_FILTER test_file_filter)
add_executable(${TEST_FILE_FILTER} tests/test_file_filter.cpp filesystem.cpp)
link_directories()
add_test(NAME "test_file_filter" COMMAND ${TEST_FILE_FILTER})


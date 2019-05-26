set                    (PROJECT_NAME encode2mp3)
cmake_minimum_required (VERSION 2.6)
project                (${PROJECT_NAME})
add_executable         (${PROJECT_NAME} encode2mp3.cpp)

#pthreads
set                    (THREADS_PREFER_PTHREAD_FLAG ON)
find_package           (Threads REQUIRED)
target_link_libraries  (${PROJECT_NAME} Threads::Threads)

#lame
#set                    (LAME_LIBRARY mp3lame)
#target_link_libraries  (${PROJECT_NAME}  ${LAME_LIBRARY})
#find_library            (mp3lame HINTS ${PROJECT_SOURCE_DIR/..})
target_link_libraries   (${PROJECT_NAME} mp3lame)

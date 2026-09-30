cmake_minimum_required(VERSION 3.16)
project(CGSS_Script C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)

add_executable(CGSS_Script
    main.c
    lookup_common.c lookup_3d.c lookup_spine.c lookup_song.c lookup_action.c
    lookup_chart.c lookup_card.c lookup_voice.c lookup_stage.c lookup_main.c
    download.c net.c acb.c util.c
    unpack.c unpack_fbx.c unpack_res.c assetripper.c
    sticker.c
    paper.c
    browse.c
    cg.c
    spine_convert.c preview.c texture_merge.cpp
    GBKswapUTF8.c sqlite3.c
    auto_updata.c
    miniz/miniz.c
    compat/wincompat.c
    compat/winhttp_curl.c
)

target_include_directories(CGSS_Script PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/compat
    ${CMAKE_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/miniz
    ${CURL_INCLUDE_DIRS}
)

target_compile_definitions(CGSS_Script PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
)

target_link_libraries(CGSS_Script PRIVATE CURL::libcurl)

if(APPLE)
    target_compile_definitions(CGSS_Script PRIVATE _DARWIN_C_SOURCE)
    target_compile_options(CGSS_Script PRIVATE -finput-charset=UTF-8)
endif()

add_executable(usm usm.c compat/wincompat.c)
target_include_directories(usm PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/compat
)

add_custom_command(TARGET CGSS_Script POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/spine_preview
        $<TARGET_FILE_DIR:CGSS_Script>/spine_preview
    COMMENT "Copying spine_preview assets next to CGSS_Script")

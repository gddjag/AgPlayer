if(NOT DEFINED CATALOGS OR CATALOGS STREQUAL "")
    message(FATAL_ERROR "CATALOGS must contain the two AgPlayer TS files")
endif()

set(expected_languages en_US zh_CN)
set(actual_languages)

foreach(catalog IN LISTS CATALOGS)
    if(NOT EXISTS "${catalog}")
        message(FATAL_ERROR "Translation catalog does not exist: ${catalog}")
    endif()

    file(READ "${catalog}" content)
    string(REGEX MATCH "language=\"([^\"]+)\"" language_match "${content}")
    if(NOT CMAKE_MATCH_1)
        message(FATAL_ERROR "Translation catalog has no language: ${catalog}")
    endif()
    list(APPEND actual_languages "${CMAKE_MATCH_1}")

    string(REGEX MATCHALL "<message[ >]" messages "${content}")
    string(REGEX MATCHALL "<translation[ >]" translations "${content}")
    list(LENGTH messages message_count)
    list(LENGTH translations translation_count)
    if(NOT message_count EQUAL translation_count)
        message(FATAL_ERROR
            "${catalog}: ${message_count} messages but ${translation_count} translations")
    endif()

    if(content MATCHES "type=\"unfinished\""
       OR content MATCHES "<translation[^>]*>[ \t\r\n]*</translation>"
       OR content MATCHES "<translation[^>]*/>")
        message(FATAL_ERROR "Translation catalog contains unfinished text: ${catalog}")
    endif()

    if(content MATCHES "�")
        message(FATAL_ERROR "Translation catalog contains replacement characters: ${catalog}")
    endif()

    message(STATUS "${catalog}: ${message_count} finished messages")
endforeach()

list(SORT actual_languages)
list(SORT expected_languages)
if(NOT actual_languages STREQUAL expected_languages)
    message(FATAL_ERROR
        "Expected languages ${expected_languages}; found ${actual_languages}")
endif()

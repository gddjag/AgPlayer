if(NOT DEFINED ROOT OR ROOT STREQUAL "")
    message(FATAL_ERROR "ROOT must point to the AgPlayer source tree")
endif()

file(GLOB_RECURSE source_files LIST_DIRECTORIES FALSE
    "${ROOT}/app/qml/*.qml"
    "${ROOT}/installer/*.iss"
    "${ROOT}/installer/*.txt"
    "${ROOT}/translations/*.ts")

set(suspicious_tokens
    "闊" "鏂" "姝" "鍒" "绔" "鏈" "鎾" "鍔" "瀵" "缁"
    "鏅" "璁" "鍙" "璇" "锛" "鈥" "鈱" "鈰" "绗" "棣" "鐢"
    "鑹" "鏍" "娓" "浣" "闅" "涓" "璇" "鍏" "鏀" "淇")

set(failures)
foreach(source_file IN LISTS source_files)
    file(READ "${source_file}" content)
    foreach(token IN LISTS suspicious_tokens)
        string(FIND "${content}" "${token}" token_index)
        if(NOT token_index EQUAL -1)
            list(APPEND failures "${source_file}: suspicious mojibake token '${token}'")
            break()
        endif()
    endforeach()
endforeach()

if(failures)
    list(JOIN failures "\n" failure_text)
    message(FATAL_ERROR "AgPlayer source encoding check failed:\n${failure_text}")
endif()

message(STATUS "Checked ${source_files} for UTF-8 mojibake")

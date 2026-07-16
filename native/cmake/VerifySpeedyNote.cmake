set(QUIZAPP_SPEEDYNOTE_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/speedynote/upstream")
cmake_path(NORMAL_PATH QUIZAPP_SPEEDYNOTE_ROOT)

set(QUIZAPP_SPEEDYNOTE_COMMIT
    "dd5386366b4b1a51a6e960491feb82e777fbdcb2")
set(_quizapp_speedynote_metadata
    "${QUIZAPP_SPEEDYNOTE_ROOT}/.quizapp-upstream.json")

if(NOT EXISTS "${_quizapp_speedynote_metadata}")
    message(FATAL_ERROR
        "SpeedyNote metadata is missing. Run scripts/fetch-speedynote.ps1.")
endif()

file(READ "${_quizapp_speedynote_metadata}" _quizapp_speedynote_json)
string(JSON _quizapp_speedynote_actual_commit
    ERROR_VARIABLE _quizapp_speedynote_json_error
    GET "${_quizapp_speedynote_json}" commit)
if(_quizapp_speedynote_json_error)
    message(FATAL_ERROR
        "SpeedyNote metadata is invalid: ${_quizapp_speedynote_json_error}")
endif()
if(NOT _quizapp_speedynote_actual_commit STREQUAL QUIZAPP_SPEEDYNOTE_COMMIT)
    message(FATAL_ERROR
        "SpeedyNote commit mismatch. Expected ${QUIZAPP_SPEEDYNOTE_COMMIT}, "
        "found ${_quizapp_speedynote_actual_commit}.")
endif()

set(_quizapp_speedynote_fingerprints
    "source/core/Document.h|d35e09507f70ca12c68c2c5b83e247fab071494e22b95acd58c6db825bcf8416"
    "source/core/Document.cpp|3fb69e89a6691bab81322102dddd693b4cf2828980e81640cde4f1147d3f370c"
    "source/core/Page.h|6408e380f8462423556796ac1e513419442f0ab7d5da834e51a18a8e1b641360"
    "source/core/Page.cpp|f8dda4de83765feee3ca0e7da0a03976732ba4dd4469a3bcc8bc796f39e2e6ed"
    "source/core/DocumentViewport.h|24e0f6b5673270177626c3874b72af17aac4ef567d83ad2d6e8ab40d4e9cb165"
    "source/core/DocumentViewport.cpp|47d72c472d966172cbd160f79fd77a4343c2dbd244d7dd84eb3b2460af7bcb5d"
    "source/core/TouchGestureHandler.h|0992e915593d2736fc48be274dad3108864bab08a13b724238fde472b685e46c"
    "source/core/TouchGestureHandler.cpp|fead3990dcc115383b7027bbf023270e5d762acdce2d7d8ae12f38d144c194d1"
    "source/layers/VectorLayer.h|04ba4eaa8860877f6014828649b318eae7f01e109a8fd90b5225cc797d9ecacf"
    "source/strokes/StrokePoint.h|e0dbe492395bef29cf1b8dd3a13becda42712d1bcdeb2cbc134a8bd76e6f097e"
    "source/strokes/VectorStroke.h|deb7c0af90fc0dd371ac609961aac5fd2f020f7555152379f9b0db3528fe343f"
    "source/objects/InsertedObject.h|167f495a07234461f22e553a9a697d5bba6a26a6fd2e5a72b1a8495ca684f37a"
    "source/objects/InsertedObject.cpp|8a9f13911243d9c5e4d47246e02ce5934cb49e5dcfc4038e9c57ddd10bae7d54")

foreach(_quizapp_speedynote_entry IN LISTS _quizapp_speedynote_fingerprints)
    string(REPLACE "|" ";" _quizapp_speedynote_fields
        "${_quizapp_speedynote_entry}")
    list(GET _quizapp_speedynote_fields 0 _quizapp_speedynote_relative_path)
    list(GET _quizapp_speedynote_fields 1 _quizapp_speedynote_expected_hash)
    set(_quizapp_speedynote_path
        "${QUIZAPP_SPEEDYNOTE_ROOT}/${_quizapp_speedynote_relative_path}")
    if(NOT EXISTS "${_quizapp_speedynote_path}")
        message(FATAL_ERROR
            "Pinned SpeedyNote source is missing: ${_quizapp_speedynote_relative_path}")
    endif()
    file(SHA256 "${_quizapp_speedynote_path}" _quizapp_speedynote_actual_hash)
    if(NOT _quizapp_speedynote_actual_hash STREQUAL _quizapp_speedynote_expected_hash)
        message(FATAL_ERROR
            "Pinned SpeedyNote source changed: ${_quizapp_speedynote_relative_path}")
    endif()
endforeach()

message(STATUS
    "Verified SpeedyNote ${QUIZAPP_SPEEDYNOTE_COMMIT} source contract")


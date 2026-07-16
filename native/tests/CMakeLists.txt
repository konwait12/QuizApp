qt_add_executable(quizapp_native_core_tests
    tst_native_core.cpp
)

target_link_libraries(quizapp_native_core_tests PRIVATE quizapp_native_core Qt6::Test)
quizapp_copy_fsrs_runtime(quizapp_native_core_tests)
add_test(NAME quizapp_native_core_tests COMMAND quizapp_native_core_tests)

qt_add_executable(quizapp_native_ui_tests
    tst_app_shell.cpp
)
if(MSVC)
    set_source_files_properties(tst_app_shell.cpp PROPERTIES
        COMPILE_OPTIONS "/UQT_NO_CAST_FROM_ASCII;/UQT_NO_CAST_TO_ASCII"
    )
else()
    set_source_files_properties(tst_app_shell.cpp PROPERTIES
        COMPILE_OPTIONS "-UQT_NO_CAST_FROM_ASCII;-UQT_NO_CAST_TO_ASCII"
    )
endif()
target_link_libraries(quizapp_native_ui_tests PRIVATE quizapp_native_ui Qt6::Test Qt6::Widgets)
quizapp_copy_fsrs_runtime(quizapp_native_ui_tests)
add_test(NAME quizapp_native_ui_tests COMMAND quizapp_native_ui_tests)
set(quizapp_ui_test_environment
    "QUIZAPP_TEST_OUTPUT_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../../output/native-ui"
)
if(NOT WIN32)
    list(APPEND quizapp_ui_test_environment "QT_QPA_PLATFORM=offscreen")
endif()
set_tests_properties(quizapp_native_ui_tests PROPERTIES
    ENVIRONMENT "${quizapp_ui_test_environment}"
)

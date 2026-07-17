cmake_minimum_required(VERSION 3.24)

project(QuizAppNative VERSION 2.0.0 LANGUAGES CXX)

set(QUIZAPP_ANDROID_PACKAGE_NAME "com.quizapp" CACHE STRING
    "Android application package name")
set(QUIZAPP_ANDROID_APP_NAME "QuizApp" CACHE STRING
    "Android application label")
set(QUIZAPP_DEFAULT_BANK_BUNDLE_DIR "" CACHE PATH
    "Optional validated 27 postgraduate exam bank bundle to package in Android assets")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(CTest)
include(cmake/VerifySpeedyNote.cmake)
find_package(Qt6 6.9 REQUIRED COMPONENTS Concurrent Core Gui Sql Svg Widgets)
qt_policy(SET QTP0002 NEW)
if(BUILD_TESTING)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
endif()

set(QUIZAPP_CARGO_EXECUTABLE "" CACHE FILEPATH "Path to the Cargo executable")
set(QUIZAPP_CARGO_TARGET_DIR "${CMAKE_BINARY_DIR}/cargo" CACHE PATH
    "ASCII-only Cargo target directory")
if(NOT QUIZAPP_CARGO_EXECUTABLE)
    find_program(QUIZAPP_CARGO_EXECUTABLE cargo REQUIRED)
endif()

set(fsrs_bridge_root "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fsrs-bridge")
set(fsrs_bridge_target_dir "${QUIZAPP_CARGO_TARGET_DIR}")
if(ANDROID)
    set(fsrs_rust_target "aarch64-linux-android")
    set(fsrs_android_linker
        "${CMAKE_ANDROID_NDK}/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android26-clang.cmd")
    if(NOT EXISTS "${fsrs_android_linker}")
        message(FATAL_ERROR "Android Rust linker not found: ${fsrs_android_linker}")
    endif()
    set(fsrs_cargo_environment
        "CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER=${fsrs_android_linker}")
else()
    set(fsrs_rust_target "x86_64-pc-windows-gnu")
    set(fsrs_cargo_environment)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(fsrs_cargo_profile "release")
    set(fsrs_cargo_profile_argument "--release")
else()
    set(fsrs_cargo_profile "debug")
    set(fsrs_cargo_profile_argument)
endif()

if(ANDROID)
    set(fsrs_bridge_library
        "${fsrs_bridge_target_dir}/${fsrs_rust_target}/${fsrs_cargo_profile}/libquizapp_fsrs_bridge.so")
    set(fsrs_bridge_outputs "${fsrs_bridge_library}")
else()
    set(fsrs_bridge_library
        "${fsrs_bridge_target_dir}/${fsrs_rust_target}/${fsrs_cargo_profile}/quizapp_fsrs_bridge.dll")
    set(fsrs_bridge_import_library
        "${fsrs_bridge_target_dir}/${fsrs_rust_target}/${fsrs_cargo_profile}/libquizapp_fsrs_bridge.dll.a")
    set(fsrs_bridge_outputs "${fsrs_bridge_library}" "${fsrs_bridge_import_library}")
endif()
add_custom_command(
    OUTPUT ${fsrs_bridge_outputs}
    COMMAND "${CMAKE_COMMAND}" -E env
        ${fsrs_cargo_environment}
        "${QUIZAPP_CARGO_EXECUTABLE}" build
        --manifest-path "${fsrs_bridge_root}/Cargo.toml"
        --target "${fsrs_rust_target}"
        --target-dir "${fsrs_bridge_target_dir}"
        --locked
        ${fsrs_cargo_profile_argument}
    DEPENDS
        "${fsrs_bridge_root}/Cargo.toml"
        "${fsrs_bridge_root}/Cargo.lock"
        "${fsrs_bridge_root}/build.rs"
        "${fsrs_bridge_root}/src/lib.rs"
    WORKING_DIRECTORY "${fsrs_bridge_root}"
    VERBATIM
    COMMENT "Building pinned fsrs-rs scheduler bridge for ${fsrs_rust_target}"
)
add_custom_target(quizapp_fsrs_bridge_build DEPENDS ${fsrs_bridge_outputs})
add_library(quizapp_fsrs_bridge SHARED IMPORTED GLOBAL)
set_target_properties(quizapp_fsrs_bridge PROPERTIES IMPORTED_LOCATION "${fsrs_bridge_library}")
if(NOT ANDROID)
    set_target_properties(quizapp_fsrs_bridge PROPERTIES
        IMPORTED_IMPLIB "${fsrs_bridge_import_library}")
endif()

function(quizapp_copy_fsrs_runtime target_name)
    if(WIN32)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${fsrs_bridge_library}"
                "$<TARGET_FILE_DIR:${target_name}>"
            VERBATIM)
    endif()
endfunction()

add_library(quizapp_speedynote_adapter STATIC
    src/handwriting/SpeedyNoteStrokeAdapter.cpp
)

target_include_directories(quizapp_speedynote_adapter PUBLIC src)
target_include_directories(quizapp_speedynote_adapter PRIVATE
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/strokes"
)
target_link_libraries(quizapp_speedynote_adapter PUBLIC Qt6::Core Qt6::Gui)

add_library(quizapp_speedynote_core STATIC
    src/handwriting/SpeedyNotePdfBridge.cpp
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/core/DarkModeUtils.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/core/Document.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/core/DocumentViewport.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/core/MarkdownNote.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/core/Page.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/core/TouchGestureHandler.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/objects/ImageObject.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/objects/InsertedObject.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/objects/LinkObject.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/objects/OcrTextObject.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/objects/TextBoxObject.cpp"
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source/ui/banners/MissingPdfBanner.cpp"
)
target_include_directories(quizapp_speedynote_core PUBLIC
    src
    "${QUIZAPP_SPEEDYNOTE_ROOT}/source"
)
target_link_libraries(quizapp_speedynote_core PUBLIC
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

add_library(quizapp_native_core STATIC
    resources/quizapp-native.qrc
    src/domain/QuestionIdentity.cpp
    src/domain/QuestionOrdering.cpp
    src/platform/SharedStoragePlatform.cpp
    src/services/BankInstallService.cpp
    src/services/BankDirectorySyncService.cpp
    src/services/BlobStore.cpp
    src/services/DefaultBankBundleBootstrapService.cpp
    src/services/FsrsScheduler.cpp
    src/services/LegacyBankImporter.cpp
    src/services/PracticeService.cpp
    src/services/ReviewService.cpp
    src/services/SharedStorageService.cpp
    src/services/SharedStorageFileService.cpp
    src/services/StudyService.cpp
    src/services/WrongBookService.cpp
    src/services/XiaoyiDirectoryInstallService.cpp
    src/storage/Database.cpp
    src/storage/SqliteBankSourceRepository.cpp
    src/storage/SqliteLibraryRepository.cpp
    src/storage/SqlitePracticeRepository.cpp
    src/storage/SqliteQuestionRepository.cpp
    src/storage/SqliteReviewRepository.cpp
    src/storage/SqliteStudyRepository.cpp
    src/storage/SqliteWrongBookRepository.cpp
)

add_dependencies(quizapp_native_core quizapp_fsrs_bridge_build)
target_include_directories(quizapp_native_core PUBLIC
    src
    "${fsrs_bridge_root}/include"
)
target_link_libraries(quizapp_native_core PUBLIC
    quizapp_speedynote_adapter
    quizapp_fsrs_bridge
    Qt6::Core
    Qt6::Gui
    Qt6::Sql
)
if(ANDROID)
    target_link_libraries(quizapp_native_core PRIVATE dl log m)
elseif(MINGW)
    target_link_libraries(quizapp_native_core PRIVATE bcrypt dbghelp ntdll userenv ws2_32)
endif()
target_compile_definitions(quizapp_native_core PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)

add_library(quizapp_native_ui STATIC
    src/ui/AnswerTablePage.cpp
    src/ui/AppFont.cpp
    src/ui/AppWindow.cpp
    src/ui/EndfieldTheme.cpp
    src/ui/HandwritingPage.cpp
    src/ui/MaterialIconProvider.cpp
    src/ui/PracticePage.cpp
    src/ui/QuestionOverviewDialog.cpp
    src/ui/ReviewPage.cpp
    src/ui/StudyHubPage.cpp
    src/ui/StudyTrendChart.cpp
    src/ui/ThemePreview.cpp
    src/ui/ThemePalette.cpp
)
target_include_directories(quizapp_native_ui PUBLIC src)
target_link_libraries(quizapp_native_ui PUBLIC
    quizapp_native_core
    quizapp_speedynote_core
    Qt6::Concurrent
    Qt6::Svg
    Qt6::Widgets
)
target_compile_definitions(quizapp_native_ui PUBLIC QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII)
if(MSVC)
    set_source_files_properties(src/ui/HandwritingPage.cpp PROPERTIES
        COMPILE_OPTIONS "/UQT_NO_CAST_FROM_ASCII;/UQT_NO_CAST_TO_ASCII"
    )
else()
    set_source_files_properties(src/ui/HandwritingPage.cpp PROPERTIES
        COMPILE_OPTIONS "-UQT_NO_CAST_FROM_ASCII;-UQT_NO_CAST_TO_ASCII"
    )
endif()

qt_add_executable(quizapp_native_app
    MANUAL_FINALIZATION
    src/ui/main.cpp
)
target_link_libraries(quizapp_native_app PRIVATE quizapp_native_ui)
set_target_properties(quizapp_native_app PROPERTIES OUTPUT_NAME QuizApp)
quizapp_copy_fsrs_runtime(quizapp_native_app)

if(ANDROID)
    set(quizapp_android_package_source_dir "${CMAKE_CURRENT_SOURCE_DIR}/android")
    if(QUIZAPP_DEFAULT_BANK_BUNDLE_DIR)
        cmake_path(ABSOLUTE_PATH QUIZAPP_DEFAULT_BANK_BUNDLE_DIR
            NORMALIZE OUTPUT_VARIABLE quizapp_default_bank_bundle_dir)
        if(NOT EXISTS "${quizapp_default_bank_bundle_dir}/manifest.json"
            OR NOT EXISTS "${quizapp_default_bank_bundle_dir}/quizapp.sqlite"
            OR NOT IS_DIRECTORY "${quizapp_default_bank_bundle_dir}/blobs")
            message(FATAL_ERROR
                "QUIZAPP_DEFAULT_BANK_BUNDLE_DIR is not a complete bank bundle")
        endif()

        file(READ "${quizapp_default_bank_bundle_dir}/manifest.json"
            quizapp_default_bank_manifest)
        string(JSON quizapp_default_bank_display_name ERROR_VARIABLE manifest_error
            GET "${quizapp_default_bank_manifest}" displayName)
        if(manifest_error OR NOT quizapp_default_bank_display_name STREQUAL "27考研题库包")
            message(FATAL_ERROR
                "Default bank bundle displayName must be 27考研题库包")
        endif()

        set(quizapp_android_package_source_dir
            "${CMAKE_BINARY_DIR}/android-package")
        file(REMOVE_RECURSE "${quizapp_android_package_source_dir}")
        file(MAKE_DIRECTORY "${quizapp_android_package_source_dir}")
        file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/android/"
            DESTINATION "${quizapp_android_package_source_dir}")
        set(quizapp_android_bank_assets
            "${quizapp_android_package_source_dir}/assets/quizapp/27-postgraduate-bank-bundle")
        file(MAKE_DIRECTORY "${quizapp_android_bank_assets}")
        file(COPY "${quizapp_default_bank_bundle_dir}/"
            DESTINATION "${quizapp_android_bank_assets}")
        file(GLOB_RECURSE quizapp_default_bank_files
            LIST_DIRECTORIES FALSE
            RELATIVE "${quizapp_default_bank_bundle_dir}"
            "${quizapp_default_bank_bundle_dir}/*")
        list(SORT quizapp_default_bank_files)
        file(WRITE "${quizapp_android_bank_assets}/files.txt" "")
        foreach(bundle_file IN LISTS quizapp_default_bank_files)
            string(REPLACE "\\" "/" bundle_file "${bundle_file}")
            if(NOT bundle_file STREQUAL "files.txt")
                file(APPEND "${quizapp_android_bank_assets}/files.txt"
                    "${bundle_file}\n")
            endif()
        endforeach()
        target_compile_definitions(quizapp_native_app PRIVATE
            QUIZAPP_HAS_DEFAULT_BANK_BUNDLE=1)
        message(STATUS
            "Packaging Android default bank bundle: ${quizapp_default_bank_display_name}")
    endif()
    set_target_properties(quizapp_native_app PROPERTIES
        QT_ANDROID_PACKAGE_NAME "${QUIZAPP_ANDROID_PACKAGE_NAME}"
        QT_ANDROID_APP_NAME "${QUIZAPP_ANDROID_APP_NAME}"
        QT_ANDROID_PACKAGE_SOURCE_DIR "${quizapp_android_package_source_dir}"
        QT_ANDROID_VERSION_CODE 20003
        QT_ANDROID_VERSION_NAME "2.0.0-alpha.3"
        QT_ANDROID_MIN_SDK_VERSION 26
        QT_ANDROID_TARGET_SDK_VERSION 35
        QT_ANDROID_EXTRA_LIBS "${fsrs_bridge_library}"
    )
endif()

qt_finalize_executable(quizapp_native_app)

if(NOT ANDROID)
    qt_add_executable(quizapp_bank_bundle_tool
        src/tools/BankBundleTool.cpp
    )
    target_link_libraries(quizapp_bank_bundle_tool PRIVATE quizapp_native_core)
    set_target_properties(quizapp_bank_bundle_tool PROPERTIES
        OUTPUT_NAME QuizAppBankBundleTool
    )
    quizapp_copy_fsrs_runtime(quizapp_bank_bundle_tool)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

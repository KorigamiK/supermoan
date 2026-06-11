cmake_minimum_required(VERSION 3.28)
project(supermoan VERSION 1.0.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 26)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT APPLE)
  message(FATAL_ERROR "supermoan is macOS-only")
endif()

add_executable(supermoan MACOSX_BUNDLE
  src/main.cpp
  src/util.cpp
  src/recorder.cpp
  src/typist.cpp
  src/transcriber.mm
)

set_source_files_properties(src/transcriber.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

# Error flow is std::expected / status codes; exceptions and RTTI are off.
target_compile_options(supermoan PRIVATE -Wall -Wextra -fno-exceptions -fno-rtti)

# The app bundle exists so TCC attributes microphone access to us
# (NSMicrophoneUsageDescription) instead of the hotkey daemon that
# spawns us, which would get the process killed without a prompt.
set_target_properties(supermoan PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "Supermoan"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.supermoan"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  OUTPUT_NAME "Supermoan"
)

target_link_libraries(supermoan PRIVATE
  "-framework AudioToolbox"        # microphone capture (AudioQueue)
  "-framework ApplicationServices" # keystroke synthesis (CGEvent) + AX trust check
  "-framework Foundation"          # NSURLSession upload + NSJSONSerialization
)

# Ad-hoc signature: TCC needs a code identity to persist permission grants.
add_custom_command(TARGET supermoan POST_BUILD
  COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:supermoan>"
)

# cmake --install build --prefix ~  ->  ~/Applications/Supermoan.app
install(TARGETS supermoan BUNDLE DESTINATION Applications)

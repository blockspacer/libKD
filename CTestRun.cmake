set(CTEST_TEST_TIMEOUT 30)

if(DEFINED ENV{CTEST_CMAKE_GENERATOR})
    set(CTEST_CMAKE_GENERATOR "$ENV{CTEST_CMAKE_GENERATOR}")
elseif(UNIX)
    set(CTEST_CMAKE_GENERATOR "Unix Makefiles")
endif()

if(DEFINED ENV{CTEST_CONFIGURATION_TYPE})
    set(CTEST_CONFIGURATION_TYPE "$ENV{CTEST_CONFIGURATION_TYPE}")
else()
    set(CTEST_CONFIGURATION_TYPE "Debug")
endif()

# Compiler
if(UNIX)
    if(DEFINED ENV{CC})
        find_program(CC_FOUND NAMES $ENV{CC})
        if(NOT CC_FOUND)
            message( FATAL_ERROR "Compiler $ENV{CC} not found." )
        endif()
        string(SUBSTRING "$ENV{CC}" 0 3 CC)
        if(CC STREQUAL "gcc")
            set(CC_NAME "gcc")
            exec_program($ENV{CC} ARGS -dumpversion OUTPUT_VARIABLE CC_VERSION)
            if((CC_VERSION VERSION_GREATER 5) OR (CC_VERSION VERSION_EQUAL 5))
                string(SUBSTRING "${CC_VERSION}" 0 1 CC_VERSION)
            else()
                string(SUBSTRING "${CC_VERSION}" 0 3 CC_VERSION)
            endif()
        elseif(CC STREQUAL "icc")
            set(CC_NAME "icc")
            exec_program($ENV{CC} ARGS -dumpversion OUTPUT_VARIABLE CC_VERSION)
        endif()
        string(SUBSTRING "$ENV{CC}" 0 5 CC)
        if(CC STREQUAL "clang")
            if(APPLE)
                set(CC_NAME "xcode")
                exec_program(xcodebuild ARGS -version OUTPUT_VARIABLE CC_VERSION)
                string(REGEX REPLACE ".*Xcode ([0-9]+\\.[0-9]+).*" "\\1" CC_VERSION ${CC_VERSION})
            else()
                set(CC_NAME "clang")
                exec_program($ENV{CC} ARGS --version OUTPUT_VARIABLE CC_VERSION)
                string(REGEX REPLACE ".*clang version ([0-9]+\\.[0-9]+).*" "\\1" CC_VERSION ${CC_VERSION})
            endif()
        endif()
    else()
        set(CC_NAME "gcc")
        exec_program(${CC_NAME} ARGS -dumpversion OUTPUT_VARIABLE CC_VERSION)
        string(SUBSTRING "${CC_VERSION}" 0 3 CC_VERSION)
    endif()
else()
    set(CC_NAME "msvc")
    string(SUBSTRING $ENV{CTEST_CMAKE_GENERATOR} 14 2 CC_VERSION)
    if($ENV{CTEST_CMAKE_GENERATOR} MATCHES ".*ARM")
        set(CC_ARCH "arm")
    elseif($ENV{CTEST_CMAKE_GENERATOR} MATCHES ".*Win64")
        set(CC_ARCH "x86_64")
    else()
        set(CC_ARCH "x86")
    endif()
endif()

# CI service
if(DEFINED ENV{APPVEYOR})
    set(CTEST_SITE "Appveyor CI")
    string(SUBSTRING "$ENV{APPVEYOR_REPO_COMMIT}" 0 8 CI_COMMIT_ID)
    set(CTEST_BUILD_NAME "${CI_COMMIT_ID}-${CC_NAME}-${CC_VERSION}-${CC_ARCH}-($ENV{APPVEYOR_BUILD_NUMBER})")
    set(CTEST_SOURCE_DIRECTORY "$ENV{APPVEYOR_BUILD_FOLDER}")
elseif(DEFINED ENV{CIRCLECI})
    set(CTEST_SITE "Circle CI")
    string(SUBSTRING "$ENV{CIRCLE_SHA1}" 0 8 CI_COMMIT_ID)
    set(CTEST_BUILD_NAME "${CI_COMMIT_ID}-${CC_NAME}-${CC_VERSION}-($ENV{CIRCLE_BUILD_NUM})")
    set(CTEST_SOURCE_DIRECTORY "$ENV{HOME}/libKD")
elseif(DEFINED ENV{GITLAB_CI})
    set(CTEST_SITE "GitLab CI")
    string(SUBSTRING "$ENV{CI_BUILD_REF}" 0 8 CI_COMMIT_ID)
    set(CTEST_BUILD_NAME "${CI_COMMIT_ID}-${CC_NAME}-${CC_VERSION}-(-)")
    set(CTEST_SOURCE_DIRECTORY "$ENV{CI_PROJECT_DIR}")
elseif(DEFINED ENV{MAGNUM})
    set(CTEST_SITE "Magnum CI")
    string(SUBSTRING "$ENV{CI_COMMIT}" 0 8 CI_COMMIT_ID)
    set(CTEST_BUILD_NAME "${CI_COMMIT_ID}-${CC_NAME}-${CC_VERSION}-($ENV{CI_BUILD_NUMBER})")
    set(CTEST_SOURCE_DIRECTORY "$ENV{HOME}/libKD")
elseif(DEFINED ENV{SHIPPABLE})
    set(CTEST_SITE "Shippable CI")
    string(SUBSTRING "$ENV{COMMIT}" 0 8 CI_COMMIT_ID)
    set(CTEST_BUILD_NAME "${CI_COMMIT_ID}-${CC_NAME}-${CC_VERSION}-($ENV{SHIPPABLE_BUILD_NUMBER})")
    set(CTEST_SOURCE_DIRECTORY "$ENV{SHIPPABLE_REPO_DIR}")
elseif(DEFINED ENV{TRAVIS})
    set(CTEST_SITE "Travis CI")
    string(SUBSTRING "$ENV{TRAVIS_COMMIT}" 0 8 CI_COMMIT_ID)
    set(CTEST_BUILD_NAME "${CI_COMMIT_ID}-${CC_NAME}-${CC_VERSION}-($ENV{TRAVIS_BUILD_NUMBER})")
    set(CTEST_SOURCE_DIRECTORY "$ENV{TRAVIS_BUILD_DIR}")
elseif(DEFINED ENV{WERCKER_ROOT})
    set(CTEST_SITE "Wercker CI")
    string(SUBSTRING "$ENV{WERCKER_GIT_COMMIT}" 0 8 CI_COMMIT_ID)
    set(CTEST_BUILD_NAME "${CI_COMMIT_ID}-${CC_NAME}-${CC_VERSION}-(-)")
    set(CTEST_SOURCE_DIRECTORY "$ENV{WERCKER_ROOT}")
endif()
set(CTEST_BINARY_DIRECTORY "${CTEST_SOURCE_DIRECTORY}/build")

# Memory check
if(DEFINED ENV{CTEST_MEMORYCHECK_TYPE})
    set(CTEST_MEMORYCHECK_TYPE "$ENV{CTEST_MEMORYCHECK_TYPE}")
else()
    set(CTEST_MEMORYCHECK_TYPE "Valgrind")
    find_program(CTEST_MEMORYCHECK_COMMAND NAMES valgrind PATHS "/usr/local/Cellar/valgrind/3.11.0/bin")
    set(CTEST_MEMORYCHECK_COMMAND_OPTIONS "--track-origins=yes --leak-check=yes")
endif()

# Code coverage
if(DEFINED ENV{CC})
    if(CC_NAME STREQUAL "gcc")
        find_program(CTEST_COVERAGE_COMMAND NAMES gcov-${CC_VERSION})
    endif()
    if(CC_NAME STREQUAL "clang")
        find_program(CTEST_COVERAGE_COMMAND NAMES llvm-cov-${CC_VERSION})
        if((CC_VERSION VERSION_GREATER 3.7) OR (CC_VERSION VERSION_EQUAL 3.7))
            set(CTEST_COVERAGE_EXTRA_FLAGS "${CTEST_COVERAGE_EXTRA_FLAGS} gcov")
        endif()
    endif()
    if(CC_NAME STREQUAL "xcode")
        find_program(CTEST_COVERAGE_COMMAND NAMES llvm-cov PATHS /Library/Developer/CommandLineTools/usr/bin/)
        set(CTEST_COVERAGE_EXTRA_FLAGS "${CTEST_COVERAGE_EXTRA_FLAGS} gcov")
    endif()
    if(CC_NAME STREQUAL "icc")
        find_program(CTEST_COVERAGE_COMMAND NAMES codecov)
    endif()
else()
    find_program(CTEST_COVERAGE_COMMAND NAMES gcov)
endif()
set(CTEST_CUSTOM_COVERAGE_EXCLUDE ${CTEST_CUSTOM_COVERAGE_EXCLUDE} "/distribution/" "/examples/" "/thirdparty/" "/tests/" "/cov-int/" "/CMakeFiles/" "/usr/")

# Dashboard run
ctest_start(Continuous)
ctest_configure()
ctest_build()
ctest_test()
if(CTEST_COVERAGE_COMMAND)
    ctest_memcheck()
endif()
if(CTEST_COVERAGE_COMMAND)
    ctest_coverage()
endif()
ctest_submit()

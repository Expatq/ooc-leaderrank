include(FetchContent)
FetchContent_Declare(
	googletest
	URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
	URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

lr_executable(memhog SOURCES tools/memhog.cpp DEPS lrcore)
lr_executable(lr-tests SOURCES
	unit/aligned_io_test.cpp
	unit/bitmap_test.cpp
	unit/cli_options_test.cpp
	unit/csv_reader_test.cpp
	unit/engine_test.cpp
	unit/grid_builder_test.cpp
	unit/memory_budget_test.cpp
	unit/meta_test.cpp
	unit/partition_test.cpp
	unit/peak_rss_test.cpp
	DEPS lrcore lrgrid lrprepare lrrank GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(lr-tests)

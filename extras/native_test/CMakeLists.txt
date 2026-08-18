# Desktop native test suite - not part of the Arduino build (this whole
# extras/ directory is conventionally ignored by the Arduino IDE/CLI, which
# only look at src/ and examples/). See README.md's "Testing" section.

add_executable(self_test self_test.cpp)
target_link_libraries(self_test PRIVATE TinyMP2::TinyMP2)
add_test(NAME self_test COMMAND self_test)

add_executable(fuzz_test fuzz_test.cpp)
target_link_libraries(fuzz_test PRIVATE TinyMP2::TinyMP2)
add_test(NAME fuzz_test COMMAND fuzz_test)

# Utility, not a ctest: decodes an arbitrary .mp2 file to raw PCM so it can
# be diffed against a reference decoder's output by hand, e.g.
#   ./test_decode some.mp2 out.raw
add_executable(test_decode test_decode.cpp)
target_link_libraries(test_decode PRIVATE TinyMP2::TinyMP2)

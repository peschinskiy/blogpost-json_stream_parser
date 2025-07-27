#!/bin/bash
mkdir -p .bin/
run_tests() {
    local std="$1"
    local src="$2"
    local bin="$3"
    echo "Test $std version"
    g++ -g -O0 -Wall -Wextra -Wpedantic -Werror -std=$std "$src" -o ".bin/$bin"
    if test $? -ne 0
    then
        echo "Compilation failed for $src with $std. Skipping tests."
        return 1
    fi

    echo '1' | .bin/$bin | diff - <(echo "1")
    echo '"str"' | .bin/$bin | diff - <(echo '"str"')
    echo '{"k": 1}' | .bin/$bin | diff - <(echo '{"k":1}')
    echo '{"k": 1, "c": 2}' | .bin/$bin | diff - <(echo '{"k":1,"c":2}')
    echo '[1, 2, 3]' | .bin/$bin | diff - <(echo '[1,2,3]')
    echo '[-1, -2, -3]' | .bin/$bin | diff - <(echo '[-1,-2,-3]')
    echo '[1.1, 2.2, 3.3]' | .bin/$bin | diff - <(echo '[1.1,2.2,3.3]')
    echo '["1", "2", "3"]' | .bin/$bin | diff - <(echo '["1","2","3"]')
    echo '{"k": [1, 2]}' | .bin/$bin | diff - <(echo '{"k":[1,2]}')
    # error condition tests
    echo '1.1.2' | .bin/$bin 2>&1 1>/dev/null | diff - <(echo "JSON parse error: Multiple decimal points in number")
    echo 'foo' | .bin/$bin 2>&1 1>/dev/null | diff - <(echo "JSON parse error: Unexpected character: f")
    echo '"abc' | .bin/$bin 2>&1 1>/dev/null | diff - <(echo "JSON parse error: Unterminated string")
    echo '{"k":}' | .bin/$bin 2>&1 1>/dev/null | diff - <(echo "JSON parse error: Expected value")
    echo '{"a":1 "b":2}' | .bin/$bin 2>&1 1>/dev/null | diff - <(echo "JSON parse error: Expected ',' between object pairs")
    echo '{1:2}' | .bin/$bin 2>&1 1>/dev/null | diff - <(echo "JSON parse error: Expected string key")
    echo '{"a" 1}' | .bin/$bin 2>&1 1>/dev/null | diff - <(echo "JSON parse error: Expected ':' after key")
    echo '[1 2]' | .bin/$bin 2>&1 1>/dev/null | diff - <(echo "JSON parse error: Expected ',' between array elements")
    echo '{k:1}' | .bin/$bin 2>&1 1>/dev/null | diff - <(echo "JSON parse error: Unexpected character: k")
}

run_tests c++17 json_17.cpp json_17
run_tests c++23 json_23.cpp json_23

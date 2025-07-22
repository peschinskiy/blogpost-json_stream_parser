#!/bin/bash
mkdir -p .bin/
echo "Test C++17 version"

g++ -g -O0 -std=c++17 json_17.cpp -o .bin/json_17

echo "1" | .bin/json_17 | diff - <(echo "1")
echo '"str"' | .bin/json_17 | diff - <(echo '"str"')
echo "{\"k\": 1}" | .bin/json_17 | diff - <(echo "{\"k\":1}")
echo "[1, 2, 3]" | .bin/json_17 | diff - <(echo "[1,2,3]")
echo "[-1, -2, -3]" | .bin/json_17 | diff - <(echo "[-1,-2,-3]")
echo "[1.1, 2.2, 3.3]" | .bin/json_17 | diff - <(echo "[1.1,2.2,3.3]")
echo "[\"1\", \"2\", \"3\"]" | .bin/json_17 | diff - <(echo "[\"1\",\"2\",\"3\"]")
echo "{\"k\": [1, 2]}" | .bin/json_17 | diff - <(echo "{\"k\":[1,2]}")

echo "Test C++23 version"
g++ -g -O0 -std=c++23 json_23.cpp -o .bin/json_23

echo "1" | .bin/json_23 | diff - <(echo "1")
echo '"str"' | .bin/json_23 | diff - <(echo '"str"')
echo "{\"k\": 1}" | .bin/json_23 | diff - <(echo "{\"k\":1}")
echo "[1, 2, 3]" | .bin/json_23 | diff - <(echo "[1,2,3]")
echo "[-1, -2, -3]" | .bin/json_23 | diff - <(echo "[-1,-2,-3]")
echo "[1.1, 2.2, 3.3]" | .bin/json_23 | diff - <(echo "[1.1,2.2,3.3]")
echo "[\"1\", \"2\", \"3\"]" | .bin/json_23 | diff - <(echo "[\"1\",\"2\",\"3\"]")
echo "{\"k\": [1, 2]}" | .bin/json_23 | diff - <(echo "{\"k\":[1,2]}")
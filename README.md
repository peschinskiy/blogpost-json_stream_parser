# Streaming JSON parser/formatter

This project demonstrates simple one-file implementation of JSON stream parser. Two implementations provided: C++17 compatible and C++23 compatible. The goal is to illustrate the evolution of modern C++ standarts towards more expressive functional style of programming.

## Build

gcc
```
(tested with gcc 14.2.0)
g++ -std=c++23 json_23.cpp
```

clang
```
(tested with clang 19.1.1)
clang++ -std=c++23 json_23.cpp 
```

## Testing

```bash
./tests.sh
```

## Limitations

Project is kept simple for demonstration purposes, so there is some implementation limitations:

1. Parser ignores everything passed after valid json parsed. For example, "3.14,some values" is valid JSON number 3.14.
2. Parser do not allow to skip fields and values. Skipping of inner parsers will leave all parent parsers in invalid state.
3. Parser do not handle escape symbols in strings
4. Parser do not handle booleans (true/false) and null values
5. Parser do not handle some edge cases in negative and floating-point numbers parsing (like '1.', '.', '-.')
6. Some error handling is skipped or simplified


# json_stream_parser

This project implements simple one-file implementation of stream JSON parser. Two implementations provided: C++17 compatible and C++23 compatible. The goal is to demonstrate the evolution of modern C++ standarts towards more expressive functional style of programming.

## Build

gcc
```
(tested with gcc >= 13.3.0)
g++ -std=c++17 json_17.cpp
```

clang
```
(tested with clang >= 18.1.0)
clang++ -std=c++17 json_17.cpp 
```

msvc
```
not tested by now
```

## Testing

```bash
./tests.sh
```

## Limitations

Project is kept simple for demostration purposes, so there is some implementation limitations:

1. Parser ignores everything passed after valid json parsed. For example, "3.14,some values" is valid JSON number 3.14.
2. Parser do not allow to skip fields and values. Skipping of inner parsers will leave all parent parsers in invalid state.


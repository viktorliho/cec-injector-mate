#pragma once

#include <cstdlib>
#include <iostream>

#define CHECK(...)                                                             \
  do {                                                                         \
    if (!(__VA_ARGS__)) {                                                      \
      std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK(" #__VA_ARGS__     \
                ") failed\n";                                                 \
      return EXIT_FAILURE;                                                     \
    }                                                                          \
  } while (false)

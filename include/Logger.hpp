#ifndef LOGGER_HPP
#define LOGGER_HPP

#include <string>

namespace ConsoleColor {
  inline constexpr std::string RED = "\033[1;31m";
  inline constexpr std::string GREEN = "\033[1;32m";
  inline constexpr std::string YELLOW = "\033[1;33m";
  inline constexpr std::string RESET = "\033[0m";

  inline constexpr std::string BOLD_RED = "\033[1;31m";
  inline constexpr std::string BOLD_GREEN = "\033[1;32m";
  inline constexpr std::string BOLD_YELLOW = "\033[1;33m";
} // namespace ConsoleColor

#endif

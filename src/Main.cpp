#include "Lexer.hpp"
#include "Parser.hpp"
#include "FileHandler.hpp"
#include "Transpiler.hpp"
#include <iostream>
#include <ranges>

auto main
(
  int argc,
  char *argv[]
) -> std::int32_t {

  std::span<char const *const> _arguments
  {
    std::views::counted
    (
      argv, argc
    )
  };

  if
  (
    _arguments.size() < 2
  ) {

    std::cout << "Error: A file input pathway is required." << '\n';
    exit(1);

  }

  FileHandler::FileOpener file{_arguments[1]};

  TOML::Parser Parse
  {
    file.view(),
    _arguments[1]
  };

  Lexer::Lex
  (
    file.view(),
    [&Parse]
    (
      const Token::TokenData& token
    ) noexcept
    {
    Parse.ReceiveToken(token);
    }
  );

  TOML::NodeIdx Root = Parse.Parse();

  FileHandler::FileWriter Writer("config.sh");

  if
  (
    !Writer.IsOpen()
  ) {

    std::cout << "Error: Could not open output file target." << '\n';
    return 1;

  }

  Transpiler::Transpile Generate(Writer);
  Generate.Generate(Parse.GetArena(), Root);
}

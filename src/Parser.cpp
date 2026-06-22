#include "Parser.hpp"
#include "AST.hpp"
#include "Logger.hpp"
#include <vector>
#include <print>
#include <cstdlib>

namespace TOML
{
  Parser::Parser
  (
    std::string_view source_view,
    std::string_view filename
  ) noexcept : Sv_SourceView_(source_view), Sv_FileName_(filename)
  {

    this->Toks_.reserve(512);
    this->HashLineBeforeTok_.reserve(512);

    TOML::TableNode RootTable
    {
      .name = "",
      .FirstChildIndx = NodeIdx::None,
      .IsArrayOfTable = false,
      ._pad = {}
    };

    this->RootTableIdx = this->Arena_.EmplaceNode(std::move(RootTable));
    this->LastTableIdx = this->RootTableIdx;
    this->TableTailIdx = this->RootTableIdx;
    this->GlobalTailIdx = NodeIdx::None;
  }

  auto Parser::Peek() const noexcept -> Token::TokenType
  {

    if
    (
      this->Cursor_ >= this->Toks_.size()
    ) [[
      unlikely
    ]] return Token::TokenType::EndOfFile;

    return this->Toks_[this->Cursor_].Type;

  }

  auto Parser::Consume() noexcept
  -> const Token::TokenData&
  {
    return this->Toks_[this->Cursor_++];
  }

  auto Parser::StartOfStatement() const noexcept -> bool
  {
    if
    (
      this->Cursor_ >= this->Toks_.size()
    ) [[
      unlikely
    ]] return true;

    const auto Type = this->Toks_[this->Cursor_].Type;

    if
    (
      Type == Token::TokenType::LeftBracket ||
      Type == Token::TokenType::EndOfFile
    ) [[
      unlikely
    ]] return true;

    if
    (
      Type == Token::TokenType::Identifier
    ) [[
      likely
    ]] if
      (
        this->Cursor_ + 1 < this->Toks_.size()
      ) [[likely]] return this->Toks_[this->Cursor_ + 1].Type == Token::TokenType::Assign;

    return false;
  }

  auto Parser::ReportError
  (
    const Token::TokenData &Tokens,
    std::string_view Msg
  ) const noexcept -> void
  {

    std::println
    (
      stderr,
      "\033[1m{}:{}:{}:\033[0m {}{}: {}{}\033[0m",
      this->Sv_FileName_,
      Tokens.Lexer_Size_t_Line_,
      Tokens.Lexer_Size_t_Column_,
      ConsoleColor::BOLD_RED,
      "error",
      ConsoleColor::RESET,
      Msg
    );

    if
    (
      Tokens.Lexer_Size_t_Line_ == 0 ||
      this->Sv_SourceView_.empty()
    ) [[
      unlikely
    ]] std::exit(1);

    size_t st_LineStart_ = Tokens.Lexer_Size_t_Offset_;

    while
    (
      st_LineStart_ > 0 &&
      this->Sv_SourceView_[st_LineStart_ - 1] != '\n'
    ) st_LineStart_--;

    size_t st_LineEnd_ = Tokens.Lexer_Size_t_Offset_;

    while
    (
      st_LineEnd_ < this->Sv_SourceView_.size() &&
      this->Sv_SourceView_[st_LineEnd_] != '\n'
    ) st_LineEnd_++;

    std::string_view Sv_SourceLine_ = this->Sv_SourceView_.substr
    (st_LineStart_,
      st_LineEnd_ - st_LineStart_
    );

    std::println
    (
      stderr,
      "{:<5} | {}",
      Tokens.Lexer_Size_t_Line_,
      Sv_SourceLine_
    );
    const size_t kSt_PadLen_ = (Tokens.Lexer_Size_t_Column_ > 0)
                                ? (Tokens.Lexer_Size_t_Column_ - 1)
                                : 0u;

    std::string Pad(kSt_PadLen_, ' ');

    std::string Underline = "^";

    if
    (
      Tokens.Sv_Val_.length() > 1
    ) Underline.append(Tokens.Sv_Val_.length() - 1, '~');

    std::println
    (
      stderr,
      "      | {}{}{}{}",
      Pad,
      ConsoleColor::BOLD_RED,
      Underline,
      ConsoleColor::RESET
    );
    std::exit(1);
  }

  auto Parser::ParseTable() noexcept -> void
  {
    this->Consume(); // Consume '['
    bool IsArrayOfTable = false;


    if
    (
      this->Peek() == Token::TokenType::LeftBracket
    ) [[
      unlikely
    ]] {
      this->Consume();
      IsArrayOfTable = true;
    }

    const auto& FirstTok = this->Toks_[this->Cursor_];
    size_t StartOffset = FirstTok.Lexer_Size_t_Offset_;
    size_t EndOffset = StartOffset;

    while
    (
      this->Peek() != Token::TokenType::RightBracket &&
      this->Peek() != Token::TokenType::EndOfFile
    ) {
      const auto& Tok = this->Consume();
      EndOffset = Tok.Lexer_Size_t_Offset_ + Tok.Sv_Val_.length();
    }


    if
    (
      this->Peek() != Token::TokenType::RightBracket
    ) [[
      unlikely
    ]] this->ReportError(
      this->Toks_[this->Cursor_], "Expected closing ']' for group"
    );

    this->Consume();

    if
    (
      IsArrayOfTable
    ) [[
      unlikely
    ]] {

      if
      (
        this->Peek() != Token::TokenType::RightBracket
      ) [[
        unlikely
      ]] this->ReportError(
        this->Toks_[this->Cursor_],
        "Expected second closing ']' for array of tables."
      );

      this->Consume();
    }

    TableNode TablePayLoad
    {
      .name = this->Sv_SourceView_.substr(StartOffset, EndOffset - StartOffset),
      .FirstChildIndx = NodeIdx::None,
      .IsArrayOfTable = IsArrayOfTable,
      ._pad = {}
    };

    // If we are currently in the Root scope, save the tail of its children
    // So that we know exactly where to pick back up if a newline reset happens.

    if
    (
      this->LastTableIdx == this->RootTableIdx
    ) this->GlobalTailIdx = this->LastTableChildIndx_;

    const auto ka_NewTableIdx_ = this->Arena_.EmplaceNode(std::move(TablePayLoad));

    this->Arena_.GetNode(this->TableTailIdx).NextSiblingIndx = ka_NewTableIdx_;
    this->TableTailIdx = ka_NewTableIdx_;

    this->LastTableIdx = ka_NewTableIdx_;
    this->LastTableChildIndx_ = NodeIdx::None;
  }


  auto Parser::ParseArray(std::string_view KeyToken) noexcept -> NodeIdx
  {

    this->Consume();

    KeyValueNode KeyPayload
    {
      .Key = KeyToken,
      .Value = "",
      .TypeDisc = 4,
      ._pad = {}
    };
    const auto KeyValueIdx = this->Arena_.EmplaceNode(std::move(KeyPayload));

    ArrayNode ArrayPayload { .FirstChildIndx = NodeIdx::None };
    const auto ArrayIndx = this->Arena_.EmplaceNode(std::move(ArrayPayload));

    this->Arena_.GetNode(KeyValueIdx).NextSiblingIndx = ArrayIndx;

    NodeIdx ArrayChildTail = NodeIdx::None;

    while
    (
      this->Peek() != Token::TokenType::RightBracket &&
      this->Peek() != Token::TokenType::EndOfFile
    ) {

      const auto ElementIdx = this->ParseValue("");

      /* The chain returned by ParseValue is at most 2 nodes long*/
      const auto& ElemHead = this->Arena_.GetNode(ElementIdx);
      const NodeIdx ElementTail = (ElemHead.NextSiblingIndx != NodeIdx::None)
                                  ? ElemHead.NextSiblingIndx
                                  : ElementIdx;

      auto& ArrayData = std::get<ArrayNode>(
        this->Arena_.GetNode(ArrayIndx).Payload
      );


      if
      (
        ArrayChildTail == NodeIdx::None
      ) [[
        unlikely
      ]] ArrayData.FirstChildIndx = ElementIdx;

      else [[
        /* nullAttr */
      ]] this->Arena_.GetNode(ArrayChildTail).NextSiblingIndx = ElementIdx;

      ArrayChildTail = ElementTail;

      if
      (
        this->Peek() == Token::TokenType::Comma
      ) {
        this->Consume();

        if
        (
          this->Peek() == Token::TokenType::RightBracket
        ) [[
          unlikely
        ]] this->ReportError(
          this->Toks_[this->Cursor_],
          "Expected another array element but got ']'"
        );

      }

      else if
      (
        this->Peek() == Token::TokenType::RightBracket
      ) break;

      else [[
        unlikely
      ]] this->ReportError(
        this->Toks_[this->Cursor_],
        "Expected ',' or ']' after array elements."
      );

    }

    if
    (
      this->Peek() != Token::TokenType::RightBracket
    ) [[
      unlikely
    ]] this->ReportError(
      this->Toks_[this->Cursor_],
      "Unterminated array block, missing closing ']'"
    );

    this->Consume();

    return KeyValueIdx;
  }

  auto Parser::ParseInlineTable(std::string_view KeyToken) noexcept -> NodeIdx
  {
    this->Consume();

      KeyValueNode KeyPayload
      {
        .Key = KeyToken,
        .Value = "",
        .TypeDisc = 5,
        ._pad = {}
      };

      const NodeIdx KeyValueIndx = this->Arena_.EmplaceNode(
        std::move(KeyPayload)
      );

      InlineTableNode InlinePayload { .FirstChildIndx = NodeIdx::None };
      const auto InlineIndx = this->Arena_.EmplaceNode(
        std::move(InlinePayload)
      );

      this->Arena_.GetNode(KeyValueIndx).NextSiblingIndx = InlineIndx;

      NodeIdx InlineChildTail = NodeIdx::None;

      while
      (
        this->Peek() != Token::TokenType::RightBrace &&
        this->Peek() != Token::TokenType::EndOfFile
      ) {

        if
        (
          this->Peek() != Token::TokenType::Identifier
        ) [[
          unlikely
        ]] this->ReportError(
          this->Toks_[this->Cursor_],
          "Expected an identifier"
        );

        const auto PropKeyToken = this->Consume();

        if
        (
          this->Peek() != Token::TokenType::Assign
        ) [[
          unlikely
        ]] this->ReportError(
          this->Toks_[this->Cursor_],
          "Expected '=' inside inline table."
        );

        this->Consume();

        NodeIdx PropValueToken = this->ParseValue(PropKeyToken.Sv_Val_);

        const auto& PropHead = this->Arena_.GetNode(PropValueToken);
        const NodeIdx PropTail = (PropHead.NextSiblingIndx != NodeIdx::None)
                                  ? PropHead.NextSiblingIndx
                                  : PropValueToken;

        auto& InlineData = std::get<InlineTableNode>(
          this->Arena_.GetNode(InlineIndx).Payload
        );

        if
        (
          InlineChildTail == NodeIdx::None
        ) [[
          unlikely
        ]] InlineData.FirstChildIndx = PropValueToken;

        else [[
          /* nullAttr */
        ]] this->Arena_.GetNode(InlineChildTail).NextSiblingIndx = PropValueToken;

        InlineChildTail = PropTail;

        if
        (
          this->Peek() == Token::TokenType::Comma
        ) {

          this->Consume();

          if
          (
            this->Peek() == Token::TokenType::RightBrace
          ) [[
            unlikely
          ]] this->ReportError(
            this->Toks_[this->Cursor_],
            "Expected another key-value pair"
          );

        }

        else if
        (
          this->Peek() == Token::TokenType::RightBrace
        ) break;

        else [[
          unlikely
        ]] this->ReportError(
          this->Toks_[this->Cursor_],
          "Expected ',' or '}' after inline table."
        );

      }

      if
      (
        this->Peek() != Token::TokenType::RightBrace
      ) [[
        unlikely
      ]] this->ReportError(
        this->Toks_[this->Cursor_],
        "Unterminated inline table block, missing '}'"
      );

      this->Consume();

      return KeyValueIndx;
  }

  auto Parser::ParseScalar(std::string_view KeyToken) noexcept -> NodeIdx
  {

    const auto& StartValueToken = this->Toks_[this->Cursor_];

    if
    (
      StartValueToken.Type == Token::TokenType::EndOfFile ||
      this->StartOfStatement()
    ) [[
      unlikely
    ]] this->ReportError(
      StartValueToken,
      "Missing value assignment after '='"
    );

    const size_t StartOffset = StartValueToken.Lexer_Size_t_Offset_;
    size_t EndOffset = StartOffset + StartValueToken.Sv_Val_.length();


    while
    (
      !this->StartOfStatement() &&
      this->Peek() != Token::TokenType::Comma &&
      this->Peek() != Token::TokenType::RightBracket &&
      this->Peek() != Token::TokenType::RightBrace &&
      this->Peek() != Token::TokenType::EndOfFile
    ) {
      const auto& ka_ContentToken_ = this->Consume();
      EndOffset = ka_ContentToken_.Lexer_Size_t_Offset_ + ka_ContentToken_.Sv_Val_.length();
    }

    const auto CombinedLength = EndOffset - StartOffset;

    std::string_view ValueView = this->Sv_SourceView_.substr(StartOffset, CombinedLength);

    KeyValueNode ValuePayload
    {
      .Key = KeyToken,
      .Value = ValueView,
      .TypeDisc = static_cast<std::uint32_t>(StartValueToken.Type),
      ._pad = {}
    };

    return this->Arena_.EmplaceNode(
      std::move(ValuePayload)
    );

  }

  auto Parser::ParseValue(std::string_view KeyToken) noexcept -> NodeIdx
  {

    if
    (
      this->Peek() == Token::TokenType::LeftBrace
    ) [[
      unlikely
    ]] return this->ParseInlineTable(KeyToken);

    else if
    (
      this->Peek() == Token::TokenType::LeftBracket
    ) [[
      unlikely
    ]] return this->ParseArray(KeyToken);

    return this->ParseScalar(KeyToken);

  }

  auto Parser::ParseKeyValue() noexcept -> void
  {

    if
    (
      this->LastTableIdx == NodeIdx::None
    ) [[
      unlikely
    ]] this->ReportError(
      this->Toks_[this->Cursor_],
      "Key-value pair declared outside of a group block."
    );

    const auto& KeyToken = this->Consume();

    if
    (
      this->Peek() != Token::TokenType::Assign
    ) [[
      unlikely
    ]] this->ReportError(
      this->Toks_[this->Cursor_],
      "Expected '=' operator after key identifier"
    );

    this->Consume();

    const NodeIdx ValueChainHead = this->ParseValue(KeyToken.Sv_Val_);

    auto& TableData = std::get<TableNode>(
      this->Arena_.GetNode(this->LastTableIdx).Payload
    );

    if
    (
      this->LastTableChildIndx_ == NodeIdx::None
    ) [[
      unlikely
    ]] TableData.FirstChildIndx = ValueChainHead;

    else [[
      /* nullAttr */
    ]] this->Arena_.GetNode(this->LastTableChildIndx_).NextSiblingIndx = ValueChainHead;

    const auto& ChainHead = this->Arena_.GetNode(ValueChainHead);
    this->LastTableChildIndx_ = (ChainHead.NextSiblingIndx != NodeIdx::None)
                                ? ChainHead.NextSiblingIndx
                                : ValueChainHead;
  }

  auto Parser::ReceiveToken(const Token::TokenData& Token) noexcept -> void
  {

    if
    (
      Token.Type == Token::TokenType::Hash
    ) [[
      unlikely
    ]] {
      this->PendingHashLine_ = Token.Lexer_Size_t_Line_;
      return;
    }

    if
    (
      Token.Type != Token::TokenType::Unknown
    ) [[
      likely
    ]] {
      this->HashLineBeforeTok_.emplace_back(this->PendingHashLine_);
      this->PendingHashLine_ = 0;
      this->Toks_.emplace_back(Token);
    }

    else [[
      unlikely
    ]] this->ReportError(
      Token,
      "Encountered unrecognized Lexer Symbol Context"
    );

  }

  auto Parser::Parse() noexcept -> NodeIdx
  {
    std::size_t _st_LastParsedLine_ {}; // We need this back to track line gaps

    while
    (
      this->Cursor_ < this->Toks_.size() &&
      this->Toks_[this->Cursor_].Type != Token::TokenType::EndOfFile
    ) {

      const auto& Currentoken = this->Toks_[this->Cursor_];
      const std::size_t HashLineBefore = this->HashLineBeforeTok_[this->Cursor_];

      const size_t LastSeen = (HashLineBefore > _st_LastParsedLine_)
                              ? HashLineBefore
                              : _st_LastParsedLine_;

      // Detect line gap > 1 to reset scope to Global Namespace
      if
      (
        LastSeen != 0 &&
        (
          Currentoken.Lexer_Size_t_Line_ - LastSeen
        ) > 1
      ) [[
        unlikely
      ]] {
        // Only reset if we aren't ALREADY in the root scope

        if
        (
          this->LastTableIdx != this->RootTableIdx
        ) {
          this->LastTableIdx = this->RootTableIdx;
          this->LastTableChildIndx_ = this->GlobalTailIdx; // O(1) Scope Restore!

        }

      }

      if
      (
        this->Peek() == Token::TokenType::LeftBracket
      ) [[
        unlikely
      ]] this->ParseTable();

      else if
      (
        this->Peek() == Token::TokenType::Identifier
      ) [[
        likely
      ]] this->ParseKeyValue();

      else [[
        unlikely
      ]] this->ReportError(this->Toks_[this->Cursor_], "Unexpected structural token sequence");

      if
      (
        this->Cursor_ > 0
      ) [[
        likely
      ]] _st_LastParsedLine_ = this->Toks_[this->Cursor_ - 1].Lexer_Size_t_Line_;
    }

    return this->RootTableIdx;

  }

  auto Parser::GetArena() const noexcept -> const ASTArena&
  {
    return this->Arena_;
  }
} // namespace TOML

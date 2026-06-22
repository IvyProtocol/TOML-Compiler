#include "AST.hpp"
#include "Transpiler.hpp"
#include <array>
#include <charconv>
#include <variant>

namespace Transpiler {

Transpile::Transpile(FileHandler::FileWriter &Writer) noexcept
    : Writer_(Writer) {}


auto Transpile::Generate
(
  const TOML::ASTArena &Arena,
  TOML::NodeIdx RootIdx
) noexcept -> void

{
  this->Buffer_.clear();
  this->Buffer_.reserve(8192);

  std::string Prefix;
  Prefix.reserve(128);

  this->Traverse(Arena, RootIdx, Prefix);
  this->Writer_.Write(this->Buffer_);
}


auto Transpile::Traverse
(
  const TOML::ASTArena &Arena,
  TOML::NodeIdx CurrentIdx,
  std::string &CurrentPrefix
) noexcept -> void
{

  while
  (
    CurrentIdx != TOML::NodeIdx::None
  ) {

    const auto &Node = Arena.GetNode(CurrentIdx);

    // TableNode
    if
    (
      const auto *Table = std::get_if<TOML::TableNode>(&Node.Payload)
    ) {

      const size_t OldLen = CurrentPrefix.size();

      if
      (
        !Table->name.empty()
      ) {

        for
        (
          const char C : Table->name
        ) {

          const char Mapped = (
            C == '.' || C == '-'
          ) ? '_' : C;

          CurrentPrefix.push_back(
            static_cast<char>(
              std::toupper(static_cast<unsigned char>(Mapped)))
          );

        }

        CurrentPrefix.push_back('_');

      }

      this->Traverse(Arena, Table->FirstChildIndx, CurrentPrefix);
      CurrentPrefix.resize(OldLen);

    }

    // ── KeyValueNode ──────────────────────────────────────
    else if
    (
      const auto *KV = std::get_if<TOML::KeyValueNode>(&Node.Payload)
    ) {

      const auto *NextNode = (Node.NextSiblingIndx != TOML::NodeIdx::None)
                                 ? &Arena.GetNode(Node.NextSiblingIndx)
                                 : nullptr;

      const auto *NextArray =
          NextNode ? std::get_if<TOML::ArrayNode>(&NextNode->Payload) : nullptr;

      const auto *NextInline =
          (NextNode && !NextArray)
              ? std::get_if<TOML::InlineTableNode>(&NextNode->Payload)
              : nullptr;

      if
      (
        NextArray
      ) {

        if
        (
          !KV->Key.empty()
        ) {

          const size_t MarkStart = this->Buffer_.size();
          this->Buffer_.append(CurrentPrefix);

          for
          (
            const char C : KV->Key
          ) this->Buffer_.push_back(
            static_cast<char>(std::toupper(static_cast<unsigned char>(C)))
          );

          const std::string ArrDecl
          (
            this->Buffer_,
            MarkStart,
            this->Buffer_.size() - MarkStart
          );

          this->Buffer_.resize(MarkStart); // trim — content is in ArrDecl

          // Pre-scan: does this array contain inline-table elements?
          bool ContainsComplex = false;

          {
            auto ScanIdx = NextArray->FirstChildIndx;

            while
            (
              ScanIdx != TOML::NodeIdx::None
            ) {

              const auto &SN = Arena.GetNode(ScanIdx);
              if
              (
                SN.NextSiblingIndx != TOML::NodeIdx::None &&
                std::holds_alternative<TOML::InlineTableNode>(
                  Arena.GetNode(SN.NextSiblingIndx).Payload
                )
              ) {

                ContainsComplex = true;
                break;

              }

              ScanIdx = SN.NextSiblingIndx;

            }
          }

          if
          (
            ContainsComplex
          ) this->Buffer_.append("declare -A ").append(ArrDecl).append("\n");
          else [[
            /* nullAttr */
          ]] this->Buffer_.append("export ").append(ArrDecl).append("=( ");

          int IndexCounter = 0;

          auto ProcessElements = [&]
          (
            auto &ElemSelf,
            TOML::NodeIdx ElemIdx
          ) -> void
          {

            while
            (
              ElemIdx != TOML::NodeIdx::None
            ) {

              const auto &EN = Arena.GetNode(ElemIdx);
              const auto *EP = std::get_if<TOML::KeyValueNode>(&EN.Payload);

              const auto *ElemNextNode =
                  (EN.NextSiblingIndx != TOML::NodeIdx::None)
                      ? &Arena.GetNode(EN.NextSiblingIndx)
                      : nullptr;

              const auto *ElemArr =
                  ElemNextNode
                      ? std::get_if<TOML::ArrayNode>(&ElemNextNode->Payload)
                      : nullptr;

              const auto *ElemInl = (ElemNextNode && !ElemArr)
                                        ? std::get_if<TOML::InlineTableNode>(
                                              &ElemNextNode->Payload)
                                        : nullptr;

              if
              (
                ElemArr
              ) {

                ElemSelf(ElemSelf, ElemArr->FirstChildIndx);
                ElemIdx = EN.NextSiblingIndx; // skip to ArrayNode

              }

              else if
              (
                ElemInl
              ) {
                // Inline-table element: emit associative-array
                // assignments keyed by "<index>_<prop>".

                auto ProcessInline = [&]
                (
                  auto &InlSelf,
                  TOML::NodeIdx PropIdx,
                  const std::string &KeyPfx
                ) -> void
                {

                  while
                  (
                    PropIdx != TOML::NodeIdx::None
                  ) {

                    const auto &PN = Arena.GetNode(PropIdx);
                    const auto *PP =
                        std::get_if<TOML::KeyValueNode>(&PN.Payload);

                    const auto *PNextNode =
                        (PN.NextSiblingIndx != TOML::NodeIdx::None)
                            ? &Arena.GetNode(PN.NextSiblingIndx)
                            : nullptr;

                    const auto *PropArr =
                        PNextNode
                            ? std::get_if<TOML::ArrayNode>(&PNextNode->Payload)
                            : nullptr;

                    const auto *PropInl =
                        (PNextNode && !PropArr)
                            ? std::get_if<TOML::InlineTableNode>(
                                  &PNextNode->Payload)
                            : nullptr;

                    if
                    (
                      PropArr
                    ) {

                      this->Buffer_.append(ArrDecl)
                          .append("[\"")
                          .append(KeyPfx)
                          .append(PP->Key)
                          .append("\"]=( ");

                      for
                      (
                        auto EIdx = PropArr->FirstChildIndx;
                        EIdx != TOML::NodeIdx::None;
                        EIdx = Arena.GetNode(EIdx).NextSiblingIndx
                      ) {

                        const auto *EProp = std::get_if<TOML::KeyValueNode>(
                            &Arena.GetNode(EIdx).Payload
                        );

                        this->Buffer_.append(EProp->Value).push_back(' ');

                      }

                      this->Buffer_.append(")\n");
                      PropIdx = PN.NextSiblingIndx;

                    }

                    else if
                    (
                      PropInl
                    ) {

                      // Nested inline table: recurse with extended key.

                      std::string NestedPfx;
                      NestedPfx.reserve(KeyPfx.size() + PP->Key.size() + 1);
                      NestedPfx.append(KeyPfx).append(PP->Key).push_back('_');
                      InlSelf(InlSelf, PropInl->FirstChildIndx, NestedPfx);
                      PropIdx = PN.NextSiblingIndx;

                    }

                    else [[
                      /* nullAttr */
                    ]] {

                      this->Buffer_.append(ArrDecl)
                          .append("[\"")
                          .append(KeyPfx)
                          .append(PP->Key)
                          .append("\"]=")
                          .append(PP->Value)
                          .append("\n");
                    }

                    PropIdx = Arena.GetNode(PropIdx).NextSiblingIndx;

                  }
                };

                std::array<char, 20> NumBuf{};
                const char *const NumEnd =
                    std::to_chars
                    (
                      NumBuf.data(),
                      NumBuf.end(),
                      IndexCounter
                    ).ptr;

                std::string BasePrefix(static_cast<const char*>(NumBuf.data()), NumEnd);
                BasePrefix.push_back('_');

                ProcessInline(ProcessInline, ElemInl->FirstChildIndx,
                              BasePrefix);
                ElemIdx = EN.NextSiblingIndx; // skip to InlineTableNode
                IndexCounter++;

              }

              else [[
                /* nullAttr */
              ]] {

                if
                (
                  ContainsComplex
                ) {

                  // Emit: ARRNAME["<index>"]=value

                  std::array<char, 20> NB{};
                  const char *const NE =
                      std::to_chars
                      (
                        NB.data(),
                        NB.end(),
                        IndexCounter
                      ).ptr;

                  this->Buffer_.append(ArrDecl)
                      .append("[\"")
                      .append(static_cast<const char*>(NB.data()), NE)
                      .append("\"]=")
                      .append(EP->Value)
                      .append("\n");
                  IndexCounter++;

                }

                else [[
                  /* nullAttr */
                ]] this->Buffer_.append(EP->Value).push_back(' ');

              }

              ElemIdx = Arena.GetNode(ElemIdx).NextSiblingIndx;

            }
          };

          ProcessElements
          (
            ProcessElements,
            NextArray->FirstChildIndx
          );

          if
          (
            !ContainsComplex
          ) this->Buffer_.append(")\n");

        }

        CurrentIdx = Node.NextSiblingIndx; // point to ArrayNode so
        // the bottom-of-loop advance lands on the next KV sibling.

      }

      // ── Inline-table value ────────────────────────────
      else if
      (
        NextInline
      ) {

        if
        (
          !KV->Key.empty()
        ) {

          // Build MapName into Buffer_, snapshot, trim.

          const size_t MarkStart = this->Buffer_.size();
          this->Buffer_.append(CurrentPrefix);

          for
          (
            const char C : KV->Key
          ) this->Buffer_.push_back(
            static_cast<char>(std::toupper(static_cast<unsigned char>(C)))
          );

          const std::string MapName
          (
            this->Buffer_,
            MarkStart,
            this->Buffer_.size() - MarkStart
          );

          this->Buffer_.resize(MarkStart);

          this->Buffer_.append("declare -A ")
                       .append(MapName)
                       .append("\n");

          auto ProcessProps = [&]
          (
            auto &PropSelf,
            TOML::NodeIdx PropIdx,
            const std::string &KeyPfx
          ) -> void
          {

            while
            (
              PropIdx != TOML::NodeIdx::None
            ) {

              const auto &PN = Arena.GetNode(PropIdx);
              const auto *PP = std::get_if<TOML::KeyValueNode>(&PN.Payload);

              const auto *PNextNode =
                  (PN.NextSiblingIndx != TOML::NodeIdx::None)
                      ? &Arena.GetNode(PN.NextSiblingIndx)
                      : nullptr;

              const auto *PropArr =
                  PNextNode ? std::get_if<TOML::ArrayNode>(&PNextNode->Payload)
                            : nullptr;

              const auto *PropInl =
                  (PNextNode && !PropArr)
                      ? std::get_if<TOML::InlineTableNode>(&PNextNode->Payload)
                      : nullptr;

              if
              (
                PropArr
              ) {

                  this->Buffer_.append(MapName)
                    .append("[\"")
                    .append(KeyPfx)
                    .append(PP->Key)
                    .append("\"]=( ");

                  for
                  (
                    auto EIdx = PropArr->FirstChildIndx;
                    EIdx != TOML::NodeIdx::None;
                    EIdx = Arena.GetNode(EIdx).NextSiblingIndx
                  ) {

                  const auto *EProp = std::get_if<TOML::KeyValueNode>(
                    &Arena.GetNode(EIdx).Payload
                  );

                  this->Buffer_.append(EProp->Value).push_back(' ');

                }

                this->Buffer_.append(")\n");
                PropIdx = PN.NextSiblingIndx;

              }

              else if
              (
                PropInl
              ) {

                std::string NestedPfx;

                NestedPfx.reserve(KeyPfx.size() + PP->Key.size() + 1);

                NestedPfx.append(KeyPfx)
                         .append(PP->Key)
                         .push_back('_');
                PropSelf(PropSelf, PropInl->FirstChildIndx, NestedPfx);
                PropIdx = PN.NextSiblingIndx;

              }

              else [[
                /* nullAttr */
              ]] {

                this->Buffer_.append(MapName)
                             .append("[\"")
                             .append(KeyPfx)
                             .append(PP->Key)
                             .append("\"]=")
                             .append(PP->Value)
                             .append("\n");

              }

              PropIdx = Arena.GetNode(PropIdx).NextSiblingIndx;

            }
          };

          ProcessProps
          (
            ProcessProps,
            NextInline->FirstChildIndx,
            ""
          );

        }

        CurrentIdx = Node.NextSiblingIndx; // point to InlineTableNode
        // bottom-of-loop advance lands on the next KV sibling.

      }

      // ── Scalar value ──────────────────────────────────
      else [[
        /* nullAttr */
      ]] {

        if
        (
          !KV->Key.empty()
        ) {

          this->Buffer_.append("export ").append(CurrentPrefix);

          for
          (
            const char C : KV->Key
          ) this->Buffer_.push_back(
            static_cast<char>(std::toupper(static_cast<unsigned char>(C)))
          );

          this->Buffer_.append("=").append(KV->Value).append("\n");

        }

      }
    }

    CurrentIdx = Arena.GetNode(CurrentIdx).NextSiblingIndx;
  }
}

} // namespace Transpiler

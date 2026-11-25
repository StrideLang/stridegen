/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#ifndef TYPECASTRESOLVER_HPP
#define TYPECASTRESOLVER_HPP

#include "resolvercontext.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/streamnode.h"
#include "stride/utils/astquery.h"

#include <string>

namespace strd {

class TypeCastResolver {
public:
  explicit TypeCastResolver(ResolverContext &ctx) : m_ctx(ctx) {}

  void resolveTypeCasting();

  static void setTypecast(const ASTNode &node, const std::string &from,
                          const std::string &to);
  static void widenOperands(const ASTNode &expression, const std::string &type,
                            const ScopeStack &scopeStack, const ASTNode &tree);

private:
  void resolveTypeCastForNode(const ASTNode &node, const ScopeStack &scopeStack,
                              const ASTNode &tree);
  void resolveTypeCastForStream(const std::shared_ptr<StreamNode> &stream,
                                const ScopeStack &scopeStack,
                                const ASTNode &tree);
  void resolveTypeCastForDeclaration(
      const std::shared_ptr<DeclarationNode> &decl, ScopeStack scopeStack,
      const ASTNode &tree);

  ResolverContext &m_ctx;
};

} // namespace strd

#endif // TYPECASTRESOLVER_HPP

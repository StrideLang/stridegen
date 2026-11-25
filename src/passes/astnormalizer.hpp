/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#ifndef ASTNORMALIZER_HPP
#define ASTNORMALIZER_HPP

#include "resolvercontext.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/utils/astquery.h"

#include <set>

namespace strd {

class AstNormalizer {
public:
  explicit AstNormalizer(ResolverContext &ctx) : m_ctx(ctx) {}

  void desugar();
  void hoistDeclarationChildren();
  void declareModuleInternalEntities();
  void resolveConstants();
  void resolveSelfPropertyAccesses();

  static std::shared_ptr<DeclarationNode>
  createSignalDeclaration(const std::string &name, int size,
                          const ScopeStack &scope, const ASTNode &tree);

private:
  void desugarSingleEntryStreams(const ASTNode &node,
                                 std::set<const void *> &visited);
  void declareInternalEntitiesForNode(const ASTNode &node, ScopeStack subScope);
  void resolveSelfPropertyAccessesForNode(const ASTNode &node);

  ResolverContext &m_ctx;
};

} // namespace strd

#endif // ASTNORMALIZER_HPP

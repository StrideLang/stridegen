/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#ifndef RATEANDTYPERESOLVER_HPP
#define RATEANDTYPERESOLVER_HPP

#include "resolvercontext.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/streamnode.h"
#include "stride/utils/astquery.h"

#include <set>
#include <string>

namespace strd {

class RateAndTypeResolver {
public:
  explicit RateAndTypeResolver(ResolverContext &ctx) : m_ctx(ctx) {}

  void setPortEntityAttributes(const ScopeStack &scope, const ASTNode &tree);
  void resolveRatesAndTypes(const ScopeStack &scope, const ASTNode &tree);
  void resolveDeclarationDataTypes();
  void setNodeRate(const ASTNode &node, double rate, const ScopeStack &scope,
                   const ASTNode &tree);

private:
  void setPortEntityAttributesForDeclaration(
      const std::shared_ptr<DeclarationNode> &decl);
  void setPortAttributesForEntity(
      const std::shared_ptr<DeclarationNode> &entityDecl,
      const std::string &portName);

  void resolveStreamRatesReverse(const std::shared_ptr<StreamNode> &stream,
                                 const ScopeStack &scope, const ASTNode &tree);
  void resolveStreamRates(const std::shared_ptr<StreamNode> &stream,
                          const ScopeStack &scope, const ASTNode &tree,
                          bool fallbackToDomainDefault = true);
  void resolveStreamTypesReverse(const std::shared_ptr<StreamNode> &stream,
                                 const ScopeStack &stack, const ASTNode &tree);
  void resolveStreamTypesForward(const std::shared_ptr<StreamNode> &stream,
                                 const ScopeStack &scope, const ASTNode &tree);
  void markUnresolvedOutputType(const ASTNode &source, const ASTNode &target,
                                const ScopeStack &scope, const ASTNode &tree);

  void setNodeType(const ASTNode &node, const ASTNode &type,
                   const ScopeStack &scope, const ASTNode &tree);

  void resolveDataTypeForDeclaration(const std::shared_ptr<DeclarationNode> &decl,
                                     std::set<const void *> &visited);
  std::shared_ptr<DeclarationNode>
  getDataTypeClass(const std::shared_ptr<DeclarationNode> &decl);
  std::shared_ptr<DeclarationNode>
  findTypeClass(const std::string &name, const std::string &framework);
  void setDeclarationType(const std::shared_ptr<DeclarationNode> &decl,
                          const ASTNode &type);
  void changeDeclarationType(const std::shared_ptr<DeclarationNode> &decl,
                             const std::string &entityType);

  ResolverContext &m_ctx;
};

} // namespace strd

#endif // RATEANDTYPERESOLVER_HPP

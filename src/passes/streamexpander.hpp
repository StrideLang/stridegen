/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#ifndef STREAMEXPANDER_HPP
#define STREAMEXPANDER_HPP

#include "resolvercontext.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/streamnode.h"
#include "stride/utils/astquery.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace strd {

class StreamExpander {
public:
  explicit StreamExpander(ResolverContext &ctx) : m_ctx(ctx) {}

  void expandValueRanges();
  void expandIndexedStreams();
  void expandParallel();
  void resolveStreamSymbols();

private:
  std::vector<ASTNode> declareUnknownEntity(
      const std::shared_ptr<EntityNode> &entityNode, int size,
      const ScopeStack &localScope, const ASTNode &tree);
  std::vector<ASTNode> declareUnknownExpressionSymbols(
      const std::shared_ptr<ExpressionNode> &expr, int size,
      const ScopeStack &scopeStack, const ASTNode &tree);
  std::vector<ASTNode> declareUnknownFunctionSymbols(
      const std::shared_ptr<FunctionNode> &func, const ScopeStack &scopeStack,
      const ASTNode &tree);
  std::vector<ASTNode> declareUnknownStreamSymbols(
      const std::shared_ptr<StreamNode> &stream,
      const ASTNode &previousStreamMember, const ScopeStack &localScope,
      const ASTNode &tree);
  std::vector<ASTNode> expandSingleIndexedStream(
      const std::shared_ptr<StreamNode> &stream, const ScopeStack &scope,
      const ASTNode &tree,
      std::vector<std::pair<std::string, int64_t>> &newBundles);

  void expandParallelStream(const std::shared_ptr<StreamNode> &stream,
                            const ScopeStack &scopeStack, const ASTNode &tree);
  void expandStreamToSizes(const std::shared_ptr<StreamNode> &stream,
                           std::vector<int> &neededCopies, int previousOutSize,
                           const ScopeStack &scopeStack);
  ASTNode expandFunctionFromProperties(const std::shared_ptr<FunctionNode> &func,
                                       const ScopeStack &scopeStack,
                                       const ASTNode &tree);

  ResolverContext &m_ctx;
  int64_t m_parallelGroupId = 0;
};

} // namespace strd

#endif // STREAMEXPANDER_HPP

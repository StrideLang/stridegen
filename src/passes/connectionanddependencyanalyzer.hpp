/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#ifndef CONNECTIONANDDEPENDENCYANALYZER_HPP
#define CONNECTIONANDDEPENDENCYANALYZER_HPP

#include "resolvercontext.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/streamnode.h"
#include "stride/utils/astquery.h"

#include <vector>

namespace strd {

class ConnectionAndDependencyAnalyzer {
public:
  explicit ConnectionAndDependencyAnalyzer(ResolverContext &ctx) : m_ctx(ctx) {}

  void storeDeclarations();
  void analyzeConnections();
  void analyzeParents();
  void analyzePersistence();
  void analyzeStreamDependencies();

private:
  void storeDeclarationsForNode(const ASTNode &node, ScopeStack scopeStack,
                                const ASTNode &tree);

  void analyzeChildConnections(const ASTNode &node, ScopeStack scopeStack);
  void checkStreamConnections(const std::shared_ptr<StreamNode> &stream,
                              ScopeStack scopeStack,
                              ASTNode previous = nullptr);
  void setInputEntityForFunction(const std::shared_ptr<FunctionNode> &func,
                                 const ASTNode &previous);
  void setOutputEntityForFunction(const std::shared_ptr<FunctionNode> &func,
                                  const ASTNode &next);
  void markPreviousReads(const ASTNode &node, const ASTNode &previous,
                         const ScopeStack &scopeStack);
  void markConnectionForNode(const ASTNode &node, ScopeStack scopeStack,
                             ASTNode previous = nullptr,
                             unsigned int listIndex = 0);

  void appendParent(const std::shared_ptr<DeclarationNode> &decl,
                    const std::shared_ptr<DeclarationNode> &parent);

  void markNodePersistence(const ASTNode &streamsList, ScopeStack &scope,
                           const ASTNode &tree);

  void analyzeContainerStreamDependencies(
      const std::vector<std::shared_ptr<StreamNode>> &streams,
      const ScopeStack &scope, const ASTNode &containerDecl = nullptr);

  ResolverContext &m_ctx;
};

} // namespace strd

#endif // CONNECTIONANDDEPENDENCYANALYZER_HPP

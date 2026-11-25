/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

    Based on BSD/MIT code by Andres Cabrera and Joseph Tilbian.

    Author: Andres Cabrera
*/

#ifndef CODEANALYSIS_HPP
#define CODEANALYSIS_HPP

#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "stride/utils/astquery.h"

namespace strd {

class CodeAnalysis {
public:
  enum { SIZE_UNKNOWN = -1, SIZE_MEMBER_ACCESS = -2 };

  // Global tree domain & framework analysis
  static std::vector<std::string> getUsedDomains(const ASTNode &tree);
  static std::vector<std::string> getUsedFrameworks(const ASTNode &tree);

  // Domain queries
  static ASTNode getNodeDomain(const ASTNode &node, const ScopeStack &scopeStack,
                               const ASTNode &tree);
  static std::string getNodeDomainName(const ASTNode &node,
                                       const ScopeStack &scopeStack,
                                       const ASTNode &tree);
  static ASTNode resolveDomain(const ASTNode &node, const ScopeStack &scopeStack,
                               const ASTNode &tree, bool downStream = true);

  // Rate queries
  static double getNodeRate(const ASTNode &node, ScopeStack scope = ScopeStack(),
                            const ASTNode &tree = nullptr);
  static double getModulePortRate(const std::shared_ptr<FunctionNode> &funcNode,
                                  const std::string &portType,
                                  const ScopeStack &scope, const ASTNode &tree);
  static ASTNode
  getModulePortRateExpr(const std::shared_ptr<FunctionNode> &funcNode,
                        const std::string &portType, const ScopeStack &scope,
                        const ASTNode &tree);
  static double resolveRate(const ASTNode &node, const ScopeStack &scopeStack,
                            const ASTNode &tree, bool downStream = true);
  static double resolveRateToFloat(const ASTNode &rateNode,
                                   const ScopeStack &scope, const ASTNode &tree,
                                   std::vector<LangError> *errors);

  // Stream connection & member access evaluation
  static std::shared_ptr<DeclarationNode>
  resolveConnectionEntity(const ASTNode &node, const ScopeStack &scopeStack,
                          const ASTNode &tree, bool downStream = true);
  static ASTNode
  getMatchedOuterInstance(const std::shared_ptr<FunctionNode> &functionNode,
                          const std::shared_ptr<DeclarationNode> &entityDecl,
                          const std::shared_ptr<DeclarationNode> &funcDecl,
                          const ScopeStack &scopeStack, const ASTNode &tree);

  static int
  evaluateSizeMemberAccess(const std::string &targetPortName,
                           ScopeStack scopeStack,
                           const std::shared_ptr<DeclarationNode> &decl,
                           const std::shared_ptr<FunctionNode> &func,
                           const ASTNode &tree);
  static double
  evaluateRateMemberAccess(const std::string &targetPortName,
                           ScopeStack scopeStack,
                           const std::shared_ptr<DeclarationNode> &decl,
                           const std::shared_ptr<FunctionNode> &func,
                           const ASTNode &tree);

  // Input and output bundle widths (-1 = unknown, -2 = member access)
  static int getNodeNumOutputs(const ASTNode &node, const ScopeStack &scope,
                               const ASTNode &tree,
                               std::vector<LangError> *errors = nullptr);
  static int getNodeNumInputs(const ASTNode &node, ScopeStack scope,
                              const ASTNode &tree,
                              std::vector<LangError> *errors = nullptr);
  static int
  getTypeNumOutputs(const std::shared_ptr<DeclarationNode> &entityDeclaration,
                    const ScopeStack &scope, const ASTNode &tree,
                    std::vector<LangError> *errors = nullptr);
  static int
  getTypeNumInputs(const std::shared_ptr<DeclarationNode> &entityDeclaration,
                   const ScopeStack &scope, const ASTNode &tree,
                   std::vector<LangError> *errors = nullptr);
  static int getFunctionDataSize(const std::shared_ptr<FunctionNode> &func,
                                 const ScopeStack &scope, const ASTNode &tree,
                                 std::vector<LangError> *errors = nullptr);
  static int getFunctionNumInstances(const std::shared_ptr<FunctionNode> &func,
                                     ScopeStack scope, const ASTNode &tree,
                                     std::vector<LangError> *errors = nullptr);

  // Data type resolution
  static std::vector<ASTNode> getInputDataTypes(const ASTNode &node,
                                                const ScopeStack &scope,
                                                const ASTNode &tree);
  static std::vector<ASTNode> getOutputDataTypes(const ASTNode &node,
                                                 const ScopeStack &scope,
                                                 const ASTNode &tree);
  static std::string
  getDataTypeForDeclaration(const std::shared_ptr<DeclarationNode> &decl,
                            const ScopeStack &scope, const ASTNode &tree);
  static std::string resolveEntityDataType(const ASTNode &node,
                                           const ScopeStack &scopeStack,
                                           const ASTNode &tree);
  static std::string resolveNodeOutDataType(const ASTNode &node,
                                            const ScopeStack &scopeStack,
                                            const ASTNode &tree);

  // Overload resolution
  static std::shared_ptr<DeclarationNode> matchDefinitionToTypes(
      const std::vector<std::shared_ptr<DeclarationNode>> &decls,
      const std::shared_ptr<FunctionNode> &func,
      const ScopeStack &scope = {}, const ASTNode &tree = nullptr);

  enum class NodeRole {
    Input,
    Output,
    Internal,
    Persistent,
    External,
    Unknown
  };

  static NodeRole
  determineNodeRole(const ASTNode &entity,
                    const std::shared_ptr<DeclarationNode> &funcDecl,
                    const ScopeStack &scope, const ASTNode &tree,
                    std::shared_ptr<DeclarationNode> &outEntityDecl);

  using DataInfo = std::pair<ASTNode, std::string>; // Instance, Type
  struct TypeTree {
    ASTNode instance;
    std::vector<TypeTree> nodes;
    // External refers to data declared above this scope.
    std::vector<DataInfo> input;
    std::vector<DataInfo> output;
    std::vector<DataInfo> external;
    std::vector<DataInfo> persistent; // persists across calls
    std::vector<DataInfo> internal;
    mutable std::unordered_set<std::string> namesSet;

    bool contains(const std::string &name) const;
    TypeTree *find(const ASTNode &node, TypeTree *tree = nullptr);
    const TypeTree *find(const ASTNode &node,
                         const TypeTree *tree = nullptr) const;
    TypeTree *getDomainRootTree(const std::string &domainName);
    TypeTree *getParentTreeForNode(const ASTNode &node,
                                   TypeTree *tree = nullptr);
  };

  static TypeTree getStateStructInformation(const ScopeStack &scope,
                                            const ASTNode &tree);
  static TypeTree getStateStructInformationForDeclaration(
      const std::shared_ptr<DeclarationNode> &decl, const ScopeStack &scope,
      const ASTNode &tree);

private:
  static std::string resolveNodeOutDataTypeInternal(const ASTNode &node,
                                                    const ScopeStack &scopeStack,
                                                    const ASTNode &tree);
  static std::string resolveListDataType(ListNode *listNode,
                                         const ScopeStack &scopeStack,
                                         const ASTNode &tree);
  static std::string resolveExpressionDataType(ExpressionNode *exprNode,
                                               const ScopeStack &scopeStack,
                                               const ASTNode &tree);
  static std::string resolveRangeDataType(RangeNode *rangeNode,
                                          const ScopeStack &scopeStack,
                                          const ASTNode &tree);
  static std::string resolveMemberAccessDataType(MemberAccessNode *memberAccess,
                                                 const ScopeStack &scopeStack,
                                                 const ASTNode &tree);

  static void processStreamNode(const ASTNode &streamNode,
                                const ScopeStack &scope, const ASTNode &tree,
                                TypeTree &typeTree);
  static void processStreamNodeForDeclaration(
      const ASTNode &node, const std::shared_ptr<DeclarationNode> &funcDecl,
      const ScopeStack &funcScope, const ASTNode &tree, TypeTree &typeTree);
};

} // namespace strd

#endif // CODEANALYSIS_HPP

/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

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

#include <map>
#include <unordered_set>
#include "codeentities.hpp"
#include "stridesystem.hpp"

namespace strd {
class CodeAnalysis {
public:
  enum {
    SIZE_UNKNOWN = -1,
    SIZE_MEMBER_ACCESS = -2
  };
  // Global tree analysis
  static std::vector<std::string> getUsedDomains(ASTNode tree);
  static std::vector<std::string> getUsedFrameworks(ASTNode tree);

  // Get instance. For entities the declaration is the instance, for modules it
  // is the function node in the stream Many properties need to be stored in the
  // instance.
  static ASTNode getInstance(ASTNode block, ScopeStack scopeStack,
                             ASTNode tree);

  // Retrieve stored declaration in "declaration" compiler property
  static std::shared_ptr<DeclarationNode> getDeclaration(ASTNode node);

  // Domain queries
  static std::shared_ptr<DeclarationNode>
  findDomainDeclaration(std::string domainName, std::string framework,
                        ASTNode tree);

  static std::shared_ptr<DeclarationNode>
  findDomainDeclaration(std::string domainId, ASTNode tree);

  static ASTNode getNodeDomain(ASTNode node, ScopeStack scopeStack,
                               ASTNode tree);

  static std::string getNodeDomainName(ASTNode node, ScopeStack scopeStack,
                                       ASTNode tree);

  static std::string getDomainIdentifier(ASTNode domain, ScopeStack scopeStack,
                                         ASTNode tree);

  static ASTNode resolveDomain(ASTNode node, ScopeStack scopeStack,
                               ASTNode tree, bool downStream = true);

  // Rate queries
  static double getNodeRate(ASTNode node, ScopeStack scope = ScopeStack(),
                            ASTNode tree = nullptr);

  static double resolveRate(ASTNode node, ScopeStack scopeStack, ASTNode tree,
                            bool downStream = true);

  static double resolveRateToFloat(ASTNode rateNode, ScopeStack scope,
                                   ASTNode tree,
                                   std::vector<LangError> *errors);
  static double
  getDomainDefaultRate(std::shared_ptr<DeclarationNode> domainDecl);

  // Framework queries
  static std::string getFrameworkForDomain(std::string domainName,
                                           ASTNode tree);
  // Stream analysis

  /// Number of parallel streams that a single stream can be broken up into
  static int numParallelStreams(StreamNode *stream, StrideSystem &platform,
                                const ScopeStack &scope, ASTNode tree,
                                std::vector<LangError> *errors = nullptr);

  static std::shared_ptr<DeclarationNode>
  resolveConnectionEntity(ASTNode node, ScopeStack scopeStack, ASTNode tree,
                          bool downStream = true);

  //
  static ASTNode
  getMatchedOuterInstance(std::shared_ptr<FunctionNode> functionNode,
                          std::shared_ptr<DeclarationNode> blockDecl,
                          std::shared_ptr<DeclarationNode> funcDecl,
                          ScopeStack scopeStack, ASTNode tree);

  static std::vector<ASTNode>
  getEntitiesInScope(ASTNode root, ScopeStack scopeStack, ASTNode tree);

  // Evaluate value of member access / port property (size and rate)
  static int evaluateSizeMemberAccess(std::string targetPortName,
                                      ScopeStack scopeStack,
                                      std::shared_ptr<DeclarationNode> decl,
                                      std::shared_ptr<FunctionNode> func,
                                      ASTNode tree);

  static double evaluateRateMemberAccess(std::string targetPortName,
                                         ScopeStack scopeStack,
                                         std::shared_ptr<DeclarationNode> decl,
                                         std::shared_ptr<FunctionNode> func,
                                         ASTNode tree);

  static std::vector<std::shared_ptr<MemberAccessNode>>
  getUsedMemberAccesses(std::shared_ptr<DeclarationNode> funcDecl);

  static std::vector<std::shared_ptr<MemberAccessNode>>
  getUsedMemberAccessesInNode(ASTNode node);

  static std::vector<ASTNode> getUsedEntitiesInNode(ASTNode node);

  // //

  // Input and output size
  static int getNodeNumOutputs(ASTNode node, const ScopeStack &scope,
                               ASTNode tree,
                               std::vector<LangError> *errors = nullptr);
  static int getNodeNumInputs(ASTNode node, ScopeStack scope, ASTNode tree,
                              std::vector<LangError> *errors = nullptr);

  // A value of -1 means undefined. -2 means set from member access / port
  // property.
  static int
  getTypeNumOutputs(std::shared_ptr<DeclarationNode> blockDeclaration,
                    const ScopeStack &scope, ASTNode tree,
                    std::vector<LangError> *errors = nullptr);

  static int getTypeNumInputs(std::shared_ptr<DeclarationNode> blockDeclaration,
                              const ScopeStack &scope, ASTNode tree,
                              std::vector<LangError> *errors = nullptr);

  static int getFunctionDataSize(std::shared_ptr<FunctionNode> func,
                                 ScopeStack scope, ASTNode tree,
                                 std::vector<LangError> *errors = nullptr);

  static int getFunctionNumInstances(std::shared_ptr<FunctionNode> func,
                                     ScopeStack scope, ASTNode tree,
                                     std::vector<LangError> *errors = nullptr);

  static std::shared_ptr<DeclarationNode>
  findDataTypeDeclaration(std::string dataTypeName, ASTNode tree);

  static std::vector<ASTNode>
  getInputDataTypes(ASTNode node, const ScopeStack &scope, ASTNode tree);
  static std::vector<ASTNode>
  getOutputDataTypes(ASTNode node, const ScopeStack &scope, ASTNode tree);
  // True if the type of the module's output block is taken from its own
  // output port (e.g. type: OutputPort.type). Its output type can then only
  // be resolved from a declared block connected to the output.
  static bool
  isOutputTypeFromOutputPort(std::shared_ptr<DeclarationNode> funcDecl);
  static ASTNode
  getDataTypeForSignalDeclaration(std::shared_ptr<DeclarationNode> decl);
  static std::string
  getDataTypeForDeclaration(std::shared_ptr<DeclarationNode> decl,
                            const ScopeStack &scope, ASTNode tree);

  static std::string resolveEntityDataType(ASTNode name, ScopeStack scopeStack,
                                           ASTNode tree);
  static std::string resolveNodeOutDataType(ASTNode node, const ScopeStack &scopeStack,
                                            ASTNode tree);
  static std::string resolveNodeOutDataTypeInternal(ASTNode node,
                                                    const ScopeStack &scopeStack,
                                                    ASTNode tree);
  static std::string resolveListDataType(ListNode *listnode,
                                         ScopeStack scopeStack, ASTNode tree);
  static std::string resolveExpressionDataType(ExpressionNode *exprnode,
                                               ScopeStack scopeStack,
                                               ASTNode tree);
  static std::string resolveRangeDataType(RangeNode *rangenode,
                                          const ScopeStack &scopeStack, ASTNode tree);
  static std::string resolveMemberAccessDataType(MemberAccessNode *memberAccess,
                                                 ScopeStack scopeStack,
                                                 ASTNode tree);

  static std::vector<ASTNode> getDomainIOEntityDeclarations(ASTNode tree);

  static std::shared_ptr<DeclarationNode>
  matchDefinitionToTypes(std::vector<std::shared_ptr<DeclarationNode>> decls,
                         std::shared_ptr<FunctionNode> func,
                         const ScopeStack &scope = {}, ASTNode tree = nullptr);

  enum class NodeRole {
    Input,
    Output,
    Internal,
    Persistent,
    External,
    Unknown
  };

  static NodeRole
  determineNodeRole(ASTNode entity, std::shared_ptr<DeclarationNode> funcDecl,
                    const ScopeStack &scope, ASTNode tree,
                    std::shared_ptr<DeclarationNode> &outEntityDecl);

  static std::vector<ASTNode>
  getUsedEntitiesInStreams(std::shared_ptr<DeclarationNode> funcDecl);

  static bool isStorageEntity(std::shared_ptr<DeclarationNode> decl);
  static std::string getStorageElementDataType(std::shared_ptr<DeclarationNode> decl);
  static SignalAccess getSignalAccess(ASTNode instanceNode, ASTNode tree, StrideSystem *system, const std::map<std::string, std::string> &domainMap = {});

  using DataInfo = std::pair<ASTNode, std::string>; // Instance, Type
  struct TypeTree {
    ASTNode instance;
    std::vector<TypeTree> nodes;
    // External refers to data declared above this scope.
    std::vector<DataInfo> input;
    std::vector<DataInfo> output;
    std::vector<DataInfo> external;
    std::vector<DataInfo> persistent; // store state that persistes across calls
    std::vector<DataInfo> internal;
    mutable std::unordered_set<std::string> namesSet;

    bool contains(std::string name);
    CodeAnalysis::TypeTree *find(ASTNode node,
                                 CodeAnalysis::TypeTree *tree = nullptr);
    TypeTree *getDomainRootTree(std::string domainName);
    TypeTree *getParentTreeForNode(ASTNode node,
                                   CodeAnalysis::TypeTree *tree = nullptr);
  };

  static TypeTree getStateStructInformation(const ScopeStack &scope,
                                            ASTNode tree);

  static TypeTree
  getStateStructInformationForDeclaration(std::shared_ptr<DeclarationNode> decl,
                                          const ScopeStack &scope,
                                          ASTNode tree);

private:
  static TypeTree
  getTypeTreeForFunctionInstance(std::shared_ptr<FunctionNode> func,
                                 const ScopeStack &scope, ASTNode tree,
                                 int &instanceCounter);
  static void processStreamNode(ASTNode streamNode, const ScopeStack &scope,
                                ASTNode tree, TypeTree &typeTree);
  static void processStreamNodeForDeclaration(
      ASTNode node, std::shared_ptr<DeclarationNode> funcDecl,
      const ScopeStack &funcScope, ASTNode tree, TypeTree &typeTree);
};
} // namespace strd

#endif // CODEANALYSIS_HPP

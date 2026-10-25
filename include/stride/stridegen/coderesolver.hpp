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

#ifndef CODERESOLVER_H
#define CODERESOLVER_H

#include "stride/parser/arraynode.h"
#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/rangenode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"

#include <set>

#include "stridesystem.hpp"

#include "systemconfiguration.hpp"

namespace strd {
// Resolves all code and creates a StrideSystem
class CodeResolver {
public:
  CodeResolver(ASTNode tree, std::string striderootDir,
               SystemConfiguration systemConfig = SystemConfiguration());
  ~CodeResolver();

  void process();

  std::shared_ptr<StrideSystem> getSystem() { return m_system; }

private:
  // Main processing functions
  void processSystem();
  void desugar();
  void desugarSingleEntryStreams(ASTNode node, std::set<const void *> &visited);
  void enableTesting();
  void declareModuleInternalEntities();
  void resolveStreamSymbols();
  void resolveDeclarationSymbols();
  void processDeclarations();
  void expandIndexedStreams();
  void expandParallel();
  void resolveConstants();
  void resolveMemberAccesses();
  void processDomains();
  void setPortEntityAttributes(const ScopeStack &scope, ASTNode tree);
  void resolveAttributes(
      const ScopeStack &scope,
      ASTNode tree); // Resolve CodeGenetaror port attributes: rate, type
  void analyzeConnections();
  void resolveTypeCasting();
  void resolveDeclarationDataTypes(); // Default type from type's typeClass
  void storeDeclarations();
  void analyzeParents();
  void analyzePersistence();

  // Sub functions
  static std::shared_ptr<DeclarationNode>
  createSignalDeclaration(std::string name, int size, ScopeStack scope,
                          ASTNode tree);
  void resolveStreamRatesReverse(std::shared_ptr<StreamNode> stream,
                                 const ScopeStack &scope, ASTNode tree);
  void resolveStreamRates(std::shared_ptr<StreamNode> stream,
                          const ScopeStack &scope, ASTNode tree);
  void resolveStreamTypesReverse(std::shared_ptr<StreamNode> stream,
                                 ScopeStack stack, ASTNode tree);
  void markUnresolvedOutputType(ASTNode source, ASTNode target,
                                ScopeStack scope, ASTNode tree);
  void resolveStreamTypesForward(std::shared_ptr<StreamNode> stream,
                                 ScopeStack scope, ASTNode tree);

  void expandParallelStream(std::shared_ptr<StreamNode> stream,
                            ScopeStack scopeStack, ASTNode tree);

  void expandStreamToSizes(std::shared_ptr<StreamNode> stream,
                           std::vector<int> &size, int previousOutSize,
                           ScopeStack scopeStack);
  ASTNode expandFunctionFromProperties(std::shared_ptr<FunctionNode> func,
                                       ScopeStack scope, ASTNode tree);

  void markNodePersistence(ASTNode streamsList, ASTNode blocksList,
                           ScopeStack &scope, ASTNode tree);
  void analyzeChildConnections(ASTNode node,
                               ScopeStack scopeStack = ScopeStack());

  void resolveDomainsForStream(std::shared_ptr<StreamNode> stream,
                               ScopeStack scopeStack,
                               ASTNode contextDomainNode);

  void setDomainForNode(ASTNode node, ASTNode domain, ScopeStack scopeStack,
                        ASTNode tree, bool force = false);
  ASTNode processDomainsForNode(ASTNode node, ScopeStack scopeStack,
                                std::vector<ASTNode> &domainStack);
  void setDomainForStack(std::vector<ASTNode> domainStack,
                         ASTNode resolvingInstance, ASTNode domainName,
                         ScopeStack scopeStack);

  std::vector<ASTNode> declareUnknownName(std::shared_ptr<EntityNode> entityNode,
                                          int size, ScopeStack localScope,
                                          ASTNode tree);
  std::vector<ASTNode> declareUnknownArray(std::shared_ptr<ArrayNode> name,
                                           int size, ScopeStack localScope,
                                           ASTNode tree);
  std::shared_ptr<DeclarationNode> createConstantDeclaration(std::string name,
                                                             ASTNode value);
  void declareIfMissing(std::string name, ASTNode declList, ASTNode value);

  std::vector<ASTNode>
  declareUnknownExpressionSymbols(std::shared_ptr<ExpressionNode> expr,
                                  int size, ScopeStack scopeStack,
                                  ASTNode tree);
  std::vector<ASTNode>
  declareUnknownFunctionSymbols(std::shared_ptr<FunctionNode> func,
                                ScopeStack scopeStack, ASTNode tree);
  std::shared_ptr<ListNode> expandNameToList(EntityNode *name, int size);
  void expandNamesToArrays(std::shared_ptr<StreamNode> stream, ASTNode tree);
  std::vector<ASTNode>
  declareUnknownStreamSymbols(std::shared_ptr<StreamNode> stream,
                              ASTNode previousStreamMember,
                              ScopeStack localScope, ASTNode tree);
  std::vector<ASTNode>
  getModuleStreams(std::shared_ptr<DeclarationNode> module);

  void declareInternalEntitiesForNode(ASTNode node, ScopeStack scope);

  void propagateDomainsForNode(ASTNode node, ScopeStack scopeStack);
  void resolveDomainForStreamNode(ASTNode node, ScopeStack scope);

  ASTNode resolveMemberAccess(std::shared_ptr<MemberAccessNode> memberAccess,
                              ScopeStack scopeStack);

  void
  setPortEntityAttributesForDeclaration(std::shared_ptr<DeclarationNode> decl,
                                        ScopeStack stack, ASTNode tree);

  void setNodeRate(ASTNode node, double rate, ScopeStack scope, ASTNode tree);
  void setNodeType(ASTNode node, ASTNode type, ScopeStack scope, ASTNode tree);

  void setInputEntityForFunction(std::shared_ptr<FunctionNode> func,
                                 ScopeStack scopeStack, ASTNode previous);
  void setOutputEntityForFunction(std::shared_ptr<FunctionNode> func,
                                  ScopeStack scopeStack, ASTNode previous);
  void checkStreamConnections(std::shared_ptr<StreamNode> stream,
                              ScopeStack scopeStack,
                              ASTNode previous = nullptr);

  void markPreviousReads(ASTNode node, ASTNode previous, ScopeStack scopeStack);
  void markConnectionForNode(ASTNode node, ScopeStack scopeStack,
                             ASTNode previous = nullptr,
                             unsigned int listIndex = 0);

  void storeDeclarationsForNode(ASTNode node, ScopeStack scopeStack,
                                ASTNode tree);

  void resolveTypeCastForNode(ASTNode node, ScopeStack scopeStack,
                              ASTNode tree);
  void resolveTypeCastForStream(std::shared_ptr<StreamNode> stream,
                                ScopeStack scopeStack, ASTNode tree);
  void resolveDataTypeForDeclaration(std::shared_ptr<DeclarationNode> decl,
                                     std::set<const void *> &visited);
  std::shared_ptr<DeclarationNode>
  getDataTypeClass(std::shared_ptr<DeclarationNode> decl);
  std::shared_ptr<DeclarationNode> findTypeClass(std::string name,
                                                 std::string framework);
  void setDeclarationType(std::shared_ptr<DeclarationNode> decl, ASTNode type);
  void setPortAttributesForBlock(std::shared_ptr<DeclarationNode> blockDecl,
                                 std::string portName);
  void changeDeclarationType(std::shared_ptr<DeclarationNode> decl,
                             std::string entityType);
  void resolveTypeCastForDeclaration(std::shared_ptr<DeclarationNode> decl,
                                     ScopeStack scopeStack, ASTNode tree);
  void resolveNodeSymbols(ASTNode decl, ScopeStack scopeStack, ASTNode tree);

  void appendParent(std::shared_ptr<DeclarationNode> decl,
                    std::shared_ptr<DeclarationNode> parent);

  std::shared_ptr<StreamNode> makeStreamFromStack(std::vector<ASTNode> &stack);
  std::vector<ASTNode> sliceStreamByDomain(std::shared_ptr<StreamNode> stream,
                                           ScopeStack scopeStack);
  void sliceDomainsInNode(std::shared_ptr<DeclarationNode> stream,
                          ScopeStack scopeStack);

  ASTNode getModuleContextDomain(std::shared_ptr<DeclarationNode> moduleDecl);

  std::vector<ASTNode> mContextDomainStack;

  std::shared_ptr<StrideSystem> m_system;
  SystemConfiguration m_systemConfig;

  ASTNode m_tree;

  // Resolver configuration
  bool m_declareUnknownSymbols = false;
};
} // namespace strd

#endif // CODERESOLVER_H

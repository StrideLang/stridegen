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

#include "stride/stridegen/codeanalysis.hpp"

#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

#include <iostream>

using namespace strd;

namespace {

void appendByRole(CodeAnalysis::TypeTree &tree, CodeAnalysis::NodeRole role,
                  const std::shared_ptr<DeclarationNode> &decl,
                  const std::string &type) {
  switch (role) {
  case CodeAnalysis::NodeRole::Output:
    tree.output.push_back({decl, type});
    break;
  case CodeAnalysis::NodeRole::Input:
    tree.input.push_back({decl, type});
    break;
  case CodeAnalysis::NodeRole::External:
    tree.external.push_back({decl, type});
    break;
  case CodeAnalysis::NodeRole::Persistent:
    tree.persistent.push_back({decl, type});
    break;
  case CodeAnalysis::NodeRole::Internal:
    tree.internal.push_back({decl, type});
    break;
  case CodeAnalysis::NodeRole::Unknown:
    break;
  }
}

void insertDomainBoundaryEntities(const ASTNode &listNode,
                                  const ASTNode &tree,
                                  CodeAnalysis::TypeTree &domainTree) {
  if (!listNode) {
    return;
  }
  for (const auto &domainNode : listNode->getChildren()) {
    if (!domainNode || (domainNode->getNodeType() != AST::Declaration &&
                        domainNode->getNodeType() != AST::ArrayDeclaration)) {
      continue;
    }
    auto entityDecl = std::static_pointer_cast<DeclarationNode>(domainNode);
    if (!domainTree.contains(entityDecl->getName())) {
      std::string type =
          CodeAnalysis::getDataTypeForDeclaration(entityDecl, {}, tree);
      domainTree.external.push_back({entityDecl, type});
    }
  }
}

} // namespace

CodeAnalysis::NodeRole CodeAnalysis::determineNodeRole(
    const ASTNode &entity, const std::shared_ptr<DeclarationNode> &funcDecl,
    const ScopeStack &scope, const ASTNode &tree,
    std::shared_ptr<DeclarationNode> &outEntityDecl) {
  if (!entity || !funcDecl) {
    return NodeRole::Unknown;
  }
  std::string entityName = ASTQuery::getNodeName(entity);
  auto funcType = ASTQuery::findTypeDeclaration(funcDecl, scope, tree);

  if (!ASTQuery::isCodeGenerator(funcType, scope, tree) ||
      !ASTQuery::isCallable(funcType, scope, tree)) {
    std::cerr << "Unexpected function type: " << funcDecl->toText()
              << std::endl;
    return NodeRole::Unknown;
  }

  auto entitiesNode = funcDecl->getPropertyValue("entities");
  std::vector<ASTNode> entitiesList =
      (entitiesNode && entitiesNode->getNodeType() == AST::List)
          ? entitiesNode->getChildren()
          : std::vector<ASTNode>();

  auto portsNode = funcDecl->getPropertyValue("ports");
  if (portsNode && portsNode->getNodeType() == AST::List) {
    for (const auto &portNode : portsNode->getChildren()) {
      if (!portNode || portNode->getNodeType() != AST::Declaration) {
        continue;
      }
      auto portDecl = std::static_pointer_cast<DeclarationNode>(portNode);
      auto portEntity = portDecl->getPropertyValue("entity");
      if (portEntity && ASTQuery::getNodeName(portEntity) == entityName) {
        outEntityDecl = ASTQuery::findDeclarationForNode(
            portEntity, {{nullptr, entitiesList}}, nullptr);
        const std::string &entityType = portDecl->getEntityType();
        if (entityType == "mainOutputPort" ||
            entityType == "propertyOutputPort" ||
            entityType == "secondaryOutputPort") {
          return NodeRole::Output;
        }
        if (entityType == "mainInputPort" ||
            entityType == "propertyInputPort" ||
            entityType == "secondaryInputPort") {
          return NodeRole::Input;
        }
      }
    }
  }

  // Entities in functions/modules are local unless marked as persistent
  if (!scope.empty()) {
    ScopeStack localScope = {scope.back()};
    auto entityDecl = ASTQuery::findDeclarationForNode(entity, localScope, nullptr);
    if (entityDecl) {
      outEntityDecl = entityDecl;
      if (funcDecl->getEntityType() == "loop" ||
          funcDecl->getEntityType() == "reaction" ||
          entityDecl->getEntityType() == "iterator" ||
          entityDecl->getEntityType() == "sequence") {
        return NodeRole::Internal;
      }
      auto persistentProp = entityDecl->getCompilerProperty("persistent");
      auto resetProp = entityDecl->getPropertyValue("reset");
      if (persistentProp ||
          (resetProp && resetProp->getNodeType() != AST::None) ||
          ASTQuery::isStorageEntity(entityDecl)) {
        return NodeRole::Persistent;
      }
      return NodeRole::Internal;
    }
  }

  if (ASTQuery::isDomainMember(funcType, scope, tree)) {
    ScopeStack outerScope = scope;
    if (!outerScope.empty()) {
      outerScope.pop_back();
    }
    auto entityDecl = ASTQuery::findDeclarationForNode(entity, outerScope, tree);
    if (entityDecl) {
      outEntityDecl = entityDecl;
      return NodeRole::External;
    }
  }

  return NodeRole::Unknown;
}

void CodeAnalysis::processStreamNode(const ASTNode &streamNode,
                                     const ScopeStack &scope,
                                     const ASTNode &tree, TypeTree &typeTree) {
  if (!streamNode) {
    return;
  }
  if (streamNode->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(streamNode);
    CodeAnalysis::TypeTree functionTree;
    functionTree.instance = func;
    auto funcDecl = ASTQuery::findDeclarationForNode(func, scope, tree);
    auto funcDeclType = ASTQuery::findTypeDeclaration(funcDecl, scope, tree);
    if (funcDecl && funcDeclType &&
        funcDecl->getEntityType() != "platformModule") {
      auto entities = funcDecl->getPropertyValue("entities");

      ScopeStack funcScope;
      if (ASTQuery::isDomainMember(funcDeclType, scope, tree) &&
          !scope.empty()) {
        // Domain members can access parent and root scopes
        funcScope.push_back(scope.back());
      }
      funcScope.push_back(
          {func, entities ? entities->getChildren() : std::vector<ASTNode>{}});

      auto streamEntities =
          ASTQuery::getContainedStreamEntities(funcDecl, funcScope, tree);
      for (const auto &ent : streamEntities) {
        for (const auto &stream : ent.streams) {
          StreamNodeIterator it(stream);
          while (it.hasNext()) {
            processStreamNode(it.next(), funcScope, tree, functionTree);
          }
        }
      }
      if (entities && entities->getNodeType() == AST::List) {
        for (const auto &entNode : entities->getChildren()) {
          if (entNode && entNode->getNodeType() == AST::Declaration) {
            auto bDecl = std::static_pointer_cast<DeclarationNode>(entNode);
            if (bDecl->getEntityType() == "iterator" ||
                bDecl->getEntityType() == "sequence") {
              processStreamNode(
                  std::make_shared<EntityNode>(bDecl->getName(),
                                               bDecl->getFilename().c_str(),
                                               bDecl->getLine()),
                  funcScope, tree, functionTree);
              for (const auto &prop : bDecl->getProperties()) {
                if (prop && prop->getValue()) {
                  processStreamNode(prop->getValue(), funcScope, tree,
                                    functionTree);
                }
              }
            }
          }
        }
      }
      if (ASTQuery::isDomainMember(funcDeclType, scope, tree) &&
          typeTree.instance) {
        auto parentDecl =
            ASTQuery::findDeclarationForNode(typeTree.instance, scope, tree);
        auto parentType =
            parentDecl ? ASTQuery::findTypeDeclaration(parentDecl, scope, tree)
                       : nullptr;
        const bool parentIsDomainMember =
            parentType && ASTQuery::isDomainMember(parentType, scope, tree);
        if (parentDecl) {
          auto parentEntities = ASTQuery::getModuleEntities(parentDecl);
          for (const auto &external : functionTree.external) {
            if (typeTree.contains(ASTQuery::getNodeName(external.first)) ||
                external.first->getNodeType() != AST::Declaration) {
              continue;
            }
            auto extDecl =
                std::static_pointer_cast<DeclarationNode>(external.first);
            if (ASTQuery::isInputPort(extDecl, parentDecl, scope, tree)) {
              typeTree.input.push_back(external);
            } else if (ASTQuery::isOutputPort(extDecl, parentDecl, scope,
                                              tree)) {
              typeTree.output.push_back(external);
            } else {
              bool inParentEntities = false;
              for (const auto &pEnt : parentEntities) {
                if (ASTQuery::getNodeName(pEnt) == extDecl->getName()) {
                  typeTree.internal.push_back(external);
                  inParentEntities = true;
                  break;
                }
              }
              if (!inParentEntities && parentIsDomainMember) {
                typeTree.external.push_back(external);
              }
            }
          }
        }
      }
    }
    std::string domainName = CodeAnalysis::getNodeDomainName(func, scope, tree);
    auto *domainTree = typeTree.getDomainRootTree(domainName);
    if (domainTree) {
      domainTree->nodes.push_back(functionTree);
    } else {
      typeTree.nodes.push_back(functionTree);
    }
  } else if (streamNode->getNodeType() == AST::Entity ||
             streamNode->getNodeType() == AST::Array) {
    const std::string entityName = ASTQuery::getNodeName(streamNode);
    if (!scope.empty() && scope.back().first &&
        scope.back().first->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(scope.back().first);
      auto funcDecl = ASTQuery::findDeclarationForNode(func, scope, tree);
      auto funcType = ASTQuery::findTypeDeclaration(funcDecl, scope, tree);
      if (ASTQuery::isCodeGenerator(funcType, scope, tree) &&
          ASTQuery::isCallable(funcType, scope, tree)) {
        std::shared_ptr<DeclarationNode> outEntityDecl;
        NodeRole role = determineNodeRole(streamNode, funcDecl, scope, tree,
                                          outEntityDecl);
        if (role != NodeRole::Unknown) {
          if (!typeTree.contains(entityName)) {
            std::string type = resolveEntityDataType(streamNode, scope, tree);
            appendByRole(typeTree, role, outEntityDecl, type);
          }
          return;
        }
      } else if (funcDecl) {
        std::cerr << "Unexpected function type: " << funcDecl->toText()
                  << std::endl;
      }
    }

    auto entityDecl = ASTQuery::findDeclarationForNode(streamNode, scope, tree);
    if (entityDecl) {
      std::string domainName =
          CodeAnalysis::getNodeDomainName(entityDecl, scope, tree);
      auto *domainTree = typeTree.getDomainRootTree(domainName);
      if (domainTree) {
        if (!domainTree->contains(entityName)) {
          std::string type = resolveEntityDataType(streamNode, {}, tree);
          domainTree->internal.push_back({entityDecl, type});
        }
      } else {
        std::cerr << __FILE__ << ":" << __LINE__ << " ERROR: domain not found"
                  << std::endl;
      }
    }
    if (streamNode->getNodeType() == AST::Array) {
      if (auto indices = std::static_pointer_cast<ArrayNode>(streamNode)->index()) {
        for (const auto &index : indices->getChildren()) {
          processStreamNode(index, scope, tree, typeTree);
        }
      }
    }
  } else if (streamNode->getNodeType() == AST::List) {
    for (const auto &listNode : streamNode->getChildren()) {
      processStreamNode(listNode, scope, tree, typeTree);
    }
  } else if (streamNode->getNodeType() == AST::Expression) {
    auto exprNode = std::static_pointer_cast<ExpressionNode>(streamNode);
    if (exprNode->isUnary()) {
      processStreamNode(exprNode->getValue(), scope, tree, typeTree);
    } else {
      processStreamNode(exprNode->getLeft(), scope, tree, typeTree);
      processStreamNode(exprNode->getRight(), scope, tree, typeTree);
    }
  } else if (streamNode->getNodeType() == AST::MemberAccess) {
    if (scope.empty()) {
      auto member = std::static_pointer_cast<MemberAccessNode>(streamNode);
      if (!ASTQuery::findDeclarationByName(member->getEntity(), scope, tree,
                                           member->getNamespaceList())) {
        std::cerr << __FILE__ << ":" << __LINE__
                  << " Error: member access in root scope: "
                  << streamNode->toText() << std::endl;
      }
      return;
    }
    typeTree.external.push_back({streamNode, DataTypes::Int});
  }
}

CodeAnalysis::TypeTree
CodeAnalysis::getStateStructInformation(const ScopeStack &scope,
                                        const ASTNode &tree) {
  CodeAnalysis::TypeTree typeTree;
  if (!tree) {
    return typeTree;
  }

  for (const auto &node : tree->getChildren()) {
    if (!node || (node->getNodeType() != AST::Declaration &&
                  node->getNodeType() != AST::ArrayDeclaration)) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() != "_domainDefinition" &&
        decl->getEntityType() != "gameDefinition") {
      continue;
    }
    typeTree.nodes.emplace_back(TypeTree());
    auto &domainTree = typeTree.nodes.back();
    domainTree.instance = decl;
    insertDomainBoundaryEntities(decl->getPropertyValue("outputs"), tree,
                                 domainTree);
    insertDomainBoundaryEntities(decl->getPropertyValue("inputs"), tree,
                                 domainTree);
    for (const char *lcProp : {"onLoad", "onInit", "onReset"}) {
      if (auto lcList = decl->getPropertyValue(lcProp)) {
        if (lcList->getNodeType() == AST::List) {
          for (const auto &lcNode : lcList->getChildren()) {
            if (lcNode && lcNode->getNodeType() == AST::Stream) {
              StreamNodeIterator it(
                  std::static_pointer_cast<StreamNode>(lcNode));
              while (it.hasNext()) {
                processStreamNode(it.next(), scope, tree, typeTree);
              }
            }
          }
        }
      }
    }
  }

  for (const auto &node : tree->getChildren()) {
    if (node && node->getNodeType() == AST::Stream) {
      StreamNodeIterator it(std::static_pointer_cast<StreamNode>(node));
      while (it.hasNext()) {
        processStreamNode(it.next(), scope, tree, typeTree);
      }
    }
  }
  return typeTree;
}

void CodeAnalysis::processStreamNodeForDeclaration(
    const ASTNode &node, const std::shared_ptr<DeclarationNode> &funcDecl,
    const ScopeStack &funcScope, const ASTNode &tree, TypeTree &typeTree) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::Function) {
    auto nestedFunc = std::static_pointer_cast<FunctionNode>(node);
    auto decls = ASTQuery::findAllDeclarationsForNode(nestedFunc, funcScope, tree);
    auto nestedFuncDecl =
        CodeAnalysis::matchDefinitionToTypes(decls, nestedFunc, funcScope, tree);
    if (!nestedFuncDecl) {
      nestedFuncDecl =
          ASTQuery::findDeclarationForNode(nestedFunc, funcScope, tree);
    }
    CodeAnalysis::TypeTree nestedTree;
    if (nestedFuncDecl && nestedFuncDecl->getEntityType() != "platformModule") {
      nestedTree = getStateStructInformationForDeclaration(nestedFuncDecl,
                                                           funcScope, tree);
    }
    nestedTree.instance = nestedFunc;
    typeTree.nodes.push_back(nestedTree);
  } else if (node->getNodeType() == AST::Entity ||
             node->getNodeType() == AST::Array) {
    std::shared_ptr<DeclarationNode> outEntityDecl;
    NodeRole role =
        determineNodeRole(node, funcDecl, funcScope, tree, outEntityDecl);
    if (role == NodeRole::External && outEntityDecl) {
      std::string entityName = ASTQuery::getNodeName(outEntityDecl);
      if (!typeTree.contains(entityName)) {
        std::string type =
            getDataTypeForDeclaration(outEntityDecl, funcScope, tree);
        typeTree.external.push_back({outEntityDecl, type});
      }
    }
    if (node->getNodeType() == AST::Array) {
      if (auto indices = std::static_pointer_cast<ArrayNode>(node)->index()) {
        for (const auto &index : indices->getChildren()) {
          processStreamNodeForDeclaration(index, funcDecl, funcScope, tree,
                                          typeTree);
        }
      }
    }
  } else if (node->getNodeType() == AST::Stream) {
    StreamNodeIterator it(std::static_pointer_cast<StreamNode>(node));
    while (it.hasNext()) {
      processStreamNodeForDeclaration(it.next(), funcDecl, funcScope, tree,
                                      typeTree);
    }
  } else if (node->getNodeType() == AST::Expression) {
    auto exprNode = std::static_pointer_cast<ExpressionNode>(node);
    if (exprNode->isUnary()) {
      processStreamNodeForDeclaration(exprNode->getValue(), funcDecl, funcScope,
                                      tree, typeTree);
    } else {
      processStreamNodeForDeclaration(exprNode->getLeft(), funcDecl, funcScope,
                                      tree, typeTree);
      processStreamNodeForDeclaration(exprNode->getRight(), funcDecl, funcScope,
                                      tree, typeTree);
    }
  } else if (node->getNodeType() == AST::List) {
    for (const auto &child : node->getChildren()) {
      processStreamNodeForDeclaration(child, funcDecl, funcScope, tree,
                                      typeTree);
    }
  }
}

CodeAnalysis::TypeTree CodeAnalysis::getStateStructInformationForDeclaration(
    const std::shared_ptr<DeclarationNode> &funcDecl, const ScopeStack &scope,
    const ASTNode &tree) {
  CodeAnalysis::TypeTree typeTree;
  typeTree.instance = funcDecl;
  if (!funcDecl) {
    return typeTree;
  }

  auto entities = funcDecl->getPropertyValue("entities");
  ScopeStack funcScope = scope;
  funcScope.push_back(
      {funcDecl, entities ? entities->getChildren() : std::vector<ASTNode>()});

  if (entities && entities->getNodeType() == AST::List) {
    for (const auto &entityNode : entities->getChildren()) {
      if (!entityNode || (entityNode->getNodeType() != AST::Declaration &&
                          entityNode->getNodeType() != AST::ArrayDeclaration)) {
        continue;
      }
      auto entityDecl = std::static_pointer_cast<DeclarationNode>(entityNode);
      std::shared_ptr<DeclarationNode> outEntityDecl;
      NodeRole role = determineNodeRole(entityNode, funcDecl, funcScope, tree,
                                        outEntityDecl);
      if (role != NodeRole::Unknown) {
        std::string type =
            getDataTypeForDeclaration(entityDecl, funcScope, tree);
        appendByRole(typeTree, role, entityDecl, type);
      }
      if (entityDecl->getEntityType() == "iterator" ||
          entityDecl->getEntityType() == "sequence") {
        for (const auto &prop : entityDecl->getProperties()) {
          if (prop && prop->getValue()) {
            processStreamNodeForDeclaration(prop->getValue(), funcDecl,
                                            funcScope, tree, typeTree);
          }
        }
      }
    }
  }

  auto streamEntities =
      ASTQuery::getContainedStreamEntities(funcDecl, funcScope, tree);
  for (const auto &ent : streamEntities) {
    for (const auto &stream : ent.streams) {
      processStreamNodeForDeclaration(stream, funcDecl, funcScope, tree,
                                      typeTree);
    }
  }
  return typeTree;
}

bool CodeAnalysis::TypeTree::contains(const std::string &name) const {
  if (namesSet.find(name) != namesSet.end()) {
    return true;
  }
  auto checkList = [&](const std::vector<DataInfo> &list) {
    for (const auto &elem : list) {
      std::string elemName = ASTQuery::getNodeName(elem.first);
      namesSet.insert(elemName);
      if (elemName == name) {
        return true;
      }
    }
    return false;
  };
  return checkList(output) || checkList(input) || checkList(external) ||
         checkList(internal) || checkList(persistent);
}

CodeAnalysis::TypeTree *CodeAnalysis::TypeTree::find(const ASTNode &node,
                                                     TypeTree *tree) {
  if (!node) {
    return nullptr;
  }
  if (!tree) {
    tree = this;
  }
  if (tree->instance == node) {
    return tree;
  }
  for (auto &innerTree : tree->nodes) {
    if (auto *foundTree = find(node, &innerTree)) {
      return foundTree;
    }
  }
  return nullptr;
}

const CodeAnalysis::TypeTree *
CodeAnalysis::TypeTree::find(const ASTNode &node, const TypeTree *tree) const {
  if (!node) {
    return nullptr;
  }
  if (!tree) {
    tree = this;
  }
  if (tree->instance == node) {
    return tree;
  }
  for (const auto &innerTree : tree->nodes) {
    if (const auto *foundTree = find(node, &innerTree)) {
      return foundTree;
    }
  }
  return nullptr;
}

CodeAnalysis::TypeTree *
CodeAnalysis::TypeTree::getParentTreeForNode(const ASTNode &node,
                                             TypeTree *tree) {
  if (!tree) {
    tree = this;
  }
  for (auto &childTree : tree->nodes) {
    if (childTree.instance == node) {
      return tree;
    }
    if (auto *foundTree = getParentTreeForNode(node, &childTree)) {
      return foundTree;
    }
  }
  return nullptr;
}

CodeAnalysis::TypeTree *
CodeAnalysis::TypeTree::getDomainRootTree(const std::string &domainName) {
  for (auto &childTree : this->nodes) {
    if (!childTree.instance ||
        childTree.instance->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(childTree.instance);
    if ((decl->getEntityType() == "_domainDefinition" ||
         decl->getEntityType() == "gameDefinition") &&
        decl->getName() == domainName) {
      return &childTree;
    }
  }
  return this;
}

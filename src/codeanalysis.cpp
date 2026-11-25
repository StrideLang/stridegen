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

#include "stride/parser/generatorrangenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <set>
#include <sstream>

using namespace strd;

namespace {

std::shared_ptr<DeclarationNode>
resolveEntityOrArrayDecl(const ASTNode &node, const ScopeStack &scopeStack,
                         const ASTNode &tree) {
  if (!node) {
    return nullptr;
  }
  auto declProp = node->getCompilerProperty("declaration");
  if (declProp && (declProp->getNodeType() == AST::Declaration ||
                   declProp->getNodeType() == AST::ArrayDeclaration)) {
    return std::static_pointer_cast<DeclarationNode>(declProp);
  }
  return ASTQuery::findDeclarationForNode(node, scopeStack, tree);
}

std::shared_ptr<DeclarationNode>
findPortEntityDecl(const std::shared_ptr<DeclarationNode> &funcDecl,
                   const std::string &portType,
                   std::string *outPortEntityName = nullptr) {
  if (!funcDecl) {
    return nullptr;
  }
  auto portsNode = funcDecl->getPropertyValue("ports");
  auto entitiesNode = funcDecl->getPropertyValue("entities");
  if (!portsNode || portsNode->getNodeType() != AST::List || !entitiesNode ||
      entitiesNode->getNodeType() != AST::List) {
    return nullptr;
  }
  for (const auto &port : portsNode->getChildren()) {
    if (!port || port->getNodeType() != AST::Declaration) {
      continue;
    }
    auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
    if (portDecl->getEntityType() != portType) {
      continue;
    }
    auto portEntity = portDecl->getPropertyValue("entity");
    if (!portEntity) {
      continue;
    }
    std::string entityName = ASTQuery::getNodeName(portEntity);
    if (outPortEntityName) {
      *outPortEntityName = entityName;
    }
    return ASTQuery::findDeclarationForNode(
        portEntity, {{funcDecl, entitiesNode->getChildren()}}, nullptr);
  }
  return nullptr;
}

ASTNode resolveModuleRateBinding(const std::shared_ptr<FunctionNode> &funcNode,
                                 const std::shared_ptr<DeclarationNode> &funcDecl,
                                 const ASTNode &rateNode) {
  if (!rateNode || rateNode->getNodeType() == AST::None ||
      rateNode->getNodeType() == AST::MemberAccess) {
    return nullptr;
  }
  if (rateNode->getNodeType() != AST::Entity) {
    return rateNode;
  }
  std::string targetName = ASTQuery::getNodeName(rateNode);
  auto portsNode = funcDecl ? funcDecl->getPropertyValue("ports") : nullptr;
  auto entitiesNode =
      funcDecl ? funcDecl->getPropertyValue("entities") : nullptr;
  if (portsNode && portsNode->getNodeType() == AST::List) {
    for (const auto &port : portsNode->getChildren()) {
      if (!port || port->getNodeType() != AST::Declaration) {
        continue;
      }
      auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
      if (portDecl->getEntityType() != "propertyInputPort") {
        continue;
      }
      auto portEntity = portDecl->getPropertyValue("entity");
      if (!portEntity || ASTQuery::getNodeName(portEntity) != targetName) {
        continue;
      }
      auto nameProp = portDecl->getPropertyValue("name");
      std::string propName =
          (nameProp && nameProp->getNodeType() == AST::String)
              ? std::static_pointer_cast<ValueNode>(nameProp)->getStringValue()
              : portDecl->getName();
      if (funcNode) {
        if (auto argVal = funcNode->getPropertyValue(propName)) {
          return argVal;
        }
      }
      if (entitiesNode && entitiesNode->getNodeType() == AST::List) {
        if (auto targetDecl = ASTQuery::findDeclarationForNode(
                rateNode, {{funcDecl, entitiesNode->getChildren()}}, nullptr)) {
          if (auto defVal = targetDecl->getPropertyValue("default")) {
            return defVal;
          }
        }
      }
      return nullptr;
    }
  }
  return rateNode;
}

std::string getPropertyPortName(const std::shared_ptr<DeclarationNode> &portDecl) {
  if (!portDecl) {
    return {};
  }
  auto portNameNode = portDecl->getPropertyValue("name");
  if (portNameNode && portNameNode->getNodeType() == AST::String) {
    return std::static_pointer_cast<ValueNode>(portNameNode)->getStringValue();
  }
  return portDecl->getName();
}

int evaluateOuterPortSize(const std::shared_ptr<DeclarationNode> &portDecl,
                          const std::string &portPropName,
                          const std::shared_ptr<FunctionNode> &func,
                          ScopeStack scopeStack, const ASTNode &tree) {
  auto portEntity = portDecl->getPropertyValue("entity");
  const std::string &portKind = portDecl->getEntityType();
  const bool isInput =
      (portKind == "mainInputPort" || portKind == "propertyInputPort");
  int portSize = isInput
                     ? CodeAnalysis::getNodeNumInputs(portEntity, scopeStack,
                                                      tree)
                     : CodeAnalysis::getNodeNumOutputs(portEntity, scopeStack,
                                                       tree);
  if (portSize != CodeAnalysis::SIZE_MEMBER_ACCESS) {
    return portSize;
  }

  if (!scopeStack.empty()) {
    scopeStack.pop_back();
  }
  if (portKind == "mainInputPort") {
    if (auto inEntity = func ? func->getCompilerProperty("inputEntity") : nullptr) {
      return CodeAnalysis::getNodeNumOutputs(inEntity, scopeStack, tree);
    }
    return 1;
  }
  if (portKind == "mainOutputPort") {
    if (auto outEntity =
            func ? func->getCompilerProperty("outputEntity") : nullptr) {
      return CodeAnalysis::getNodeNumInputs(outEntity, scopeStack, tree);
    }
    if (func) {
      return CodeAnalysis::getNodeNumOutputs(
          func->getPropertyValue(portPropName), scopeStack, tree);
    }
    return 0;
  }
  if (func) {
    return CodeAnalysis::getNodeNumOutputs(
        func->getPropertyValue(portPropName), scopeStack, tree);
  }
  return 0;
}

double resolveRateThroughOuterScopes(ASTNode resolvedEntity,
                                     const ScopeStack &scopeStack,
                                     const ASTNode &tree) {
  ScopeStack outerScope = scopeStack;
  for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
    if (!resolvedEntity) {
      return -1.0;
    }
    double rate =
        CodeAnalysis::resolveRate(resolvedEntity, outerScope, tree, true);
    if (rate != -1.0) {
      return rate;
    }
    if (!it->first || it->first->getNodeType() != AST::Function) {
      if (!outerScope.empty()) {
        outerScope.pop_back();
      }
      continue;
    }
    auto functionNode = std::static_pointer_cast<FunctionNode>(it->first);
    auto funcDecl =
        ASTQuery::findDeclarationForNode(functionNode, outerScope, tree);
    if (funcDecl && (resolvedEntity->getNodeType() == AST::Declaration ||
                     resolvedEntity->getNodeType() == AST::ArrayDeclaration)) {
      resolvedEntity = CodeAnalysis::getMatchedOuterInstance(
          functionNode,
          std::static_pointer_cast<DeclarationNode>(resolvedEntity), funcDecl,
          outerScope, tree);
    } else {
      break;
    }
    if (!outerScope.empty()) {
      outerScope.pop_back();
    }
  }
  if (resolvedEntity) {
    double rate =
        CodeAnalysis::resolveRate(resolvedEntity, outerScope, tree, true);
    if (rate != -1.0) {
      return rate;
    }
  }
  return -1.0;
}

std::shared_ptr<DeclarationNode>
resolveConnectionEntityInternal(const ASTNode &node,
                                const ScopeStack &scopeStack,
                                const ASTNode &tree, bool downStream,
                                std::set<const void *> &visited) {
  if (!node || visited.count(node.get())) {
    return nullptr;
  }
  visited.insert(node.get());

  if (node->getNodeType() == AST::Declaration) {
    return std::static_pointer_cast<DeclarationNode>(node);
  }
  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    return ASTQuery::findDeclarationForNode(node, scopeStack, tree);
  }
  if (node->getNodeType() != AST::Function) {
    return nullptr;
  }

  auto funcDecl = std::static_pointer_cast<DeclarationNode>(
      node->getCompilerProperty("declaration"));
  if (!funcDecl) {
    return nullptr;
  }
  auto ports = funcDecl->getPropertyValue("ports");
  auto entities = funcDecl->getPropertyValue("entities");
  auto portDeclEntity = downStream ? ASTQuery::getModuleMainOutputPort(funcDecl)
                                   : ASTQuery::getModuleMainInputPort(funcDecl);
  if (!portDeclEntity || !ports || !entities) {
    return nullptr;
  }
  auto portEntity = portDeclEntity->getPropertyValue("entity");
  if (!portEntity) {
    return nullptr;
  }
  auto portEntityDecl = ASTQuery::findDeclarationForNode(
      portEntity, {{node, entities->getChildren()}}, nullptr);
  if (!portEntityDecl) {
    return nullptr;
  }
  auto entityDomain = CodeAnalysis::getNodeDomain(portEntityDecl, {}, nullptr);
  if (!entityDomain || entityDomain->getNodeType() != AST::MemberAccess) {
    std::cerr << "Unexpected port entity domain" << std::endl;
    return nullptr;
  }
  auto member = std::static_pointer_cast<MemberAccessNode>(entityDomain);
  assert(member->getPropertyName() == "domain");
  const std::string &portName = member->getEntity();

  for (const auto &port : ports->getChildren()) {
    if (!port || port->getNodeType() != AST::Declaration) {
      continue;
    }
    auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
    if (portDecl->getName() != portName) {
      continue;
    }
    const std::string &portType = portDecl->getEntityType();
    ASTNode outerEntity;
    bool traverseDown = downStream;
    if (portType == "mainOutputPort" && downStream) {
      outerEntity = node->getCompilerProperty("outputEntity");
      traverseDown = true;
    } else if (portType == "mainInputPort" && !downStream) {
      outerEntity = node->getCompilerProperty("inputEntity");
      traverseDown = false;
    } else {
      continue;
    }
    if (!outerEntity) {
      continue;
    }
    if (outerEntity->getNodeType() == AST::Entity ||
        outerEntity->getNodeType() == AST::Array) {
      return ASTQuery::findDeclarationForNode(outerEntity, scopeStack, tree);
    }
    return resolveConnectionEntityInternal(outerEntity, scopeStack, tree,
                                           traverseDown, visited);
  }
  return nullptr;
}

std::string buildTypeCacheKey(const ScopeStack &scopeStack) {
  if (scopeStack.empty()) {
    return "_cacheResolvedOutDataType";
  }
  std::ostringstream oss;
  oss << "_cacheResolvedOutDataType";
  for (const auto &s : scopeStack) {
    oss << "_" << std::hex << reinterpret_cast<uintptr_t>(s.first.get());
  }
  return oss.str();
}

std::vector<ASTNode> getEntityOrArrayDataTypes(const ASTNode &node,
                                               const ScopeStack &scope,
                                               const ASTNode &tree,
                                               bool includeBuffer) {
  auto decl = ASTQuery::findDeclarationForNode(node, scope, tree);
  if (!decl && tree) {
    decl = ASTQuery::findDeclarationForNode(node, {}, tree);
  }
  if (!decl) {
    std::cerr << __FILE__ << ":" << __LINE__
              << " Could not find node declaration for "
              << ASTQuery::getQualifiedName(node) << std::endl;
    return {};
  }

  const std::string &entityType = decl->getEntityType();
  if (entityType == "signal") {
    return {ASTQuery::getDataTypeForSignalDeclaration(decl)};
  }
  if (entityType == "iterator") {
    auto entityProp = decl->getPropertyValue("entity");
    if (entityProp && entityProp->getNodeType() != AST::None) {
      auto targetDecl = resolveEntityOrArrayDecl(entityProp, scope, tree);
      if (targetDecl && targetDecl->getEntityType() == "signal") {
        return {ASTQuery::getDataTypeForSignalDeclaration(targetDecl)};
      }
    }
    return {std::make_shared<EntityNode>(
        CodeAnalysis::getDataTypeForDeclaration(decl, scope, tree), __FILE__,
        __LINE__)};
  }
  if (entityType == "switch" || entityType == "trigger" ||
      entityType == "string" || entityType == "sequence" ||
      (includeBuffer && entityType == "buffer") ||
      ASTQuery::isConstant(decl, scope, tree)) {
    return {std::make_shared<EntityNode>(
        CodeAnalysis::getDataTypeForDeclaration(decl, scope, tree), __FILE__,
        __LINE__)};
  }
  std::cerr << __FILE__ << ":" << __LINE__
            << " Could not find type property in:" << decl->toText()
            << std::endl;
  return {};
}

std::vector<ASTNode>
getPortForwardedDataTypes(const std::shared_ptr<FunctionNode> &func,
                          const std::shared_ptr<DeclarationNode> &funcDecl,
                          bool forOutput, const ScopeStack &scope,
                          const ASTNode &tree) {
  auto primaryPort = forOutput ? ASTQuery::getModuleMainOutputPort(funcDecl)
                               : ASTQuery::getModuleMainInputPort(funcDecl);
  if (!primaryPort) {
    return {};
  }
  auto portEntityNode = primaryPort->getPropertyValue("entity");
  if (!portEntityNode) {
    return {};
  }
  auto entitiesNode = funcDecl->getPropertyValue("entities");
  ScopeStack internalScope;
  if (entitiesNode && entitiesNode->getNodeType() == AST::List) {
    internalScope.push_back({funcDecl, entitiesNode->getChildren()});
  }
  auto entityDecl =
      ASTQuery::findDeclarationForNode(portEntityNode, internalScope, nullptr);
  if (!entityDecl) {
    return {};
  }
  const std::string &entType = entityDecl->getEntityType();
  if (forOutput &&
      (entType == "switch" || entType == "trigger" || entType == "string")) {
    return {std::make_shared<EntityNode>(
        CodeAnalysis::getDataTypeForDeclaration(entityDecl, scope, tree),
        __FILE__, __LINE__)};
  }
  if (entType != "signal") {
    return {};
  }
  auto dataType = ASTQuery::getDataTypeForSignalDeclaration(entityDecl);
  if (!dataType) {
    return {};
  }
  if (dataType->getNodeType() != AST::MemberAccess) {
    return {dataType};
  }

  auto dataTypeMember = std::static_pointer_cast<MemberAccessNode>(dataType);
  if (dataTypeMember->getEntity() == primaryPort->getName()) {
    auto connected =
        func->getCompilerProperty(forOutput ? "mainOutput" : "mainInput");
    if (connected) {
      auto connectedDecl = connected->getCompilerProperty("declaration");
      if (connectedDecl &&
          (connectedDecl->getNodeType() == AST::Declaration ||
           connectedDecl->getNodeType() == AST::ArrayDeclaration)) {
        return {std::static_pointer_cast<DeclarationNode>(connectedDecl)
                    ->getPropertyValue("type")};
      }
    }
    return {};
  }

  auto oppositePort = forOutput ? ASTQuery::getModuleMainInputPort(funcDecl)
                                : ASTQuery::getModuleMainOutputPort(funcDecl);
  if (oppositePort && dataTypeMember->getEntity() == oppositePort->getName()) {
    auto connected =
        func->getCompilerProperty(forOutput ? "mainInput" : "mainOutput");
    if (!connected) {
      return {};
    }
    return forOutput ? CodeAnalysis::getOutputDataTypes(connected, scope, tree)
                     : CodeAnalysis::getInputDataTypes(connected, scope, tree);
  }
  return {};
}

std::string resolveTypeFromTypeProperty(const ASTNode &node) {
  if (!node) {
    return {};
  }
  if (node->getNodeType() == AST::Entity) {
    return std::static_pointer_cast<EntityNode>(node)->getName();
  }
  if (node->getNodeType() == AST::String) {
    return std::static_pointer_cast<ValueNode>(node)->getStringValue();
  }
  return ASTQuery::getNodeName(node);
}

int scoreOverloadCandidate(const std::shared_ptr<DeclarationNode> &decl,
                           const std::vector<std::string> &actualTypes,
                           const std::vector<ASTNode> &actualNodes,
                           bool hasStreamInput, const ASTNode &tree) {
  if (!decl || decl->getEntityType() != "platformModule") {
    return 0;
  }
  auto inputsNode = decl->getPropertyValue("inputs");
  if (!inputsNode) {
    return hasStreamInput ? -1 : 100;
  }
  if (!hasStreamInput || inputsNode->getNodeType() != AST::List) {
    return -1;
  }

  auto literalFits = [&](size_t i, const std::string &expected) {
    if (i >= actualNodes.size() || !actualNodes[i] ||
        actualNodes[i]->getNodeType() != AST::Int) {
      return false;
    }
    auto literal = std::static_pointer_cast<ValueNode>(actualNodes[i]);
    return literal->getIntValue() >= 0 &&
           DataTypes::resolve(expected, tree).isInteger();
  };
  auto conversionScore = [&](size_t i, const std::string &actual,
                             const std::string &expected) {
    const auto to = DataTypes::resolve(expected, tree);
    const bool fromInteger = literalFits(i, expected) ||
                             DataTypes::resolve(actual, tree).isInteger();
    return fromInteger && !to.isInteger() ? 3 : 5;
  };

  std::vector<std::string> fixedExpectedTypes;
  std::shared_ptr<DeclarationNode> variadicDecl;
  for (const auto &c : inputsNode->getChildren()) {
    if (c && c->getNodeType() == AST::Declaration) {
      auto d = std::static_pointer_cast<DeclarationNode>(c);
      if (d->getEntityType() == "variadic" ||
          d->getEntityType() == "constrainedList") {
        variadicDecl = d;
        break;
      }
    }
    fixedExpectedTypes.push_back(resolveTypeFromTypeProperty(c));
  }

  if (!variadicDecl && fixedExpectedTypes.size() != actualTypes.size()) {
    return -1;
  }
  if (variadicDecl && actualTypes.size() < fixedExpectedTypes.size()) {
    return -1;
  }

  int score = 0;
  for (size_t i = 0; i < fixedExpectedTypes.size(); ++i) {
    const auto &actual = actualTypes[i];
    const auto &expected = fixedExpectedTypes[i];
    if (actual.empty() || expected.empty()) {
      score += 1;
    } else if (actual == expected) {
      score += 10;
    } else if (literalFits(i, expected) ||
               DataTypes::implicitlyConvertible(actual, expected, tree)) {
      score += conversionScore(i, actual, expected);
    } else {
      return -1;
    }
  }

  if (!variadicDecl) {
    return score;
  }

  size_t variadicCount = actualTypes.size() - fixedExpectedTypes.size();
  int minItems = variadicDecl->getEntityType() == "variadic" ? 1 : 0;
  int maxItems = -1;
  auto minNode = variadicDecl->getPropertyValue("min");
  if (!minNode) {
    minNode = variadicDecl->getPropertyValue("minItems");
  }
  if (minNode && minNode->getNodeType() == AST::Int) {
    minItems = static_cast<int>(
        std::static_pointer_cast<ValueNode>(minNode)->getIntValue());
  }
  auto maxNode = variadicDecl->getPropertyValue("max");
  if (!maxNode) {
    maxNode = variadicDecl->getPropertyValue("maxItems");
  }
  if (maxNode && maxNode->getNodeType() == AST::Int) {
    maxItems = static_cast<int>(
        std::static_pointer_cast<ValueNode>(maxNode)->getIntValue());
  }
  if (static_cast<int>(variadicCount) < minItems) {
    return -1;
  }
  if (maxItems >= 0 && static_cast<int>(variadicCount) > maxItems) {
    return -1;
  }

  std::vector<std::string> allowedTypes;
  auto typesNode = variadicDecl->getPropertyValue("types");
  if (typesNode && typesNode->getNodeType() == AST::List) {
    for (const auto &tc : typesNode->getChildren()) {
      allowedTypes.push_back(resolveTypeFromTypeProperty(tc));
    }
  }

  for (size_t i = fixedExpectedTypes.size(); i < actualTypes.size(); ++i) {
    const auto &actual = actualTypes[i];
    if (allowedTypes.empty() || actual.empty()) {
      score += 1;
    } else {
      bool typeAllowed = false;
      for (const auto &allowed : allowedTypes) {
        if (actual == allowed) {
          typeAllowed = true;
          score += 10;
          break;
        }
        if (DataTypes::implicitlyConvertible(actual, allowed, tree)) {
          typeAllowed = true;
          score += 5;
          break;
        }
      }
      if (!typeAllowed) {
        return -1;
      }
    }
  }
  return score;
}

} // namespace

std::vector<std::string> CodeAnalysis::getUsedDomains(const ASTNode &tree) {
  std::vector<std::string> domains;
  if (!tree) {
    return domains;
  }
  auto addUnique = [&](const std::string &domainName) {
    if (!domainName.empty() && std::find(domains.begin(), domains.end(),
                                         domainName) == domains.end()) {
      domains.push_back(domainName);
    }
  };
  for (const ASTNode &node : tree->getChildren()) {
    if (!node) {
      continue;
    }
    if (node->getNodeType() == AST::Stream) {
      StreamNodeIterator it(std::static_pointer_cast<StreamNode>(node));
      while (it.hasNext()) {
        addUnique(CodeAnalysis::getNodeDomainName(it.next(), {}, tree));
      }
    } else if (node->getNodeType() == AST::Declaration ||
               node->getNodeType() == AST::ArrayDeclaration) {
      addUnique(CodeAnalysis::getNodeDomainName(node, ScopeStack(), tree));
    }
  }
  return domains;
}

std::vector<std::string> CodeAnalysis::getUsedFrameworks(const ASTNode &tree) {
  std::vector<std::string> domains = CodeAnalysis::getUsedDomains(tree);
  std::vector<std::string> usedFrameworks;
  for (const std::string &domain : domains) {
    usedFrameworks.push_back(ASTQuery::getFrameworkForDomain(domain, tree));
  }
  return usedFrameworks;
}

ASTNode CodeAnalysis::getNodeDomain(const ASTNode &node,
                                    const ScopeStack &scopeStack,
                                    const ASTNode &tree) {
  ASTNode domainNode = nullptr;
  if (!node) {
    return nullptr;
  }

  if (node->getNodeType() == AST::Entity) {
    auto declaration = resolveEntityOrArrayDecl(node, scopeStack, tree);
    if (declaration) {
      auto typeDeclaration =
          ASTQuery::findTypeDeclaration(declaration, scopeStack, tree);
      if (typeDeclaration &&
          typeDeclaration->getEntityType() == "platformModule") {
        if (auto domProp = node->getCompilerProperty("domain")) {
          domainNode = domProp->deepCopy();
          domainNode->setNamespaceList(node->getNamespaceList());
        }
      } else if (declaration->getDomain()) {
        domainNode = declaration->getDomain()->deepCopy();
        if (!node->getNamespaceList().empty()) {
          domainNode->setNamespaceList(node->getNamespaceList());
        }
      }
    }
  } else if (node->getNodeType() == AST::Array) {
    auto declaration = resolveEntityOrArrayDecl(node, scopeStack, tree);
    if (declaration && declaration->getDomain()) {
      domainNode = declaration->getDomain()->deepCopy();
      domainNode->setNamespaceList(node->getNamespaceList());
    }
  } else if (node->getNodeType() == AST::List) {
    for (const ASTNode &member : node->getChildren()) {
      if (!member || member->getNodeType() == AST::Int ||
          member->getNodeType() == AST::Real ||
          member->getNodeType() == AST::String ||
          member->getNodeType() == AST::Switch ||
          member->getNodeType() == AST::MemberAccess) {
        continue;
      }
      if (auto memberDom =
              CodeAnalysis::getNodeDomain(member, scopeStack, tree)) {
        return memberDom;
      }
    }
    if (!node->getChildren().empty()) {
      return CodeAnalysis::getNodeDomain(node->getChildren().front(),
                                         scopeStack, tree);
    }
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() == "reaction") {
      if (auto trigDom = decl->getCompilerProperty("triggerDomain")) {
        return trigDom;
      }
    } else if (decl->getDomain()) {
      domainNode = decl->getDomain()->deepCopy();
      if (auto frameworkNode = node->getCompilerProperty("framework")) {
        if (frameworkNode->getNodeType() == AST::String) {
          domainNode->addScope(
              std::static_pointer_cast<ValueNode>(frameworkNode)
                  ->getStringValue());
        }
      }
    }
  } else if (node->getNodeType() == AST::Function) {
    domainNode = node->getCompilerProperty("domain");
    if (!domainNode) {
      auto func = std::static_pointer_cast<FunctionNode>(node);
      auto funcDecl = ASTQuery::findDeclarationForNode(func, scopeStack, tree);
      if (funcDecl) {
        if (funcDecl->getEntityType() == "platformModule") {
          domainNode = funcDecl->getPropertyValue("domain");
        } else if (funcDecl->getName() == "Sleep" &&
                   funcDecl->getEntityType() == "module") {
          domainNode = func->getPropertyValue("domain");
        } else if (funcDecl->getEntityType() == "reaction") {
          domainNode = funcDecl->getPropertyValue("domain");
        } else if (funcDecl->getEntityType() == "_domainDefinition") {
          auto parentDomain = funcDecl->getPropertyValue("parentDomain");
          if (parentDomain && parentDomain->getNodeType() != AST::None) {
            domainNode = parentDomain->deepCopy();
          }
        }
      }
    }
  } else if (node->getNodeType() == AST::Expression) {
    ExpressionNode *expr = static_cast<ExpressionNode *>(node.get());
    if (expr->isUnary()) {
      domainNode =
          CodeAnalysis::getNodeDomain(expr->getValue(), scopeStack, tree);
    } else {
      if (auto samplingDom = expr->getCompilerProperty("samplingDomain")) {
        return samplingDom;
      }
      for (const auto &child : expr->getChildren()) {
        domainNode = CodeAnalysis::getNodeDomain(child, scopeStack, tree);
        if (domainNode) {
          break;
        }
      }
    }
  } else if (node->getNodeType() == AST::Stream) {
    StreamNode *stream = static_cast<StreamNode *>(node.get());
    ASTNode leftDomain = getNodeDomain(stream->getLeft(), scopeStack, tree);
    ASTNode rightDomain = getNodeDomain(stream->getRight(), scopeStack, tree);
    if (!leftDomain) {
      leftDomain = rightDomain;
    } else if (!rightDomain) {
      rightDomain = leftDomain;
    }
    if (leftDomain && rightDomain &&
        ASTQuery::getDomainIdentifier(leftDomain, scopeStack, tree) ==
            ASTQuery::getDomainIdentifier(rightDomain, scopeStack, tree)) {
      return leftDomain;
    }
  } else if (node->getNodeType() == AST::Real ||
             node->getNodeType() == AST::Int ||
             node->getNodeType() == AST::Switch ||
             node->getNodeType() == AST::String) {
    domainNode = std::static_pointer_cast<ValueNode>(node)->getDomain();
  } else if (node->getNodeType() == AST::MemberAccess) {
    domainNode = node->getCompilerProperty("domain");
  }

  return domainNode;
}

std::string CodeAnalysis::getNodeDomainName(const ASTNode &node,
                                            const ScopeStack &scopeStack,
                                            const ASTNode &tree) {
  ASTNode domainNode = CodeAnalysis::getNodeDomain(node, scopeStack, tree);
  if (domainNode) {
    return ASTQuery::getDomainIdentifier(domainNode, scopeStack, tree);
  }
  return {};
}

ASTNode CodeAnalysis::resolveDomain(const ASTNode &node,
                                    const ScopeStack &scopeStack,
                                    const ASTNode &tree, bool downStream) {
  auto entityDecl =
      CodeAnalysis::resolveConnectionEntity(node, scopeStack, tree, downStream);
  if (entityDecl) {
    return CodeAnalysis::getNodeDomain(entityDecl, scopeStack, tree);
  }
  return CodeAnalysis::getNodeDomain(node, scopeStack, tree);
}

double CodeAnalysis::getNodeRate(const ASTNode &node, ScopeStack scope,
                                 const ASTNode &tree) {
  if (!node) {
    return -1.0;
  }
  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    auto declaration = ASTQuery::findDeclarationForNode(node, scope, tree);
    if (declaration) {
      ASTNode rateNode = declaration->getPropertyValue("rate");
      if (rateNode) {
        std::vector<LangError> tempErrors;
        double rate = CodeAnalysis::resolveRateToFloat(rateNode, scope, tree,
                                                       &tempErrors);
        if (tempErrors.empty() && rate >= 0) {
          return rate;
        }
      }
    }
  } else if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    if (func->getRate() >= 0) {
      return func->getRate();
    }
    double outPortRate =
        getModulePortRate(func, "mainOutputPort", scope, tree);
    if (outPortRate >= 0) {
      return outPortRate;
    }
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    double rate = -1.0;
    for (const ASTNode &element : node->getChildren()) {
      if (auto samplingDom = node->getCompilerProperty("samplingDomain")) {
        std::string exprDomName =
            ASTQuery::getDomainIdentifier(samplingDom, scope, tree);
        std::string elemDomName =
            CodeAnalysis::getNodeDomainName(element, scope, tree);
        if (!exprDomName.empty() && !elemDomName.empty() &&
            exprDomName != elemDomName) {
          continue;
        }
      }
      double elementRate = CodeAnalysis::getNodeRate(element, scope, tree);
      if (elementRate != -1.0) {
        rate = elementRate;
      }
    }
    if (rate >= 0.0) {
      return rate;
    }
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    ASTNode rateNode = decl->getPropertyValue("rate");
    if (rateNode) {
      std::vector<LangError> tempErrors;
      double rate =
          CodeAnalysis::resolveRateToFloat(rateNode, scope, tree, &tempErrors);
      return tempErrors.empty() ? rate : -1.0;
    }
  } else if (node->getNodeType() == AST::Stream) {
    return CodeAnalysis::getNodeRate(
        static_cast<StreamNode *>(node.get())->getLeft(), scope, tree);
  }
  if (auto rateProp = node->getCompilerProperty("rate")) {
    if (rateProp->getNodeType() == AST::Real ||
        rateProp->getNodeType() == AST::Int) {
      return static_cast<ValueNode *>(rateProp.get())->toReal();
    }
  }
  return -1.0;
}

double CodeAnalysis::resolveRateToFloat(const ASTNode &rateNode,
                                        const ScopeStack &scope,
                                        const ASTNode &tree,
                                        std::vector<LangError> *errors) {
  if (!rateNode) {
    return -1.0;
  }
  if (rateNode->getNodeType() == AST::Int ||
      rateNode->getNodeType() == AST::Real) {
    return static_cast<ValueNode *>(rateNode.get())->toReal();
  }
  if (rateNode->getNodeType() == AST::Entity) {
    auto valueDeclaration = ASTQuery::findDeclarationForNode(rateNode, scope, tree);
    if (valueDeclaration &&
        ASTQuery::isConstant(valueDeclaration, scope, tree)) {
      auto property =
          ASTQuery::findPropertyByName(valueDeclaration->getProperties(), "value");
      if (property && property->getValue()) {
        auto val = property->getValue();
        if (val->getNodeType() == AST::Int || val->getNodeType() == AST::Real) {
          return static_cast<ValueNode *>(val.get())->toReal();
        }
      }
    }
  } else if (rateNode->getNodeType() == AST::Expression) {
    double rate =
        ASTFunctions::evaluateConstReal(rateNode, scope, tree, errors);
    if (!errors || errors->empty()) {
      return rate;
    }
  }
  return -1.0;
}

ASTNode CodeAnalysis::getModulePortRateExpr(
    const std::shared_ptr<FunctionNode> &funcNode, const std::string &portType,
    const ScopeStack &scope, const ASTNode &tree) {
  if (!funcNode) {
    return nullptr;
  }
  auto decls = ASTQuery::findAllDeclarationsForNode(funcNode, scope, tree);
  auto funcDecl = matchDefinitionToTypes(decls, funcNode, scope, tree);
  if (!funcDecl && !decls.empty()) {
    funcDecl = decls.front();
  }
  if (!funcDecl) {
    return nullptr;
  }
  auto portEntityDecl = findPortEntityDecl(funcDecl, portType);
  if (!portEntityDecl) {
    return nullptr;
  }
  return resolveModuleRateBinding(funcNode, funcDecl,
                                  portEntityDecl->getPropertyValue("rate"));
}

double CodeAnalysis::getModulePortRate(
    const std::shared_ptr<FunctionNode> &funcNode, const std::string &portType,
    const ScopeStack &scope, const ASTNode &tree) {
  if (!funcNode) {
    return -1.0;
  }
  auto decls = ASTQuery::findAllDeclarationsForNode(funcNode, scope, tree);
  auto funcDecl = matchDefinitionToTypes(decls, funcNode, scope, tree);
  if (!funcDecl && !decls.empty()) {
    funcDecl = decls.front();
  }
  if (!funcDecl) {
    return -1.0;
  }
  auto portEntityDecl = findPortEntityDecl(funcDecl, portType);
  if (!portEntityDecl) {
    return -1.0;
  }
  ASTNode resolvedRateNode = resolveModuleRateBinding(
      funcNode, funcDecl, portEntityDecl->getPropertyValue("rate"));
  if (!resolvedRateNode || resolvedRateNode->getNodeType() == AST::None) {
    return -1.0;
  }

  if (resolvedRateNode->getNodeType() == AST::MemberAccess) {
    auto pp = std::static_pointer_cast<MemberAccessNode>(resolvedRateNode);
    for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
      if (!it->first) {
        continue;
      }
      std::shared_ptr<FunctionNode> outerFunc;
      std::shared_ptr<DeclarationNode> outerDecl;
      if (it->first->getNodeType() == AST::Function) {
        outerFunc = std::static_pointer_cast<FunctionNode>(it->first);
        outerDecl = ASTQuery::findDeclarationForNode(outerFunc, scope, tree);
      } else if (it->first->getNodeType() == AST::Declaration) {
        outerDecl = std::static_pointer_cast<DeclarationNode>(it->first);
      }
      if (!outerDecl) {
        continue;
      }
      if (pp->getPropertyName() == "size") {
        int sz = evaluateSizeMemberAccess(pp->getEntity(), scope, outerDecl,
                                          outerFunc, tree);
        if (sz > 0) {
          return static_cast<double>(sz);
        }
      } else if (pp->getPropertyName() == "rate") {
        double r = evaluateRateMemberAccess(pp->getEntity(), scope, outerDecl,
                                            outerFunc, tree);
        if (r > 0) {
          return r;
        }
      }
    }
    return -1.0;
  }

  ScopeStack evalScope = scope;
  if (auto entitiesNode = funcDecl->getPropertyValue("entities")) {
    if (entitiesNode->getNodeType() == AST::List) {
      evalScope.push_back({funcDecl, entitiesNode->getChildren()});
    }
  }
  std::vector<LangError> tempErrors;
  double rate =
      resolveRateToFloat(resolvedRateNode, evalScope, tree, &tempErrors);
  return (tempErrors.empty() && rate >= 0.0) ? rate : -1.0;
}

std::shared_ptr<DeclarationNode> CodeAnalysis::resolveConnectionEntity(
    const ASTNode &node, const ScopeStack &scopeStack, const ASTNode &tree,
    bool downStream) {
  std::set<const void *> visited;
  return resolveConnectionEntityInternal(node, scopeStack, tree, downStream,
                                         visited);
}

ASTNode CodeAnalysis::getMatchedOuterInstance(
    const std::shared_ptr<FunctionNode> &functionNode,
    const std::shared_ptr<DeclarationNode> &entityDecl,
    const std::shared_ptr<DeclarationNode> &funcDecl,
    const ScopeStack &scopeStack, const ASTNode &tree) {
  if (!functionNode || !entityDecl || !funcDecl) {
    return nullptr;
  }
  auto entitiesNode = funcDecl->getPropertyValue("entities");
  auto portsNode = funcDecl->getPropertyValue("ports");
  if (!entitiesNode || !portsNode) {
    return nullptr;
  }
  const auto &internalEntities = entitiesNode->getChildren();
  for (const auto &port : portsNode->getChildren()) {
    if (!port || port->getNodeType() != AST::Declaration) {
      continue;
    }
    auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
    auto portEntity = portDecl->getPropertyValue("entity");
    auto portEntityDecl = ASTQuery::findDeclarationForNode(
        portEntity, {{functionNode, internalEntities}}, nullptr);
    if (!portEntityDecl || portEntityDecl->getName() != entityDecl->getName()) {
      continue;
    }
    if (portDecl->getEntityType() == "mainOutputPort") {
      auto outputEntity = functionNode->getCompilerProperty("outputEntity");
      return outputEntity ? ASTQuery::getInstance(outputEntity, scopeStack, tree)
                          : nullptr;
    }
    if (portDecl->getEntityType() == "mainInputPort") {
      auto inputEntity = functionNode->getCompilerProperty("inputEntity");
      return inputEntity ? ASTQuery::getInstance(inputEntity, scopeStack, tree)
                         : nullptr;
    }
    auto propNameNode = portDecl->getPropertyValue("name");
    if (propNameNode && propNameNode->getNodeType() == AST::String) {
      auto outerEntity = functionNode->getPropertyValue(
          std::static_pointer_cast<ValueNode>(propNameNode)->getStringValue());
      if (outerEntity) {
        if (outerEntity->getNodeType() == AST::Entity ||
            outerEntity->getNodeType() == AST::Array) {
          return ASTQuery::findDeclarationForNode(outerEntity, scopeStack, tree);
        }
        return outerEntity;
      }
    }
    break;
  }
  return nullptr;
}

int CodeAnalysis::evaluateSizeMemberAccess(
    const std::string &targetPortName, ScopeStack scopeStack,
    const std::shared_ptr<DeclarationNode> &decl,
    const std::shared_ptr<FunctionNode> &func, const ASTNode &tree) {
  if (!decl) {
    return 0;
  }
  auto portsNode = decl->getPropertyValue("ports");
  auto entitiesNode = decl->getPropertyValue("entities");
  int portSize = 0;

  if (portsNode && entitiesNode) {
    for (const auto &port : portsNode->getChildren()) {
      if (!port || port->getNodeType() != AST::Declaration) {
        continue;
      }
      auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
      auto portEntity = portDecl->getPropertyValue("entity");
      if (portDecl->getName() != targetPortName &&
          (!portEntity || ASTQuery::getNodeName(portEntity) != targetPortName)) {
        continue;
      }
      if (portEntity && (portEntity->getNodeType() == AST::Entity ||
                         portEntity->getNodeType() == AST::Array)) {
        portSize = evaluateOuterPortSize(
            portDecl, getPropertyPortName(portDecl), func, scopeStack, tree);
      } else {
        std::cerr << "Unexpected port entity entry" << std::endl;
      }
      break;
    }
  }

  if (portSize == 0) {
    ScopeStack lookupScope = scopeStack;
    if (entitiesNode && entitiesNode->getNodeType() == AST::List) {
      lookupScope.push_back({decl, entitiesNode->getChildren()});
    }
    if (auto targetDecl =
            ASTQuery::findDeclarationByName(targetPortName, lookupScope, tree)) {
      if (targetDecl->getNodeType() == AST::ArrayDeclaration) {
        std::vector<LangError> errs;
        int sz = ASTQuery::getEntityDeclaredSize(targetDecl, lookupScope, tree,
                                                 &errs);
        if (errs.empty() && sz > 0) {
          portSize = sz;
        }
      } else if (targetDecl->getEntityType() == "buffer") {
        int sz = CodeAnalysis::getTypeNumOutputs(targetDecl, lookupScope, tree,
                                                 nullptr);
        if (sz > 0) {
          portSize = sz;
        }
      }
    }
  }
  return portSize;
}

double CodeAnalysis::evaluateRateMemberAccess(
    const std::string &targetPortName, ScopeStack scopeStack,
    const std::shared_ptr<DeclarationNode> &decl,
    const std::shared_ptr<FunctionNode> &func, const ASTNode &tree) {
  if (!decl) {
    return -1.0;
  }
  auto portsNode = decl->getPropertyValue("ports");
  if (!portsNode) {
    return -1.0;
  }
  for (const auto &port : portsNode->getChildren()) {
    if (!port || port->getNodeType() != AST::Declaration) {
      continue;
    }
    auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
    auto portEntity = portDecl->getPropertyValue("entity");
    if (portDecl->getName() != targetPortName &&
        (!portEntity || ASTQuery::getNodeName(portEntity) != targetPortName)) {
      continue;
    }
    if (!portEntity || (portEntity->getNodeType() != AST::Entity &&
                        portEntity->getNodeType() != AST::Array)) {
      std::cerr << "Unexpected port entity entry" << std::endl;
      continue;
    }

    double rate =
        CodeAnalysis::resolveRate(portEntity, scopeStack, tree, false);
    if (rate != -1.0) {
      return rate;
    }

    const std::string &portKind = portDecl->getEntityType();
    if (portKind == "mainInputPort") {
      auto inEntity = func ? func->getCompilerProperty("inputEntity") : nullptr;
      if (!inEntity) {
        return 1.0;
      }
      auto resolvedEntity = ASTQuery::getInstance(inEntity, scopeStack, tree);
      return resolveRateThroughOuterScopes(resolvedEntity, scopeStack, tree);
    }
    if (portKind == "mainOutputPort") {
      auto outEntity =
          func ? func->getCompilerProperty("outputEntity") : nullptr;
      if (!outEntity) {
        return -1.0;
      }
      auto resolvedEntity = ASTQuery::getInstance(outEntity, scopeStack, tree);
      ScopeStack callerScope = scopeStack;
      if (!callerScope.empty() && callerScope.back().first == decl) {
        callerScope.pop_back();
      }
      if (resolvedEntity && resolvedEntity->getNodeType() == AST::Function) {
        double portRate = CodeAnalysis::getModulePortRate(
            std::static_pointer_cast<FunctionNode>(resolvedEntity),
            "mainInputPort", callerScope, tree);
        if (portRate > 0) {
          return portRate;
        }
      }
      return resolveRateThroughOuterScopes(resolvedEntity, scopeStack, tree);
    }
    if (portKind == "propertyInputPort" || portKind == "propertyOutputPort") {
      auto resolvedEntity = ASTQuery::getInstance(portEntity, scopeStack, tree);
      return resolveRateThroughOuterScopes(resolvedEntity, scopeStack, tree);
    }
  }
  return -1.0;
}

double CodeAnalysis::resolveRate(const ASTNode &node,
                                 const ScopeStack &scopeStack,
                                 const ASTNode &tree, bool downStream) {
  auto entityDecl =
      CodeAnalysis::resolveConnectionEntity(node, scopeStack, tree, downStream);
  if (entityDecl) {
    return CodeAnalysis::getNodeRate(entityDecl, scopeStack, tree);
  }
  if (node) {
    return CodeAnalysis::getNodeRate(node, scopeStack, tree);
  }
  return -1.0;
}

int CodeAnalysis::getNodeNumOutputs(const ASTNode &node,
                                    const ScopeStack &scope,
                                    const ASTNode &tree,
                                    std::vector<LangError> *errors) {
  if (!node) {
    return 1;
  }
  if (node->getNodeType() == AST::List) {
    int size = 0;
    for (const ASTNode &member : node->getChildren()) {
      int newSize =
          CodeAnalysis::getNodeNumOutputs(member, scope, tree, errors);
      if (newSize < 0) {
        return newSize;
      }
      size += newSize;
    }
    return size;
  }
  if (node->getNodeType() == AST::Int || node->getNodeType() == AST::Real ||
      node->getNodeType() == AST::String || node->getNodeType() == AST::Switch) {
    return 1;
  }
  if (node->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(node);
    if (expr->isUnary()) {
      return getNodeNumOutputs(expr->getValue(), scope, tree, errors);
    }
    int leftSize = getNodeNumOutputs(expr->getLeft(), scope, tree, errors);
    int rightSize = getNodeNumOutputs(expr->getRight(), scope, tree, errors);
    if (leftSize < 0 || rightSize < 0) {
      return std::min(leftSize, rightSize);
    }
    return std::max(leftSize, rightSize);
  }
  if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    auto decls = ASTQuery::findAllDeclarationsForNode(func, scope, tree);
    auto funcDecl = matchDefinitionToTypes(decls, func, scope, tree);
    if (!funcDecl) {
      funcDecl = ASTQuery::findDeclarationForNode(func, scope, tree);
    }
    if (!funcDecl) {
      return CodeAnalysis::SIZE_UNKNOWN;
    }
    int typeOutputs = getTypeNumOutputs(funcDecl, scope, tree, errors);
    if (typeOutputs < 0) {
      return typeOutputs;
    }
    int dataSize = CodeAnalysis::getFunctionDataSize(func, scope, tree, errors);
    return typeOutputs * dataSize;
  }
  if (node->getNodeType() == AST::Array) {
    return ASTQuery::getArraySize(std::static_pointer_cast<ArrayNode>(node),
                                  scope, tree, errors);
  }
  if (node->getNodeType() == AST::Entity) {
    auto decl = resolveEntityOrArrayDecl(node, scope, tree);
    return decl ? getTypeNumOutputs(decl, scope, tree, errors)
                : CodeAnalysis::SIZE_UNKNOWN;
  }
  if (node->getNodeType() == AST::MemberAccess) {
    auto member = std::static_pointer_cast<MemberAccessNode>(node);
    auto entityDecl = ASTQuery::findDeclarationByName(
        member->getEntity(), scope, tree, member->getNamespaceList());
    if (entityDecl && entityDecl->getEntityType() == "buffer") {
      return member->getPropertyName() == "frame"
                 ? getTypeNumOutputs(entityDecl, scope, tree, errors)
                 : 1;
    }
    const auto &propName = member->getPropertyName();
    if (propName == "size" || propName == "rate" || propName == "domain") {
      return CodeAnalysis::SIZE_MEMBER_ACCESS;
    }
    if (propName == "status") {
      return 1;
    }
    std::cerr << "Unknown member access in getNodeNumOutputs() setting size to 1"
              << std::endl;
    return 1;
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    return ASTQuery::getEntityDeclaredSize(
        std::static_pointer_cast<DeclarationNode>(node), scope, tree, errors);
  }
  if (node->getNodeType() == AST::GeneratorRange) {
    auto genRange = std::static_pointer_cast<GeneratorRangeNode>(node);
    std::vector<LangError> evalErrors;
    int64_t start = ASTFunctions::evaluateConstInteger(
        genRange->startIndex(), scope, tree, &evalErrors);
    int64_t end = ASTFunctions::evaluateConstInteger(genRange->endIndex(),
                                                     scope, tree, &evalErrors);
    int64_t step = 1;
    if (genRange->stepIndex()) {
      step = ASTFunctions::evaluateConstInteger(genRange->stepIndex(), scope,
                                                tree, &evalErrors);
    }
    if (evalErrors.empty() && step != 0) {
      if (step > 0 && end >= start) {
        return static_cast<int>((end - start) / step + 1);
      }
      if (step < 0 && end <= start) {
        return static_cast<int>((start - end) / (-step) + 1);
      }
      return 0;
    }
    return CodeAnalysis::SIZE_UNKNOWN;
  }
  return CodeAnalysis::SIZE_UNKNOWN;
}

int CodeAnalysis::getNodeNumInputs(const ASTNode &node, ScopeStack scope,
                                   const ASTNode &tree,
                                   std::vector<LangError> *errors) {
  if (!node) {
    return 0;
  }
  if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    auto funcDecl = ASTQuery::findDeclarationForNode(func, scope, tree);
    if (!funcDecl) {
      return CodeAnalysis::SIZE_UNKNOWN;
    }
    if (funcDecl->getEntityType() == "reaction" ||
        funcDecl->getEntityType() == "_domainDefinition") {
      return 1; // Reactions and domains always take 1 input trigger
    }
    auto subScope = ASTQuery::getModuleEntities(funcDecl);
    scope.push_back({func, subScope});
    int numInputs = getTypeNumInputs(funcDecl, scope, tree, errors);
    if (numInputs < 0) {
      return numInputs;
    }
    int dataSize = CodeAnalysis::getFunctionDataSize(func, scope, tree, errors);
    return numInputs * dataSize;
  }
  if (node->getNodeType() == AST::Stream) {
    return CodeAnalysis::getNodeNumInputs(
        static_cast<StreamNode *>(node.get())->getLeft(), scope, tree, errors);
  }
  if (node->getNodeType() == AST::Entity) {
    auto decl = resolveEntityOrArrayDecl(node, scope, tree);
    return decl ? getTypeNumInputs(decl, scope, tree, errors)
                : CodeAnalysis::SIZE_UNKNOWN;
  }
  if (node->getNodeType() == AST::Array) {
    return ASTQuery::getArraySize(std::static_pointer_cast<ArrayNode>(node),
                                  scope, tree, errors);
  }
  if (node->getNodeType() == AST::List) {
    int size = 0;
    for (const ASTNode &member : node->getChildren()) {
      int memberIn = CodeAnalysis::getNodeNumInputs(member, scope, tree, errors);
      if (memberIn < 0) {
        return memberIn;
      }
      size += memberIn;
    }
    return size;
  }
  if (node->getNodeType() == AST::MemberAccess) {
    return CodeAnalysis::SIZE_UNKNOWN;
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    return getTypeNumInputs(std::static_pointer_cast<DeclarationNode>(node),
                            scope, tree, errors);
  }
  return 0;
}

int CodeAnalysis::getTypeNumOutputs(
    const std::shared_ptr<DeclarationNode> &entityDeclaration,
    const ScopeStack &scope, const ASTNode &tree,
    std::vector<LangError> *errors) {
  if (!entityDeclaration) {
    return 0;
  }
  if (entityDeclaration->getNodeType() == AST::ArrayDeclaration) {
    return ASTQuery::getEntityDeclaredSize(entityDeclaration, scope, tree,
                                           errors);
  }
  if (entityDeclaration->getNodeType() != AST::Declaration) {
    return 0;
  }
  const std::string &entType = entityDeclaration->getEntityType();
  if (entType == "module" || entType == "loop") {
    auto portDecl = ASTQuery::getModuleMainOutputPort(entityDeclaration);
    ASTNode outputEntity = portDecl ? portDecl->getPropertyValue("entity") : nullptr;
    if (!outputEntity || outputEntity->getNodeType() != AST::Entity) {
      return 0;
    }
    auto subScope = ASTQuery::getModuleEntities(entityDeclaration);
    auto outDecl = ASTQuery::findDeclarationForNode(
        outputEntity, {{entityDeclaration, subScope}}, nullptr);
    if (!outDecl) {
      return 0;
    }
    if (outDecl->getNodeType() == AST::ArrayDeclaration) {
      return ASTQuery::getEntityDeclaredSize(outDecl, scope, tree, errors);
    }
    return 1;
  }
  if (entType == "platformModule") {
    auto outputList = entityDeclaration->getPropertyValue("outputs");
    if (outputList && outputList->getNodeType() == AST::List) {
      return static_cast<int>(outputList->getChildren().size());
    }
  } else if (entType == "buffer") {
    auto evaluateSize = [&](const std::string &name) -> int64_t {
      auto prop = entityDeclaration->getPropertyValue(name);
      if (!prop || prop->getNodeType() == AST::None) {
        return -1;
      }
      std::vector<LangError> sizeErrors;
      int64_t sz =
          ASTFunctions::evaluateConstInteger(prop, scope, tree, &sizeErrors);
      return sizeErrors.empty() ? sz : -1;
    };
    if (int64_t frameSize = evaluateSize("frameSize"); frameSize > 0) {
      return static_cast<int>(frameSize);
    }
    if (int64_t capacity = evaluateSize("capacity"); capacity > 0) {
      return static_cast<int>(capacity);
    }
    LOG_ERROR() << "Missing or unexpected capacity property for buffer"
                << std::endl;
  }
  return 1;
}

int CodeAnalysis::getTypeNumInputs(
    const std::shared_ptr<DeclarationNode> &entityDeclaration,
    const ScopeStack &scope, const ASTNode &tree,
    std::vector<LangError> *errors) {
  if (!entityDeclaration) {
    return 0;
  }
  if (entityDeclaration->getNodeType() == AST::ArrayDeclaration) {
    return ASTQuery::getEntityDeclaredSize(entityDeclaration, scope, tree,
                                           errors);
  }
  if (entityDeclaration->getNodeType() != AST::Declaration) {
    return 0;
  }
  const std::string &entType = entityDeclaration->getEntityType();
  if (entType == "module" || entType == "loop") {
    auto portDecl = ASTQuery::getModuleMainInputPort(entityDeclaration);
    ASTNode inputEntity = portDecl ? portDecl->getPropertyValue("entity") : nullptr;
    if (!inputEntity || inputEntity->getNodeType() != AST::Entity) {
      return 0;
    }
    auto subScope = ASTQuery::getModuleEntities(entityDeclaration);
    auto inDecl = ASTQuery::findDeclarationForNode(
        inputEntity, {{entityDeclaration, subScope}}, nullptr);
    if (!inDecl) {
      return subScope.empty() ? 1 : 0;
    }
    if (inDecl->getNodeType() == AST::ArrayDeclaration) {
      return ASTQuery::getEntityDeclaredSize(inDecl, scope, tree, errors);
    }
    return 1;
  }
  if (entType == "platformModule") {
    auto inputList = entityDeclaration->getPropertyValue("inputs");
    if (inputList && inputList->getNodeType() == AST::List) {
      for (const auto &child : inputList->getChildren()) {
        if (child && child->getNodeType() == AST::Declaration) {
          const auto &childType =
              std::static_pointer_cast<DeclarationNode>(child)->getEntityType();
          if (childType == "variadic" || childType == "constrainedList") {
            return CodeAnalysis::SIZE_MEMBER_ACCESS;
          }
        }
      }
      return static_cast<int>(inputList->getChildren().size());
    }
  } else if (entType == "buffer") {
    auto modeProp = entityDeclaration->getPropertyValue("mode");
    if (modeProp && ASTQuery::getNodeName(modeProp) == "ModeTable") {
      return ASTQuery::getEntityDeclaredSize(entityDeclaration, scope, tree,
                                             errors);
    }
    return 1;
  } else {
    int size = ASTQuery::getNodeSize(entityDeclaration, scope, tree);
    int internalSize = 1;
    auto typeDeclaration = ASTQuery::findTypeDeclarationByName(
        entityDeclaration->getEntityType(), scope, tree);
    if (typeDeclaration &&
        (typeDeclaration->getEntityType() == "platformModule" ||
         typeDeclaration->getEntityType() == "platformEntity")) {
      auto inputs = typeDeclaration->getPropertyValue("inputs");
      if (inputs && inputs->getNodeType() == AST::List) {
        for (const auto &child : inputs->getChildren()) {
          if (child && child->getNodeType() == AST::Declaration) {
            const auto &childType =
                std::static_pointer_cast<DeclarationNode>(child)
                    ->getEntityType();
            if (childType == "variadic" || childType == "constrainedList") {
              return CodeAnalysis::SIZE_MEMBER_ACCESS;
            }
          }
        }
        internalSize = static_cast<int>(inputs->getChildren().size());
      }
    }
    return size * internalSize;
  }
  return 1;
}

int CodeAnalysis::getFunctionDataSize(
    const std::shared_ptr<FunctionNode> &func, const ScopeStack &scope,
    const ASTNode &tree, std::vector<LangError> *errors) {
  if (!func) {
    return 1;
  }
  auto ports = func->getProperties();
  if (ports.empty()) {
    return 1;
  }
  auto decl = ASTQuery::findDeclarationForNode(func, scope, tree);
  int size = 1;
  for (const auto &port : ports) {
    ASTNode value = port->getValue();
    int argSize = CodeAnalysis::getNodeNumOutputs(value, scope, tree, errors);
    int declaredPortSize = 1;

    if (decl && decl->getPropertyValue("ports")) {
      auto entities = decl->getPropertyValue("entities");
      for (const auto &portDeclNode :
           decl->getPropertyValue("ports")->getChildren()) {
        if (!portDeclNode || portDeclNode->getNodeType() != AST::Declaration) {
          continue;
        }
        auto portDecl = std::static_pointer_cast<DeclarationNode>(portDeclNode);
        if (port->getName() != getPropertyPortName(portDecl)) {
          continue;
        }
        auto portEntityNode = portDecl->getPropertyValue("entity");
        if (portEntityNode && entities) {
          ScopeStack entScope = {{decl, entities->getChildren()}};
          if (portDecl->getEntityType() == "mainInputPort" ||
              portDecl->getEntityType() == "propertyInputPort") {
            declaredPortSize = CodeAnalysis::getNodeNumInputs(
                portEntityNode, entScope, tree, errors);
          } else if (portDecl->getEntityType() == "mainOutputPort" ||
                     portDecl->getEntityType() == "propertyOutputPort") {
            declaredPortSize = CodeAnalysis::getNodeNumOutputs(
                portEntityNode, entScope, tree, errors);
          }
        }
        break;
      }
    }

    int bundleFactor = 1;
    if (declaredPortSize == 1 && argSize > 0) {
      bundleFactor = argSize;
    } else if (declaredPortSize > 1 && argSize > declaredPortSize &&
               argSize % declaredPortSize == 0) {
      bundleFactor = argSize / declaredPortSize;
    }
    if (size == 1 && bundleFactor > 1) {
      size = bundleFactor;
    }
  }
  return size;
}

int CodeAnalysis::getFunctionNumInstances(
    const std::shared_ptr<FunctionNode> &func, ScopeStack scope,
    const ASTNode &tree, std::vector<LangError> *errors) {
  if (!func) {
    return 1;
  }
  auto funcDecl = ASTQuery::findDeclarationForNode(func, scope, tree);
  if (funcDecl && funcDecl->getPropertyValue("entities")) {
    scope.push_back(
        {func, funcDecl->getPropertyValue("entities")->getChildren()});
  }
  int numInstances = 1;
  auto ports = funcDecl ? funcDecl->getPropertyValue("ports") : nullptr;
  if (!ports) {
    return numInstances;
  }
  for (const auto &propertyNode : func->getProperties()) {
    auto portEntity = propertyNode->getValue();
    const auto &portName = propertyNode->getName();
    for (const auto &port : ports->getChildren()) {
      if (!port || port->getNodeType() != AST::Declaration) {
        continue;
      }
      auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
      auto nameNode = portDecl->getPropertyValue("name");
      if (!nameNode || nameNode->getNodeType() != AST::String) {
        continue;
      }
      if (std::static_pointer_cast<ValueNode>(nameNode)->getStringValue() !=
          portName) {
        continue;
      }
      const bool isInput = (portDecl->getEntityType() == "mainInputPort" ||
                            portDecl->getEntityType() == "propertyInputPort");
      int propertySize =
          isInput
              ? CodeAnalysis::getNodeNumOutputs(portEntity, scope, tree, errors)
              : CodeAnalysis::getNodeNumInputs(portEntity, scope, tree, errors);
      auto internalPortEntity = portDecl->getPropertyValue("entity");
      if (!internalPortEntity) {
        continue;
      }
      int internalSize =
          isInput ? CodeAnalysis::getNodeNumOutputs(internalPortEntity, scope,
                                                    tree, errors)
                  : CodeAnalysis::getNodeNumInputs(internalPortEntity, scope,
                                                   tree, errors);
      if (internalSize == CodeAnalysis::SIZE_MEMBER_ACCESS) {
        return 1;
      }
      if (internalSize <= 0) {
        continue;
      }
      int portNumInstances = propertySize / internalSize;
      assert(propertySize / internalSize ==
             static_cast<float>(propertySize) / internalSize);
      if (numInstances == 0 || (numInstances == 1 && portNumInstances > 1)) {
        numInstances = portNumInstances;
      } else if (numInstances != portNumInstances && portNumInstances != 1) {
        std::cerr << "ERROR size mismatch in function ports" << std::endl;
      }
    }
  }
  return numInstances;
}

std::vector<ASTNode> CodeAnalysis::getInputDataTypes(const ASTNode &node,
                                                     const ScopeStack &scope,
                                                     const ASTNode &tree) {
  if (!node) {
    return {};
  }
  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    return getEntityOrArrayDataTypes(node, scope, tree,
                                     node->getNodeType() == AST::Entity);
  }
  if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    auto funcDecl = ASTQuery::findDeclarationForNode(func, scope, tree);
    if (!funcDecl) {
      return {};
    }
    return getPortForwardedDataTypes(func, funcDecl, false, scope, tree);
  }
  return {};
}

std::vector<ASTNode> CodeAnalysis::getOutputDataTypes(const ASTNode &node,
                                                      const ScopeStack &scope,
                                                      const ASTNode &tree) {
  if (!node) {
    return {};
  }
  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    return getEntityOrArrayDataTypes(node, scope, tree, false);
  }
  if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    auto funcDecl = ASTQuery::findDeclarationForNode(func, scope, tree);
    if (!funcDecl) {
      return {};
    }
    if (funcDecl->getEntityType() == "platformModule") {
      std::vector<ASTNode> types;
      auto outputsNode = funcDecl->getPropertyValue("outputs");
      if (outputsNode && outputsNode->getNodeType() == AST::List) {
        for (const auto &output : outputsNode->getChildren()) {
          if (!output || output->getNodeType() != AST::Entity) {
            return {};
          }
          types.push_back(std::make_shared<EntityNode>(
              ASTQuery::getNodeName(output), __FILE__, __LINE__));
        }
      }
      return types;
    }
    return getPortForwardedDataTypes(func, funcDecl, true, scope, tree);
  }
  if (node->getNodeType() == AST::List) {
    std::vector<ASTNode> types;
    for (const auto &child : node->getChildren()) {
      auto childTypes = getOutputDataTypes(child, scope, tree);
      types.insert(types.end(), childTypes.begin(), childTypes.end());
    }
    return types;
  }
  if (node->getNodeType() == AST::Expression ||
      node->getNodeType() == AST::Int || node->getNodeType() == AST::Real ||
      node->getNodeType() == AST::Switch || node->getNodeType() == AST::String) {
    std::string typeName = resolveNodeOutDataType(node, scope, tree);
    return {std::make_shared<EntityNode>(typeName, __FILE__, __LINE__)};
  }
  return {};
}

std::string CodeAnalysis::getDataTypeForDeclaration(
    const std::shared_ptr<DeclarationNode> &decl, const ScopeStack &scope,
    const ASTNode &tree) {
  if (!decl) {
    return {};
  }
  if (decl->getEntityType() == "signal") {
    auto typeNode = ASTQuery::getDataTypeForSignalDeclaration(decl);
    if (typeNode && typeNode->getNodeType() == AST::Entity) {
      return std::static_pointer_cast<EntityNode>(typeNode)->getName();
    }
    return DataTypes::Real;
  }
  if (ASTQuery::isConstant(decl, scope, tree)) {
    auto property = ASTQuery::findPropertyByName(decl->getProperties(), "value");
    if (property && property->getValue() &&
        property->getValue()->getNodeType() != AST::None) {
      return resolveNodeOutDataType(property->getValue(), scope, tree);
    }
    auto typeNode = decl->getPropertyValue("type");
    if (typeNode && typeNode->getNodeType() == AST::Entity) {
      return std::static_pointer_cast<EntityNode>(typeNode)->getName();
    }
  } else if (decl->getEntityType() == "string") {
    return DataTypes::String;
  } else if (ASTQuery::isStorageEntity(decl)) {
    return ASTQuery::getStorageElementDataType(decl);
  } else if (decl->getEntityType() == "switch" ||
             decl->getEntityType() == "trigger") {
    return DataTypes::Switch;
  } else if (decl->getEntityType() == "iterator") {
    auto entityProp = decl->getPropertyValue("entity");
    if (entityProp && entityProp->getNodeType() != AST::None) {
      auto targetDecl = resolveEntityOrArrayDecl(entityProp, scope, tree);
      if (!targetDecl && tree) {
        targetDecl = ASTQuery::findDeclarationForNode(entityProp, {}, tree);
      }
      if (targetDecl) {
        return getDataTypeForDeclaration(targetDecl, scope, tree);
      }
    }
    for (const char *propName : {"default", "limit", "increment"}) {
      auto propVal = decl->getPropertyValue(propName);
      if (propVal && propVal->getNodeType() != AST::None &&
          (propVal->getNodeType() == AST::Real ||
           resolveNodeOutDataType(propVal, scope, tree) == DataTypes::Real)) {
        return DataTypes::Real;
      }
    }
    return DataTypes::Int;
  } else if (decl->getEntityType() == "sequence") {
    for (const char *propName :
         {"from", "default", "limit", "step", "increment"}) {
      auto propVal = decl->getPropertyValue(propName);
      if (propVal && propVal->getNodeType() != AST::None &&
          (propVal->getNodeType() == AST::Real ||
           resolveNodeOutDataType(propVal, scope, tree) == DataTypes::Real)) {
        return DataTypes::Real;
      }
    }
    return DataTypes::Int;
  }
  return {};
}

std::string CodeAnalysis::resolveEntityDataType(const ASTNode &node,
                                                const ScopeStack &scopeStack,
                                                const ASTNode &tree) {
  if (!node || (node->getNodeType() != AST::Entity &&
                node->getNodeType() != AST::Array)) {
    std::cerr << __FILE__ << ":" << __LINE__
              << " Error, expecting entity or array" << std::endl;
    return {};
  }
  auto declaration = resolveEntityOrArrayDecl(node, scopeStack, tree);
  if (!declaration) {
    return {};
  }
  if (declaration->getEntityType() == "signal") {
    ASTNode typeNode = declaration->getPropertyValue("type");
    if (typeNode && typeNode->getNodeType() == AST::Entity) {
      return std::static_pointer_cast<EntityNode>(typeNode)->getName();
    }
    return {};
  }
  return getDataTypeForDeclaration(declaration, scopeStack, tree);
}

std::string CodeAnalysis::resolveNodeOutDataType(const ASTNode &node,
                                                 const ScopeStack &scopeStack,
                                                 const ASTNode &tree) {
  if (!node) {
    return {};
  }
  const std::string cacheKey = buildTypeCacheKey(scopeStack);
  if (auto typeProp = node->getCompilerProperty(cacheKey)) {
    if (typeProp->getNodeType() == AST::String) {
      return std::static_pointer_cast<ValueNode>(typeProp)->getStringValue();
    }
  }
  std::string result = resolveNodeOutDataTypeInternal(node, scopeStack, tree);
  if (!result.empty()) {
    node->setCompilerProperty(
        cacheKey, std::make_shared<ValueNode>(result, __FILE__, __LINE__));
  }
  return result;
}

std::string CodeAnalysis::resolveNodeOutDataTypeInternal(
    const ASTNode &node, const ScopeStack &scopeStack, const ASTNode &tree) {
  switch (node->getNodeType()) {
  case AST::Int:
  case AST::Real:
    return DataTypes::literalType(*std::static_pointer_cast<ValueNode>(node));
  case AST::Switch:
    return DataTypes::Switch;
  case AST::String:
    return DataTypes::String;
  case AST::List:
    return resolveListDataType(static_cast<ListNode *>(node.get()), scopeStack,
                               tree);
  case AST::Entity:
  case AST::Array:
    return resolveEntityDataType(node, scopeStack, tree);
  case AST::Expression:
    return resolveExpressionDataType(static_cast<ExpressionNode *>(node.get()),
                                     scopeStack, tree);
  case AST::Range:
    return resolveRangeDataType(static_cast<RangeNode *>(node.get()),
                                scopeStack, tree);
  case AST::GeneratorRange:
    return DataTypes::Int;
  case AST::MemberAccess:
    return resolveMemberAccessDataType(
        static_cast<MemberAccessNode *>(node.get()), scopeStack, tree);
  default:
    return {};
  }
}

std::string CodeAnalysis::resolveListDataType(ListNode *listNode,
                                              const ScopeStack &scopeStack,
                                              const ASTNode &tree) {
  const std::vector<ASTNode> &members = listNode->getChildren();
  if (members.empty()) {
    return {};
  }
  std::string type = resolveNodeOutDataType(members.front(), scopeStack, tree);
  for (const ASTNode &member : members) {
    std::string nextType = resolveNodeOutDataType(member, scopeStack, tree);
    if (type != nextType) {
      type = DataTypes::promote(type, nextType, tree);
      if (type.empty()) {
        return {};
      }
    }
  }
  return type;
}

std::string CodeAnalysis::resolveExpressionDataType(
    ExpressionNode *exprNode, const ScopeStack &scopeStack,
    const ASTNode &tree) {
  if (exprNode->isUnary()) {
    return resolveNodeOutDataType(exprNode->getValue(), scopeStack, tree);
  }
  ASTNode left = exprNode->getLeft();
  ASTNode right = exprNode->getRight();
  std::string leftType = resolveNodeOutDataType(left, scopeStack, tree);
  std::string rightType = resolveNodeOutDataType(right, scopeStack, tree);

  auto takesType = [&](const ASTNode &operand, const std::string &type) {
    return operand &&
           (operand->getNodeType() == AST::Int ||
            operand->getNodeType() == AST::Real) &&
           DataTypes::untypedLiteralTakes(
               *std::static_pointer_cast<ValueNode>(operand), type, tree);
  };
  if (takesType(left, rightType)) {
    return rightType;
  }
  if (takesType(right, leftType)) {
    return leftType;
  }

  std::string promoted = DataTypes::promote(leftType, rightType, tree);
  if (leftType != rightType) {
    std::string risk = DataTypes::promotionRisk(leftType, rightType, tree);
    if (!risk.empty()) {
      // Read by CodeValidator::findLossyConversions()
      exprNode->setCompilerProperty(
          "promotionRisk",
          std::make_shared<ValueNode>(risk, __FILE__, __LINE__));
      exprNode->setCompilerProperty(
          "promotedType",
          std::make_shared<ValueNode>(promoted, __FILE__, __LINE__));
    }
  }
  return promoted;
}

std::string CodeAnalysis::resolveRangeDataType(RangeNode *rangeNode,
                                               const ScopeStack &scopeStack,
                                               const ASTNode &tree) {
  std::string leftType =
      resolveNodeOutDataType(rangeNode->startIndex(), scopeStack, tree);
  std::string rightType =
      resolveNodeOutDataType(rangeNode->endIndex(), scopeStack, tree);
  return (leftType == rightType) ? leftType : std::string();
}

std::string CodeAnalysis::resolveMemberAccessDataType(
    MemberAccessNode *memberAccess, const ScopeStack &scopeStack,
    const ASTNode &tree) {
  if (!memberAccess) {
    return {};
  }
  if (memberAccess->getPropertyName() == "size") {
    return DataTypes::Int;
  }
  if (memberAccess->getPropertyName() == "rate") {
    return DataTypes::Real;
  }
  const std::string &entityName = memberAccess->getEntity();
  const std::string &propName = memberAccess->getPropertyName();
  if (!entityName.empty()) {
    auto entityDecl = ASTQuery::findDeclarationByName(
        entityName, scopeStack, tree, memberAccess->getNamespaceList());
    if (entityDecl) {
      auto propVal = entityDecl->getPropertyValue(propName);
      if (!propVal) {
        if (auto propDecl = ASTQuery::findPropertyDeclaration(
                entityDecl, propName, scopeStack, tree)) {
          propVal = propDecl->getPropertyValue("default");
        }
      }
      if (propVal) {
        std::string type = resolveNodeOutDataType(propVal, scopeStack, tree);
        if (!type.empty()) {
          return type;
        }
      }
    }
  }
  return DataTypes::Real;
}

std::shared_ptr<DeclarationNode> CodeAnalysis::matchDefinitionToTypes(
    const std::vector<std::shared_ptr<DeclarationNode>> &decls,
    const std::shared_ptr<FunctionNode> &func, const ScopeStack &scope,
    const ASTNode &tree) {
  std::vector<std::string> actualTypes;
  std::vector<ASTNode> actualNodes;
  bool hasStreamInput = false;

  auto resolveInputType = [&](const ASTNode &node) -> std::string {
    if (!node) {
      return {};
    }
    if (auto typecastProp = node->getCompilerProperty("typecast")) {
      if (typecastProp->getNodeType() == AST::String) {
        return std::static_pointer_cast<ValueNode>(typecastProp)
            ->getStringValue();
      }
    }
    std::string t = CodeAnalysis::resolveNodeOutDataType(node, scope, tree);
    if (!t.empty()) {
      return t;
    }
    auto connDecl =
        CodeAnalysis::resolveConnectionEntity(node, scope, tree, true);
    if (!connDecl) {
      connDecl =
          CodeAnalysis::resolveConnectionEntity(node, scope, tree, false);
    }
    if (connDecl) {
      t = CodeAnalysis::getDataTypeForDeclaration(connDecl, scope, tree);
      if (!t.empty()) {
        return t;
      }
    }
    if (node->getNodeType() == AST::Function) {
      auto prevFunc = std::static_pointer_cast<FunctionNode>(node);
      auto prevDecls = ASTQuery::findAllDeclarationsForNode(prevFunc, scope, tree);
      for (const auto &pd : prevDecls) {
        if (pd && pd->getEntityType() == "platformModule") {
          auto outputsProp = pd->getPropertyValue("outputs");
          if (outputsProp && outputsProp->getNodeType() == AST::List &&
              !outputsProp->getChildren().empty()) {
            t = resolveTypeFromTypeProperty(outputsProp->getChildren()[0]);
            if (!t.empty()) {
              break;
            }
          }
        }
      }
    }
    return t;
  };

  auto streamInput = func ? func->getCompilerProperty("mainInput") : nullptr;
  if (streamInput) {
    hasStreamInput = true;
    if (streamInput->getNodeType() == AST::List) {
      for (const auto &child : streamInput->getChildren()) {
        actualTypes.push_back(resolveInputType(child));
        actualNodes.push_back(child);
      }
    } else {
      actualTypes.push_back(resolveInputType(streamInput));
      actualNodes.push_back(streamInput);
    }
  } else if (func) {
    auto connDecl =
        CodeAnalysis::resolveConnectionEntity(func, scope, tree, false);
    if (connDecl) {
      std::string t =
          CodeAnalysis::getDataTypeForDeclaration(connDecl, scope, tree);
      if (!t.empty()) {
        hasStreamInput = true;
        actualTypes.push_back(t);
      }
    }
  }

  std::shared_ptr<DeclarationNode> funcDecl;
  int bestScore = -1;
  for (const auto &decl : decls) {
    if (decl && decl->getEntityType() == "platformModule") {
      int s = scoreOverloadCandidate(decl, actualTypes, actualNodes,
                                     hasStreamInput, tree);
      if (s > bestScore) {
        bestScore = s;
        funcDecl = decl;
      }
    }
  }

  if (!funcDecl && actualTypes.empty()) {
    for (const auto &decl : decls) {
      if (decl && decl->getEntityType() == "platformModule") {
        auto inputs = decl->getPropertyValue("inputs");
        if (inputs &&
            (!hasStreamInput || inputs->getNodeType() != AST::List)) {
          continue;
        }
        funcDecl = decl;
        break;
      }
    }
  }

  if (!funcDecl && !decls.empty()) {
    funcDecl = decls.front();
  }

  assert(!funcDecl || !func || funcDecl->getName() == func->getName() ||
         funcDecl->getName().find(func->getName() + "@") == 0);
  return funcDecl;
}

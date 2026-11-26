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

    Author: Andres Cabrera
*/

#include "stride/stridegen/cppcodegen/domainconnectionbuilder.hpp"
#include "stride/parser/functionnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/stridegen/stridesystem.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

#include <algorithm>
#include <set>

using namespace strd;

namespace {

const std::set<std::string> &reservedConnectionProps() {
  static const std::set<std::string> props = {
      "sourceFramework",    "destinationFramework", "sourceDomains",
      "destinationDomains", "sourceStreams",        "destinationStreams",
      "sourceImports",      "destinationImports",   "sourceEntities",
      "destinationEntities", "meta",                "label"};
  return props;
}

void collectOverridesFromProps(
    const std::string &connName,
    const std::vector<std::shared_ptr<PropertyNode>> &props,
    std::map<std::string, std::map<std::string, ASTNode>> &overrides) {
  const auto &reserved = reservedConnectionProps();
  for (const auto &prop : props) {
    if (prop && !reserved.count(prop->getName())) {
      overrides[connName][prop->getName()] = prop->getValue();
    }
  }
}

void collectConnectorsAndOverrides(
    const ASTNode &tree,
    std::vector<std::shared_ptr<DeclarationNode>> &allConnectors,
    std::map<std::string, std::map<std::string, ASTNode>> &connectionOverrides) {
  if (!tree) {
    return;
  }
  for (const auto &node : tree->getChildren()) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() == "connection") {
      bool isBlueprint = decl->getPropertyValue("sourceStreams") ||
                         decl->getPropertyValue("destinationStreams") ||
                         decl->getPropertyValue("sourceDomains") ||
                         decl->getPropertyValue("sourceFramework") ||
                         decl->getPropertyValue("sourceEntities") ||
                         decl->getPropertyValue("destinationEntities");
      bool alreadyExists = std::any_of(
          allConnectors.begin(), allConnectors.end(),
          [&](const auto &c) { return c->getName() == decl->getName(); });
      if (isBlueprint && !alreadyExists) {
        allConnectors.push_back(decl);
      } else {
        collectOverridesFromProps(decl->getName(), decl->getProperties(),
                                  connectionOverrides);
      }
    } else if (decl->getEntityType() == "system") {
      auto conns = decl->getPropertyValue("connections");
      if (conns && conns->getNodeType() == AST::List) {
        for (const auto &item : conns->getChildren()) {
          if (!item) {
            continue;
          }
          if (item->getNodeType() == AST::Declaration) {
            auto itemDecl = std::static_pointer_cast<DeclarationNode>(item);
            collectOverridesFromProps(itemDecl->getName(),
                                      itemDecl->getProperties(),
                                      connectionOverrides);
          } else if (item->getNodeType() == AST::Function) {
            auto itemFunc = std::static_pointer_cast<FunctionNode>(item);
            collectOverridesFromProps(itemFunc->getName(),
                                      itemFunc->getProperties(),
                                      connectionOverrides);
          }
        }
      }
    }
  }
}

bool matchesEndpoint(const ASTNode &domainsList, const ASTNode &frameworkProp,
                     const std::string &domainName,
                     const std::string &domainFramework) {
  if (domainsList && domainsList->getNodeType() == AST::List) {
    if (domainsList->getChildren().empty()) {
      std::string fw =
          frameworkProp ? ASTQuery::getNodeName(frameworkProp) : "";
      return fw.empty() || fw == domainFramework || fw == domainName;
    }
    for (const auto &dom : domainsList->getChildren()) {
      std::string name = ASTQuery::getNodeName(dom);
      if (name == domainName || name == domainFramework) {
        return true;
      }
    }
    return false;
  }
  if (frameworkProp) {
    std::string fw = ASTQuery::getNodeName(frameworkProp);
    return fw == domainFramework || fw == domainName;
  }
  return false;
}

ASTNode resolveConnectionImports(StrideSystem &system,
                                 const ASTNode &importsProp,
                                 const ASTNode &frameworkProp) {
  if (!importsProp) {
    return nullptr;
  }
  std::string fwStr = ASTQuery::valueText(frameworkProp);
  if (fwStr.empty()) {
    return nullptr;
  }

  ASTNode combinedTree = std::make_shared<AST>();
  for (const auto &import : importsProp->getChildren()) {
    if (!import || import->getNodeType() != AST::String) {
      continue;
    }
    auto importName =
        std::static_pointer_cast<ValueNode>(import)->getStringValue();
    auto importNodes = system.loadImportTree(importName, "", fwStr);
    auto builtinObjects = system.getImportTrees();
    ASTNode localTree = std::make_shared<AST>();
    for (const auto &n : importNodes) {
      localTree->addChild(n);
    }
    for (const auto &n : importNodes) {
      ASTFunctions::fillDefaultPropertiesForNode(n, importNodes);
      ASTFunctions::insertRequiredObjectsForNode(n, builtinObjects, localTree);
    }
    system.injectResourceConfiguration(localTree);
    for (const auto &c : localTree->getChildren()) {
      combinedTree->addChild(c);
    }
  }
  return combinedTree->getChildren().empty() ? nullptr : combinedTree;
}

std::shared_ptr<DeclarationNode>
findEndpointDeclaration(const ASTNode &endpoint, const ASTNode &imports,
                        const ASTNode &entities, const std::string &fwAlias,
                        const ASTNode &tree) {
  const std::string name = ASTQuery::getNodeName(endpoint);
  auto decl = ASTQuery::findDeclarationByName(name, {}, imports, {}, fwAlias);
  if (!decl && entities) {
    decl = ASTQuery::findDeclarationByName(name, {}, entities, {}, fwAlias);
    if (!decl) {
      decl = ASTQuery::findDeclarationByName(name, {}, entities, {});
    }
  }
  if (!decl) {
    decl = ASTQuery::findDeclarationByName(name, {}, tree, {});
  }
  return decl;
}

void addUniqueEntitiesAndImports(const ASTNode &imports,
                                 const ASTNode &entities,
                                 const std::string &domainId,
                                 const std::string &framework,
                                 std::vector<ASTNode> &newChildren) {
  if (imports) {
    for (const auto &imp : imports->getChildren()) {
      if (imp) {
        newChildren.push_back(imp->deepCopy());
      }
    }
  }
  if (!entities) {
    return;
  }
  for (const auto &child : entities->getChildren()) {
    if (!child || (child->getNodeType() != AST::Declaration &&
                   child->getNodeType() != AST::ArrayDeclaration)) {
      continue;
    }
    auto childDecl = std::static_pointer_cast<DeclarationNode>(child);
    if (!childDecl->getPropertyValue("domain")) {
      childDecl->setPropertyValue(
          "domain", std::make_shared<EntityNode>(domainId, __FILE__, __LINE__));
    }
    if (!childDecl->getCompilerProperty("domain")) {
      childDecl->setCompilerProperty(
          "domain", std::make_shared<EntityNode>(domainId, __FILE__, __LINE__));
    }
    if (!framework.empty() && !childDecl->getCompilerProperty("framework")) {
      childDecl->setCompilerProperty(
          "framework",
          std::make_shared<ValueNode>(framework, __FILE__, __LINE__));
    }
    bool alreadyPresent = std::any_of(
        newChildren.begin(), newChildren.end(), [&](const auto &existing) {
          return existing &&
                 (existing->getNodeType() == AST::Declaration ||
                  existing->getNodeType() == AST::ArrayDeclaration) &&
                 std::static_pointer_cast<DeclarationNode>(existing)
                         ->getName() == childDecl->getName();
        });
    if (!alreadyPresent) {
      newChildren.push_back(childDecl);
    }
  }
}

void splitDomainId(const std::string &domainId, const ASTNode &tree,
                   std::string &outFramework, std::string &outShortId) {
  auto sep = domainId.find("::");
  if (sep != std::string::npos) {
    outFramework = domainId.substr(0, sep);
    outShortId = domainId.substr(sep + 2);
  } else {
    outShortId = domainId;
    if (tree) {
      outFramework = ASTQuery::getFrameworkForDomain(domainId, tree);
    }
  }
}

} // namespace

ASTNode DomainConnectionBuilder::substitutePropertiesInAST(
    ASTNode node, const std::map<std::string, ASTNode> &props) {
  if (!node || props.empty()) {
    return node;
  }
  if (node->getNodeType() == AST::Entity) {
    auto entity = std::static_pointer_cast<EntityNode>(node);
    auto it = props.find(entity->getName());
    if (it != props.end() && it->second) {
      ASTNode replacement = it->second->deepCopy();
      node->copyLocationTo(replacement);
      return replacement;
    }
    return node;
  }
  if (node->getNodeType() == AST::MemberAccess) {
    auto member = std::static_pointer_cast<MemberAccessNode>(node);
    std::string key = member->getEntity().empty()
                          ? member->getPropertyName()
                          : (member->getEntity() + "." +
                             member->getPropertyName());
    auto it = props.find(key);
    if (it == props.end() && !member->getEntity().empty()) {
      it = props.find(member->getPropertyName());
    }
    if (it != props.end() && it->second) {
      ASTNode replacement = it->second->deepCopy();
      node->copyLocationTo(replacement);
      return replacement;
    }
    return node;
  }
  if (node->getNodeType() == AST::Stream) {
    auto stream = std::static_pointer_cast<StreamNode>(node);
    stream->setLeft(substitutePropertiesInAST(stream->getLeft(), props));
    stream->setRight(substitutePropertiesInAST(stream->getRight(), props));
    return stream;
  }

  std::vector<ASTNode> newChildren;
  bool changed = false;
  for (const auto &child : node->getChildren()) {
    auto newChild = substitutePropertiesInAST(child, props);
    if (newChild != child) {
      changed = true;
    }
    newChildren.push_back(newChild);
  }
  if (changed) {
    node->setChildren(newChildren);
  }
  return node;
}

ConnectionNodes DomainConnectionBuilder::getDomainChangeStreams(
    StrideSystem &system, std::string previousDomainId,
    std::string nextDomainId, const ASTNode &tree) {
  ConnectionNodes domainChangeNodes;

  std::vector<std::shared_ptr<DeclarationNode>> allConnectors =
      system.getConnectionDefinitions();
  std::map<std::string, std::map<std::string, ASTNode>> connectionOverrides;
  collectConnectorsAndOverrides(tree, allConnectors, connectionOverrides);

  std::string prevFramework, prevShortId;
  splitDomainId(previousDomainId, tree, prevFramework, prevShortId);
  std::string nextFramework, nextShortId;
  splitDomainId(nextDomainId, tree, nextFramework, nextShortId);

  const auto &reserved = reservedConnectionProps();
  for (const auto &connector : allConnectors) {
    bool sourceMatch = matchesEndpoint(
        connector->getPropertyValue("sourceDomains"),
        connector->getPropertyValue("sourceFramework"), prevShortId,
        prevFramework);
    bool destMatch = matchesEndpoint(
        connector->getPropertyValue("destinationDomains"),
        connector->getPropertyValue("destinationFramework"), nextShortId,
        nextFramework);

    if (!sourceMatch || !destMatch) {
      continue;
    }

    auto sourceStreams = connector->getPropertyValue("sourceStreams");
    auto destStreams = connector->getPropertyValue("destinationStreams");
    if (!sourceStreams || !destStreams) {
      continue;
    }

    std::map<std::string, ASTNode> effectiveProps;
    for (const auto &prop : connector->getProperties()) {
      if (prop && !reserved.count(prop->getName())) {
        effectiveProps[prop->getName()] = prop->getValue();
      }
    }
    if (auto ovIt = connectionOverrides.find(connector->getName());
        ovIt != connectionOverrides.end()) {
      for (const auto &[k, v] : ovIt->second) {
        effectiveProps[k] = v;
      }
    }

    domainChangeNodes.sourceStreams =
        substitutePropertiesInAST(sourceStreams->deepCopy(), effectiveProps);
    domainChangeNodes.destStreams =
        substitutePropertiesInAST(destStreams->deepCopy(), effectiveProps);
    if (auto sourceEntities = connector->getPropertyValue("sourceEntities")) {
      domainChangeNodes.sourceEntities =
          substitutePropertiesInAST(sourceEntities->deepCopy(), effectiveProps);
    }
    if (auto destEntities = connector->getPropertyValue("destinationEntities")) {
      domainChangeNodes.destEntities =
          substitutePropertiesInAST(destEntities->deepCopy(), effectiveProps);
    }
    domainChangeNodes.sourceImports = resolveConnectionImports(
        system, connector->getPropertyValue("sourceImports"),
        connector->getPropertyValue("sourceFramework"));
    domainChangeNodes.destImports = resolveConnectionImports(
        system, connector->getPropertyValue("destinationImports"),
        connector->getPropertyValue("destinationFramework"));
    break;
  }

  return domainChangeNodes;
}

void DomainConnectionBuilder::generateDomainConnections(StrideSystem &system,
                                                        const ASTNode &tree) {
  if (!tree) {
    return;
  }
  std::vector<ASTNode> newChildren;
  for (const auto &node : tree->getChildren()) {
    if (!node || node->getNodeType() != AST::Stream) {
      newChildren.push_back(node);
      continue;
    }

    auto stream = std::static_pointer_cast<StreamNode>(node);
    std::vector<std::shared_ptr<StreamNode>> newStreams;
    std::shared_ptr<StreamNode> activeHead = stream;

    while (stream && stream->getRight()) {
      ASTNode originalRight = stream->getRight();
      ASTNode next =
          (originalRight->getNodeType() == AST::Stream)
              ? std::static_pointer_cast<StreamNode>(originalRight)->getLeft()
              : originalRight;

      auto previousDomainId = ASTQuery::getDomainIdentifier(
          CodeAnalysis::getNodeDomain(stream->getLeft(), {}, tree), {}, tree);
      auto nextDomainId = ASTQuery::getDomainIdentifier(
          CodeAnalysis::getNodeDomain(next, {}, tree), {}, tree);

      if (nextDomainId != previousDomainId) {
        auto domainChangeNodes = getDomainChangeStreams(
            system, previousDomainId, nextDomainId, tree);

        auto nextInstance = ASTQuery::getInstance(next, {}, tree);
        if (nextInstance) {
          if (auto writes = nextInstance->getCompilerProperty("writes")) {
            auto newWrites = std::make_shared<ListNode>(__FILE__, __LINE__);
            for (const auto &w : writes->getChildren()) {
              if (w && w->getNodeType() == AST::Entity) {
                if (ASTQuery::getQualifiedName(w) != previousDomainId) {
                  newWrites->addChild(w);
                }
              }
            }
            nextInstance->setCompilerProperty("writes", newWrites);
          }
        }

        if (domainChangeNodes.sourceStreams &&
            !domainChangeNodes.sourceStreams->getChildren().empty() &&
            domainChangeNodes.sourceStreams->getChildren()[0]
                    ->getChildren()
                    .size() >= 2 &&
            domainChangeNodes.destStreams &&
            !domainChangeNodes.destStreams->getChildren().empty() &&
            !domainChangeNodes.destStreams->getChildren()[0]
                 ->getChildren()
                 .empty()) {
          ASTNode srcOut = domainChangeNodes.sourceStreams->getChildren()[0]
                               ->getChildren()[1];
          stream->setRight(srcOut);
          srcOut->setCompilerProperty("inputEntity", stream->getLeft());

          auto previousFramework =
              ASTQuery::getFrameworkForDomain(previousDomainId, tree);
          if (auto nodeDecl = findEndpointDeclaration(
                  srcOut, domainChangeNodes.sourceImports,
                  domainChangeNodes.sourceEntities,
                  system.getFrameworkAlias(previousFramework), tree)) {
            srcOut->setCompilerProperty("declaration", nodeDecl);
          }
          srcOut->setCompilerProperty(
              "domain", std::make_shared<EntityNode>(previousDomainId, __FILE__,
                                                     __LINE__));
          newStreams.push_back(activeHead);

          ASTNode connectionNode =
              domainChangeNodes.destStreams->getChildren()[0]->getChildren()[0];
          connectionNode->setCompilerProperty("outputEntity", next);
          auto destFramework =
              ASTQuery::getFrameworkForDomain(nextDomainId, tree);
          if (auto nodeDecl = findEndpointDeclaration(
                  connectionNode, domainChangeNodes.destImports,
                  domainChangeNodes.destEntities,
                  system.getFrameworkAlias(destFramework), tree)) {
            connectionNode->setCompilerProperty("declaration", nodeDecl);
          }
          connectionNode->setCompilerProperty(
              "domain",
              std::make_shared<EntityNode>(nextDomainId, __FILE__, __LINE__));

          if (auto connectionDomain =
                  CodeAnalysis::getNodeDomain(connectionNode, {}, tree);
              nextInstance && connectionDomain) {
            nextInstance->appendToPropertyValue("writes", connectionDomain);
          }

          auto newStream = std::make_shared<StreamNode>(
              connectionNode, originalRight, __FILE__, __LINE__);
          activeHead = newStream;
          stream = newStream;

          addUniqueEntitiesAndImports(
              domainChangeNodes.sourceImports, domainChangeNodes.sourceEntities,
              previousDomainId, previousFramework, newChildren);
          addUniqueEntitiesAndImports(
              domainChangeNodes.destImports, domainChangeNodes.destEntities,
              nextDomainId, destFramework, newChildren);
        }
      }

      if (stream->getRight() &&
          stream->getRight()->getNodeType() == AST::Stream) {
        stream = std::static_pointer_cast<StreamNode>(stream->getRight());
      } else {
        break;
      }
    }

    if (newStreams.empty()) {
      newChildren.push_back(node);
    } else {
      newStreams.push_back(activeHead);
      newChildren.insert(newChildren.end(), newStreams.begin(),
                         newStreams.end());
    }
  }
  tree->setChildren(newChildren);
}

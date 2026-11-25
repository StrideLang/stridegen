/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#include "astnormalizer.hpp"

#include "stride/parser/arraynode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

#include <cassert>
#include <functional>
#include <iostream>

using namespace strd;

std::shared_ptr<DeclarationNode>
AstNormalizer::createSignalDeclaration(const std::string &name, int size,
                                       const ScopeStack &scope,
                                       const ASTNode &tree) {
  std::string filename = tree ? tree->getFilename() : "";
  const char *fname = filename.c_str();
  int line = tree ? tree->getLine() : -1;
  int column = tree ? tree->getColumn() : -1;

  std::shared_ptr<DeclarationNode> newDecl;
  if (size <= 1) {
    newDecl = std::make_shared<DeclarationNode>(name, "signal", nullptr, fname,
                                                line, column);
  } else {
    auto indexList = std::make_shared<ListNode>(
        std::make_shared<ValueNode>(static_cast<int64_t>(size), fname, line,
                                    column),
        fname, line, column);
    auto arrayNode =
        std::make_shared<ArrayNode>(name, indexList, fname, line, column);
    newDecl = std::make_shared<DeclarationNode>(arrayNode, "signal", nullptr,
                                                fname, line, column);
  }
  assert(newDecl);
  // Lets the resolver turn it into a switch or string once its type is known
  newDecl->setCompilerProperty(
      "autoDeclared", std::make_shared<ValueNode>(true, __FILE__, __LINE__));
  if (tree) {
    tree->copyLocationTo(newDecl);
    ASTFunctions::fillDefaultPropertiesForNode(newDecl, tree->getChildren());
  }
  ASTFunctions::resolveConstantsInNode(newDecl, scope, tree);
  return newDecl;
}

void AstNormalizer::desugar() {
  std::set<const void *> visited;
  desugarSingleEntryStreams(m_ctx.tree, visited);
  if (m_ctx.system) {
    for (const auto &pair : m_ctx.system->getImportTrees()) {
      for (const auto &node : pair.second) {
        desugarSingleEntryStreams(node, visited);
      }
    }
  }
}

void AstNormalizer::desugarSingleEntryStreams(const ASTNode &node,
                                              std::set<const void *> &visited) {
  if (!node || !visited.insert(node.get()).second) {
    return;
  }

  if (node->getNodeType() == AST::Stream) {
    auto stream = std::static_pointer_cast<StreamNode>(node);
    if (!stream->getRight()) {
      ASTNode originalLeft = stream->getLeft();
      auto onNode = std::make_shared<ValueNode>(
          true, stream->getFilename().c_str(), stream->getLine());
      stream->setLeft(onNode);
      stream->setRight(originalLeft);
    } else {
      desugarSingleEntryStreams(stream->getLeft(), visited);
      desugarSingleEntryStreams(stream->getRight(), visited);
    }
    return;
  }

  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    auto streamEntities =
        ASTQuery::getContainedStreamEntities(decl, ScopeStack(), m_ctx.tree);
    for (const auto &ent : streamEntities) {
      for (const auto &stream : ent.streams) {
        desugarSingleEntryStreams(stream, visited);
      }
    }
    for (const auto &prop : decl->getProperties()) {
      if (prop && prop->getValue()) {
        desugarSingleEntryStreams(prop->getValue(), visited);
      }
    }
  }

  for (const auto &child : node->getChildren()) {
    desugarSingleEntryStreams(child, visited);
  }
}

void AstNormalizer::hoistDeclarationChildren() {
  // Move streams and entity/array declarations outside properties into the
  // "streams" and "entities" properties.
  auto appendToListProp = [](const std::shared_ptr<DeclarationNode> &decl,
                             const std::string &propName,
                             const ASTNode &child) {
    ASTNode current = decl->getPropertyValue(propName);
    if (!current || current->getNodeType() == AST::None) {
      decl->setPropertyValue(
          propName, std::make_shared<ListNode>(
                        child, child->getFilename().c_str(), child->getLine()));
    } else if (current->getNodeType() != AST::List) {
      auto list = std::make_shared<ListNode>(current, __FILE__, __LINE__);
      list->addChild(child);
      decl->setPropertyValue(propName, list);
    } else {
      current->addChild(child);
    }
  };

  std::function<void(const std::vector<ASTNode> &)> walkChildren =
      [&](const std::vector<ASTNode> &children) {
        for (const ASTNode &node : children) {
          if (!node) {
            continue;
          }
          if (node->getNodeType() == AST::Declaration ||
              node->getNodeType() == AST::ArrayDeclaration) {
            auto decl = std::static_pointer_cast<DeclarationNode>(node);
            std::vector<ASTNode> toDelete;
            for (const auto &child : decl->getChildren()) {
              if (!child) {
                continue;
              }
              if (child->getNodeType() == AST::Stream) {
                toDelete.push_back(child);
                appendToListProp(decl, "streams", child);
              } else if (child->getNodeType() == AST::Declaration ||
                         child->getNodeType() == AST::ArrayDeclaration) {
                toDelete.push_back(child);
                appendToListProp(decl, "entities", child);
              } else if (child->getNodeType() == AST::Property) {
                auto propVal =
                    std::static_pointer_cast<PropertyNode>(child)->getValue();
                if (!propVal) {
                  continue;
                }
                if (propVal->getNodeType() == AST::List) {
                  walkChildren(propVal->getChildren());
                } else {
                  walkChildren({propVal});
                }
              }
            }
            for (const auto &deadProp : toDelete) {
              decl->removeProperty(deadProp);
            }
          } else {
            walkChildren(node->getChildren());
          }
        }
      };

  walkChildren(m_ctx.tree->getChildren());
}

void AstNormalizer::declareModuleInternalEntities() {
  for (const ASTNode &node : m_ctx.tree->getChildren()) {
    declareInternalEntitiesForNode(node, {});
  }
}

void AstNormalizer::declareInternalEntitiesForNode(const ASTNode &node,
                                                   ScopeStack subScope) {
  if (!node || node->getNodeType() != AST::Declaration) {
    return;
  }
  auto decl = std::static_pointer_cast<DeclarationNode>(node);

  auto findDeclInFuncScope = [&](const ASTNode &portEntity,
                                 const ScopeStack &innerScope) {
    auto portEntityDecl =
        ASTQuery::findDeclarationForNode(portEntity, innerScope, nullptr);
    if (!portEntityDecl) {
      auto rootPortDecl =
          ASTQuery::findDeclarationForNode(portEntity, subScope, m_ctx.tree);
      if (rootPortDecl &&
          ASTQuery::isConstant(rootPortDecl, subScope, m_ctx.tree)) {
        portEntityDecl = rootPortDecl;
      }
    }
    return portEntityDecl;
  };

  if (decl->getEntityType() == "loop") {
    ASTNode portsNode = decl->getPropertyValue("ports");
    if (!portsNode || portsNode->getNodeType() == AST::None) {
      decl->replacePropertyValue(
          "ports", std::make_shared<ListNode>(nullptr, __FILE__, __LINE__));
    }
  }

  if (decl->getEntityType() != "module" &&
      decl->getEntityType() != "reaction" &&
      decl->getEntityType() != "loop") {
    return;
  }

  ASTNode internalEntities = decl->getPropertyValue("entities");
  if (!internalEntities || internalEntities->getNodeType() != AST::List) {
    decl->setPropertyValue(
        "entities",
        std::make_shared<ListNode>(internalEntities, __FILE__, __LINE__));
    internalEntities = decl->getPropertyValue("entities");
  }

  ASTNode mainPortsDefaultDomain;
  auto ensureMainPortEntity =
      [&](const std::shared_ptr<DeclarationNode> &portDecl,
          const std::string &baseName, bool skipConstDomainAssign) {
        if (!portDecl) {
          return;
        }
        ASTNode portEntity = portDecl->getPropertyValue("entity");
        ASTNode portDomain;
        if (!portDecl->getCompilerProperty("anonymous")) {
          portDomain = std::make_shared<MemberAccessNode>(
              portDecl->getName(), "domain", __FILE__, __LINE__);
          if (!mainPortsDefaultDomain) {
            mainPortsDefaultDomain = portDomain;
          }
        }

        if (!portEntity || portEntity->getNodeType() == AST::None) {
          std::string possibleName = baseName;
          auto existing = ASTQuery::findDeclarationByName(
              possibleName, ScopeStack(), internalEntities);
          int counter = 0;
          while (existing) {
            possibleName = baseName + "_" + std::to_string(counter++);
            existing = ASTQuery::findDeclarationByName(
                possibleName, ScopeStack(), internalEntities);
          }
          portEntity =
              std::make_shared<EntityNode>(possibleName, __FILE__, __LINE__);
          portDecl->setPropertyValue("entity", portEntity);
        }

        ScopeStack funcInternalScope = {{node, internalEntities->getChildren()}};
        auto portEntityDecl =
            findDeclInFuncScope(portEntity, funcInternalScope);
        if (!portEntityDecl) {
          portEntityDecl = createSignalDeclaration(
              ASTQuery::getNodeName(portEntity), 1, subScope, m_ctx.tree);
          internalEntities->addChild(portEntityDecl);
          ASTFunctions::fillDefaultPropertiesForNode(
              portEntityDecl, m_ctx.tree->getChildren());
        }

        ASTNode domainNode = CodeAnalysis::getNodeDomain(
            portEntity, funcInternalScope, m_ctx.tree);
        if (!domainNode || domainNode->getNodeType() == AST::None) {
          if (portEntityDecl && mainPortsDefaultDomain &&
              (!skipConstDomainAssign ||
               !ASTQuery::isConstant(portEntityDecl, funcInternalScope,
                                     m_ctx.tree))) {
            portEntityDecl->setPropertyValue("domain", mainPortsDefaultDomain);
          }
        }
      };

  ensureMainPortEntity(ASTQuery::getModuleMainOutputPort(decl), "Output",
                       false);
  ensureMainPortEntity(ASTQuery::getModuleMainInputPort(decl), "Input", true);

  ASTNode ports = decl->getPropertyValue("ports");
  internalEntities = decl->getPropertyValue("entities");
  if (ports && ports->getNodeType() == AST::List) {
    for (const ASTNode &port : ports->getChildren()) {
      if (!port || port->getNodeType() != AST::Declaration) {
        continue;
      }
      auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
      ASTNode entityPortValue = portDecl->getPropertyValue("entity");
      ASTNode portDomain = portDecl->getPropertyValue("domain");
      if (!portDomain && !portDecl->getCompilerProperty("anonymous")) {
        portDomain = std::make_shared<MemberAccessNode>(
            portDecl->getName(), "domain", __FILE__, __LINE__);
      }
      auto portRate = std::make_shared<MemberAccessNode>(
          portDecl->getName(), "rate", __FILE__, __LINE__);

      if (!entityPortValue) {
        continue;
      }
      if (entityPortValue->getNodeType() == AST::Entity) {
        auto nameNode = std::static_pointer_cast<EntityNode>(entityPortValue);
        auto portEntityDecl = findDeclInFuncScope(
            nameNode, {{node, internalEntities->getChildren()}});
        if (!portEntityDecl) {
          portEntityDecl = createSignalDeclaration(nameNode->getName(), 1,
                                                   subScope, m_ctx.tree);
          portEntityDecl->replacePropertyValue(
              "rate", std::make_shared<ValueNode>(__FILE__, __LINE__));
          internalEntities->addChild(portEntityDecl);
        }
        auto declType = ASTQuery::findTypeDeclaration(portEntityDecl, subScope,
                                                      m_ctx.tree);
        if (declType) {
          auto inheritList =
              ASTQuery::getInheritedTypes(declType, subScope, m_ctx.tree);
          bool inheritsDomainMember = false;
          for (const auto &inherits : inheritList) {
            auto typeName = inherits->getPropertyValue("typeName");
            if (typeName && typeName->getNodeType() == AST::String &&
                std::static_pointer_cast<ValueNode>(typeName)
                        ->getStringValue() == "domainMember") {
              inheritsDomainMember = true;
              break;
            }
          }
          if (inheritsDomainMember) {
            ASTNode entityDomain = portEntityDecl->getDomain();
            if ((!entityDomain || entityDomain->getNodeType() == AST::None) &&
                portDomain) {
              portEntityDecl->setPropertyValue("domain", portDomain);
            }
            if (ASTQuery::isRated(portEntityDecl, m_ctx.tree) &&
                (!portEntityDecl->getPropertyValue("rate") ||
                 portEntityDecl->getPropertyValue("rate")->getNodeType() ==
                     AST::None)) {
              portEntityDecl->setPropertyValue("rate", portRate);
            }
          }
        }
      } else if (entityPortValue->getNodeType() == AST::None) {
        std::string defaultName = portDecl->getName() + "_Entity";
        auto nameNode =
            std::make_shared<EntityNode>(defaultName, __FILE__, __LINE__);
        portDecl->replacePropertyValue("entity", nameNode);
        auto newSignal = ASTQuery::findDeclarationByName(
            defaultName, ScopeStack(), internalEntities);
        if (!newSignal) {
          newSignal = createSignalDeclaration(defaultName, 1, subScope,
                                              m_ctx.tree);
          internalEntities->addChild(newSignal);
          ASTFunctions::fillDefaultPropertiesForNode(newSignal,
                                                     m_ctx.tree->getChildren());
          ASTNode entityDomain = newSignal->getDomain();
          if (!entityDomain || entityDomain->getNodeType() == AST::None) {
            if (portDomain) {
              newSignal->setPropertyValue("domain", portDomain);
            }
            if (newSignal->getPropertyValue("rate") &&
                newSignal->getPropertyValue("rate")->getNodeType() ==
                    AST::None) {
              newSignal->setPropertyValue("rate", portRate);
            }
          }
        }
      }
    }
  } else if (ports && ports->getNodeType() != AST::None) {
    std::cerr << "ERROR! ports property must be a list or None!" << std::endl;
  }

  for (const ASTNode &child : internalEntities->getChildren()) {
    declareInternalEntitiesForNode(child, subScope);
  }
}

void AstNormalizer::resolveConstants() {
  auto allOverrides = m_ctx.systemConfig.overrides.find("all");
  if (allOverrides != m_ctx.systemConfig.overrides.end()) {
    for (const auto &[overrideName, overrideVal] : allOverrides->second) {
      auto decl =
          ASTQuery::findDeclarationByName(overrideName, {}, m_ctx.tree);
      if (!decl) {
        std::cerr << "WARNING: Configuration override not found: "
                  << overrideName << std::endl;
        continue;
      }
      if (!ASTQuery::isConstant(decl, {}, m_ctx.tree)) {
        std::cerr << "WARNING: Ignoring configuration override '"
                  << overrideName << "'. Not constant." << std::endl;
        continue;
      }
      if (std::holds_alternative<std::string>(overrideVal)) {
        decl->replacePropertyValue(
            "value", std::make_shared<ValueNode>(
                         std::get<std::string>(overrideVal), __FILE__, __LINE__));
      } else if (std::holds_alternative<int64_t>(overrideVal)) {
        decl->replacePropertyValue(
            "value", std::make_shared<ValueNode>(
                         std::get<int64_t>(overrideVal), __FILE__, __LINE__));
      }
    }
  }
  for (const ASTNode &node : m_ctx.tree->getChildren()) {
    ASTFunctions::resolveConstantsInNode(node, {}, m_ctx.tree);
  }
}

void AstNormalizer::resolveSelfPropertyAccesses() {
  for (const auto &node : m_ctx.tree->getChildren()) {
    resolveSelfPropertyAccessesForNode(node);
  }
}

void AstNormalizer::resolveSelfPropertyAccessesForNode(const ASTNode &node) {
  if (!node || (node->getNodeType() != AST::Declaration &&
                node->getNodeType() != AST::ArrayDeclaration)) {
    return;
  }
  auto decl = std::static_pointer_cast<DeclarationNode>(node);
  for (const auto &prop : decl->getProperties()) {
    auto value = prop->getValue();
    if (value && value->getNodeType() == AST::MemberAccess) {
      auto memberAccess = std::static_pointer_cast<MemberAccessNode>(value);
      if (memberAccess->getEntity().empty()) {
        if (auto replacement =
                decl->getPropertyValue(memberAccess->getPropertyName())) {
          decl->setPropertyValue(prop->getName(), replacement->deepCopy());
        }
      }
    }
  }
}

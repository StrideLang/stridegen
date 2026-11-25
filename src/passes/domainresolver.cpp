/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#include "domainresolver.hpp"

#include "stride/parser/arraynode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/functionnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astquery.h"

#include <algorithm>
#include <cassert>
#include <functional>
#include <iostream>
#include <set>

using namespace strd;

void DomainResolver::resolveDomainAliases() {
  if (!m_ctx.system) {
    return;
  }
  const auto &importTrees = m_ctx.system->getImportTrees();
  auto findDecl =
      [&](const std::string &name) -> std::shared_ptr<DeclarationNode> {
    if (auto decl = ASTQuery::findDeclarationByName(name, {}, m_ctx.tree, {})) {
      return decl;
    }
    for (const auto &objects : importTrees) {
      if (auto decl = ASTQuery::findDeclarationByName(
              name, {{nullptr, objects.second}}, nullptr, {})) {
        return decl;
      }
    }
    return nullptr;
  };

  auto aliasedDomain =
      [&](const std::string &name) -> std::shared_ptr<DeclarationNode> {
    auto decl = findDecl(name);
    if (!decl || decl->getEntityType() != "alias") {
      return nullptr;
    }
    std::set<std::string> seen{name};
    while (decl && decl->getEntityType() == "alias") {
      auto target = decl->getPropertyValue("entity");
      if (!target || target->getNodeType() != AST::Entity) {
        return nullptr;
      }
      std::string targetName = ASTQuery::getNodeName(target);
      if (!seen.insert(targetName).second) {
        return nullptr;
      }
      decl = findDecl(targetName);
    }
    if (decl && decl->getEntityType() == "_domainDefinition") {
      return decl;
    }
    return nullptr;
  };

  std::vector<std::shared_ptr<DeclarationNode>> domainsToAdd;
  auto replaceIfAlias = [&](const std::shared_ptr<PropertyNode> &prop,
                            auto &&replace) {
    if (!prop ||
        (prop->getName() != "domain" && prop->getName() != "parentDomain")) {
      return;
    }
    auto value = prop->getValue();
    if (!value || value->getNodeType() != AST::Entity ||
        !value->getNamespaceList().empty()) {
      return;
    }
    auto domain = aliasedDomain(ASTQuery::getNodeName(value));
    if (!domain) {
      return;
    }
    replace(prop->getName(),
            std::make_shared<EntityNode>(domain->getName(),
                                         value->getFilename().c_str(),
                                         value->getLine()));
    if (!ASTQuery::findDeclarationByName(domain->getName(), {}, m_ctx.tree,
                                         {}) &&
        std::find(domainsToAdd.begin(), domainsToAdd.end(), domain) ==
            domainsToAdd.end()) {
      domainsToAdd.push_back(domain);
    }
  };

  std::set<const void *> visited;
  std::function<void(const ASTNode &)> walk = [&](const ASTNode &node) {
    if (!node || !visited.insert(node.get()).second) {
      return;
    }
    if (node->getNodeType() == AST::Declaration ||
        node->getNodeType() == AST::ArrayDeclaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      const auto properties = decl->getProperties();
      for (const auto &prop : properties) {
        replaceIfAlias(prop, [&](const std::string &name, const ASTNode &v) {
          decl->replacePropertyValue(name, v);
        });
        if (prop) {
          walk(prop->getValue());
        }
      }
    } else if (node->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(node);
      for (const auto &prop : func->getProperties()) {
        replaceIfAlias(prop, [&](const std::string &name, const ASTNode &v) {
          func->replacePropertyValue(name, v);
        });
        if (prop) {
          walk(prop->getValue());
        }
      }
    } else if (node->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(node);
      walk(stream->getLeft());
      walk(stream->getRight());
      return;
    }
    const auto children = node->getChildren();
    for (const auto &child : children) {
      walk(child);
    }
  };
  walk(m_ctx.tree);
  for (const auto &domain : domainsToAdd) {
    m_ctx.tree->addChild(domain);
  }
}

void DomainResolver::processDomains() {
  std::vector<ASTNode> children = m_ctx.tree->getChildren();
  ScopeStack scopeStack;
  ASTNode platformDomain =
      m_ctx.system ? m_ctx.system->getPlatformDomain() : nullptr;

  // Assign shared globals domain to loop/reaction internals before general
  // propagation
  for (const auto &node : children) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() != "loop" &&
        decl->getEntityType() != "reaction") {
      continue;
    }
    std::vector<ASTNode> streams;
    for (const auto &ent :
         ASTQuery::getContainedStreamEntities(decl, {}, m_ctx.tree)) {
      streams.insert(streams.end(), ent.streams.begin(), ent.streams.end());
    }
    if (auto entities = decl->getPropertyValue("entities")) {
      for (const auto &b : entities->getChildren()) {
        if (b && b->getNodeType() == AST::Declaration) {
          auto bDecl = std::static_pointer_cast<DeclarationNode>(b);
          if (bDecl->getEntityType() == "iterator" ||
              bDecl->getEntityType() == "sequence") {
            for (const auto &prop : bDecl->getProperties()) {
              if (prop && prop->getValue()) {
                streams.push_back(prop->getValue());
              }
            }
          }
        }
      }
    }
    applyDomainOfUsedGlobals(decl, streams);
  }

  for (auto rit = children.rbegin(); rit != children.rend(); ++rit) {
    propagateDomainsForNode(*rit, scopeStack, platformDomain);
  }

  std::vector<ASTNode> newTree;
  for (const ASTNode &node : m_ctx.tree->getChildren()) {
    if (node && node->getNodeType() == AST::Stream) {
      std::vector<ASTNode> sliced = sliceStreamByDomain(
          std::static_pointer_cast<StreamNode>(node), ScopeStack());
      newTree.insert(newTree.end(), sliced.begin(), sliced.end());
    } else if (node && node->getNodeType() == AST::Declaration) {
      auto module = std::static_pointer_cast<DeclarationNode>(node);
      sliceDomainsInNode(module, ScopeStack());
      newTree.push_back(module);
    } else {
      newTree.push_back(node);
    }
  }
  m_ctx.tree->setChildren(newTree);
}

void DomainResolver::propagateDomainsForNode(const ASTNode &node,
                                             ScopeStack scopeStack,
                                             const ASTNode &defaultDomain) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::Stream) {
    resolveDomainsForStream(std::static_pointer_cast<StreamNode>(node),
                            scopeStack, defaultDomain);
  } else if (node->getNodeType() == AST::Declaration) {
    auto module = std::static_pointer_cast<DeclarationNode>(node);
    auto streamEntities =
        ASTQuery::getContainedStreamEntities(module, scopeStack, m_ctx.tree);
    if (!streamEntities.empty() || module->getPropertyValue("entities")) {
      std::vector<ASTNode> streamsNode;
      for (const auto &b : streamEntities) {
        streamsNode.insert(streamsNode.end(), b.streams.begin(),
                           b.streams.end());
      }
      std::vector<ASTNode> moduleEntities = ASTQuery::getModuleEntities(module);
      ASTNode ports = module->getPropertyValue("ports");
      if (ports && ports->getNodeType() == AST::List) {
        const auto &portList = ports->getChildren();
        moduleEntities.insert(moduleEntities.begin(), portList.begin(),
                              portList.end());
      }

      scopeStack.push_back({node, moduleEntities});
      ASTNode contextDomainNode = getModuleContextDomain(module);

      for (auto streamIt = streamsNode.rbegin(); streamIt != streamsNode.rend();
           ++streamIt) {
        if (*streamIt && (*streamIt)->getNodeType() == AST::Stream) {
          resolveDomainsForStream(
              std::static_pointer_cast<StreamNode>(*streamIt), scopeStack,
              contextDomainNode);
        }
      }
      for (const auto &streamNode : streamsNode) {
        if (streamNode && streamNode->getNodeType() == AST::Stream) {
          resolveDomainsForStream(
              std::static_pointer_cast<StreamNode>(streamNode), scopeStack,
              contextDomainNode);
        }
      }
      for (const auto &declNode : moduleEntities) {
        propagateDomainsForNode(declNode, scopeStack, defaultDomain);
      }
    }
  }
}

void DomainResolver::resolveDomainsForStream(std::shared_ptr<StreamNode> stream,
                                             const ScopeStack &scopeStack,
                                             const ASTNode &contextDomainNode) {
  if (!stream) {
    return;
  }
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  std::vector<ASTNode> domainStack;
  ASTNode previousDomain =
      CodeAnalysis::getNodeDomain(left, scopeStack, m_ctx.tree);
  std::string previousDomainId =
      ASTQuery::getDomainIdentifier(previousDomain, scopeStack, m_ctx.tree);

  if (!previousDomain || previousDomain->getNodeType() == AST::None) {
    previousDomain = contextDomainNode;
  }
  ASTNode domainNode;
  while (right) {
    domainNode = processDomainsForNode(left, scopeStack, domainStack);
    std::string domainId =
        domainNode
            ? ASTQuery::getDomainIdentifier(domainNode, scopeStack, m_ctx.tree)
            : std::string();

    if (left == right &&
        (!domainNode || domainNode->getNodeType() == AST::None)) {
      if (left->getNodeType() == AST::Function && previousDomain &&
          previousDomain->getNodeType() != AST::None) {
        domainNode = previousDomain;
      } else if (!(left->getNodeType() == AST::Entity ||
                   left->getNodeType() == AST::Array) ||
                 !previousDomain ||
                 previousDomain->getNodeType() == AST::None) {
        previousDomain = contextDomainNode;
      }
    }

    if (domainNode && domainNode->getNodeType() != AST::None &&
        previousDomainId != domainId) {
      setDomainForStack(domainStack, domainNode, scopeStack);
      domainStack.clear();
    }
    if (left->getNodeType() == AST::Expression ||
        left->getNodeType() == AST::List) {
      ASTNode samplingDomain = CodeAnalysis::getNodeDomain(
          stream->getRight(), scopeStack, m_ctx.tree);
      if (samplingDomain) {
        std::function<void(const ASTNode &, const ASTNode &)> tagSampling =
            [&](const ASTNode &n, const ASTNode &sDom) {
              for (const auto &child : n->getChildren()) {
                if (!child) {
                  continue;
                }
                if (child->getNodeType() == AST::Expression ||
                    child->getNodeType() == AST::List) {
                  child->setCompilerProperty("samplingDomain", sDom);
                  tagSampling(child, sDom);
                } else if (child->getNodeType() == AST::Entity ||
                           child->getNodeType() == AST::Array ||
                           child->getNodeType() == AST::MemberAccess) {
                  auto dom = CodeAnalysis::getNodeDomain(child, scopeStack,
                                                         m_ctx.tree);
                  if (!dom || dom->getNodeType() == AST::None) {
                    auto instance =
                        ASTQuery::getInstance(child, scopeStack, m_ctx.tree);
                    if (instance &&
                        (instance->getNodeType() == AST::Declaration ||
                         instance->getNodeType() == AST::ArrayDeclaration)) {
                      auto decl =
                          std::static_pointer_cast<DeclarationNode>(instance);
                      auto declType = ASTQuery::findTypeDeclaration(
                          decl, scopeStack, m_ctx.tree);
                      bool inheritsDomainType = false;
                      if (declType) {
                        for (const auto &inherits :
                             ASTQuery::getInheritedTypes(declType, scopeStack,
                                                         m_ctx.tree)) {
                          auto typeName =
                              inherits->getPropertyValue("typeName");
                          if (typeName && typeName->getNodeType() == AST::String &&
                              std::static_pointer_cast<ValueNode>(typeName)
                                      ->getStringValue() == "domainMember") {
                            inheritsDomainType = true;
                            break;
                          }
                        }
                      }
                      if (inheritsDomainType) {
                        decl->setPropertyValue("domain", sDom);
                      }
                    } else if (instance &&
                               instance->getNodeType() == AST::MemberAccess) {
                      instance->setCompilerProperty("domain", sDom);
                    }
                  }
                }
              }
            };
        left->setCompilerProperty("samplingDomain", samplingDomain);
        tagSampling(left, samplingDomain);
      }
    } else if (left->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(left);
      auto decl = ASTQuery::findDeclarationForNode(left, scopeStack, m_ctx.tree);
      if (decl && (decl->getEntityType() == "module" ||
                   decl->getEntityType() == "reaction" ||
                   decl->getEntityType() == "loop")) {
        domainNode = processDomainsForNode(decl, scopeStack, domainStack);
        auto internalStreams = decl->getPropertyValue("streams");
        auto internalEntities = decl->getPropertyValue("entities");
        if (internalStreams && internalEntities) {
          auto internalScopeStack = scopeStack;
          internalScopeStack.push_back({func, internalEntities->getChildren()});
          for (const auto &internalStream : internalStreams->getChildren()) {
            if (internalStream && internalStream->getNodeType() == AST::Stream) {
              resolveDomainsForStream(
                  std::static_pointer_cast<StreamNode>(internalStream),
                  internalScopeStack, nullptr);
            }
          }
        }
      }
      for (const auto &property : func->getProperties()) {
        processDomainsForNode(property->getValue(), scopeStack, domainStack);
      }
    }

    if (left == right) {
      if (previousDomain && previousDomain->getNodeType() != AST::None) {
        setDomainForStack(domainStack, previousDomain, scopeStack);
        domainStack.clear();
      }
      right = left = nullptr;
    } else if (right->getNodeType() == AST::Stream) {
      stream = std::static_pointer_cast<StreamNode>(right);
      left = stream->getLeft();
      right = stream->getRight();
    } else {
      left = right;
    }
    previousDomain = domainNode;
    previousDomainId = domainId;
  }
}

ASTNode DomainResolver::processDomainsForNode(const ASTNode &node,
                                              const ScopeStack &scopeStack,
                                              std::vector<ASTNode> &domainStack) {
  ASTNode currentDomain;
  if (!node) {
    return currentDomain;
  }

  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    auto declaration =
        ASTQuery::findDeclarationForNode(node, scopeStack, m_ctx.tree);
    if (declaration) {
      ASTNode domain = declaration->getDomain();
      auto typeDeclaration =
          ASTQuery::findTypeDeclaration(declaration, scopeStack, m_ctx.tree);
      if (typeDeclaration &&
          typeDeclaration->getEntityType() == "platformModule") {
        domainStack.push_back(node);
      } else if (!ASTQuery::isConstant(declaration, scopeStack, m_ctx.tree)) {
        if (!domain || domain->getNodeType() == AST::None) {
          domainStack.push_back(declaration);
        } else if (domain->getNodeType() == AST::String ||
                   domain->getNodeType() == AST::Entity ||
                   domain->getNodeType() == AST::MemberAccess) {
          currentDomain = domain->deepCopy();
          currentDomain->setNamespaceList(node->getNamespaceList());
        } else {
          std::cerr << "unrecognized domain type" << std::endl;
        }
      }
      if (node->getNodeType() == AST::Array) {
        if (auto indexNode = std::static_pointer_cast<ArrayNode>(node)->index()) {
          for (const auto &indexElement : indexNode->getChildren()) {
            if (!indexElement) {
              continue;
            }
            std::vector<ASTNode> indexDomainStack;
            ASTNode idxDom =
                processDomainsForNode(indexElement, scopeStack, indexDomainStack);
            if (!indexDomainStack.empty() && idxDom &&
                idxDom->getNodeType() != AST::None) {
              setDomainForStack(indexDomainStack, idxDom, scopeStack);
            }
            if (indexElement->getNodeType() != AST::String &&
                indexElement->getNodeType() != AST::Int &&
                indexElement->getNodeType() != AST::Real &&
                indexElement->getNodeType() != AST::List &&
                indexElement->getNodeType() != AST::Expression &&
                indexElement->getNodeType() != AST::Range) {
              ASTNode elemDomain = CodeAnalysis::getNodeDomain(
                  indexElement, scopeStack, m_ctx.tree);
              if (elemDomain && elemDomain->getNodeType() != AST::None) {
                setDomainForNode(indexElement, elemDomain, scopeStack);
              }
            }
          }
        }
      }
      if (!scopeStack.empty()) {
        if (ASTQuery::findDeclarationForNode(node, {scopeStack.back()}, nullptr) &&
            declaration->getCompilerProperty("parentInstances") == nullptr) {
          auto parentList = std::make_shared<ListNode>(__FILE__, __LINE__);
          for (const auto &subScope : scopeStack) {
            std::string name =
                subScope.first ? ASTQuery::getNodeName(subScope.first) : "";
            parentList->addChild(
                std::make_shared<ValueNode>(name, __FILE__, __LINE__));
          }
          declaration->setCompilerProperty("parentInstances", parentList);
        }
      }
    }
  } else if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    ASTNode domain = CodeAnalysis::getNodeDomain(node, scopeStack, m_ctx.tree);
    if (domain && (domain->getNodeType() == AST::String ||
                   domain->getNodeType() == AST::Entity ||
                   domain->getNodeType() == AST::MemberAccess)) {
      currentDomain = domain;
    } else {
      domainStack.push_back(node);
    }
    for (const auto &member : func->getProperties()) {
      ASTNode propertyDomain =
          processDomainsForNode(member->getValue(), scopeStack, domainStack);
      if (!propertyDomain || propertyDomain->getNodeType() == AST::None) {
        domainStack.push_back(member->getValue());
      }
    }
  } else if (node->getNodeType() == AST::List) {
    std::vector<ASTNode> listDomainStack;
    for (const ASTNode &member : node->getChildren()) {
      ASTNode memberDomain =
          processDomainsForNode(member, scopeStack, domainStack);
      if (!currentDomain || currentDomain->getNodeType() == AST::None) {
        if (memberDomain &&
            (memberDomain->getNodeType() == AST::String ||
             memberDomain->getNodeType() == AST::Entity ||
             memberDomain->getNodeType() == AST::MemberAccess)) {
          currentDomain = memberDomain;
        } else {
          domainStack.push_back(node);
        }
      }
      if (!memberDomain || memberDomain->getNodeType() == AST::None) {
        listDomainStack.push_back(member);
      }
    }
    if (currentDomain && currentDomain->getNodeType() != AST::None) {
      setDomainForStack(listDomainStack, currentDomain, scopeStack);
    } else {
      domainStack.insert(domainStack.end(), listDomainStack.begin(),
                         listDomainStack.end());
    }
  } else if (node->getNodeType() == AST::Expression) {
    ASTNode samplingDomain;
    for (const ASTNode &member : node->getChildren()) {
      ASTNode memberDomain =
          processDomainsForNode(member, scopeStack, domainStack);
      if (!memberDomain || memberDomain->getNodeType() == AST::None) {
        domainStack.push_back(member);
      } else if (!samplingDomain) {
        samplingDomain = memberDomain;
        currentDomain = memberDomain;
      }
    }
    if (samplingDomain) {
      node->setCompilerProperty("samplingDomain", samplingDomain);
    }
    if (!scopeStack.empty() &&
        node->getCompilerProperty("parentInstances") == nullptr) {
      auto parentList = std::make_shared<ListNode>(__FILE__, __LINE__);
      for (const auto &subScope : scopeStack) {
        std::string name =
            subScope.first ? ASTQuery::getNodeName(subScope.first) : "";
        parentList->addChild(
            std::make_shared<ValueNode>(name, __FILE__, __LINE__));
      }
      node->setCompilerProperty("parentInstances", parentList);
    }
  } else if (node->getNodeType() == AST::Int ||
             node->getNodeType() == AST::Real ||
             node->getNodeType() == AST::String ||
             node->getNodeType() == AST::Switch ||
             node->getNodeType() == AST::Declaration) {
    domainStack.push_back(node);
  }
  return currentDomain;
}

void DomainResolver::setDomainForStack(const std::vector<ASTNode> &domainStack,
                                       const ASTNode &domainName,
                                       const ScopeStack &scopeStack) {
  for (const ASTNode &relatedNode : domainStack) {
    setDomainForNode(relatedNode, domainName, scopeStack);
  }
}

void DomainResolver::setDomainForNode(const ASTNode &node,
                                      const ASTNode &domain,
                                      const ScopeStack &scopeStack,
                                      bool force) {
  if (!node) {
    return;
  }
  ASTNode existingDomain =
      CodeAnalysis::getNodeDomain(node, scopeStack, m_ctx.tree);
  if (existingDomain && existingDomain->getNodeType() != AST::None && !force) {
    return;
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (!ASTQuery::isConstant(decl, scopeStack, m_ctx.tree)) {
      decl->replacePropertyValue("domain", domain);
    }
  } else if (node->getNodeType() == AST::Entity ||
             node->getNodeType() == AST::Array) {
    auto declaration =
        ASTQuery::findDeclarationForNode(node, scopeStack, m_ctx.tree);
    if (declaration &&
        !ASTQuery::isConstant(declaration, scopeStack, m_ctx.tree)) {
      auto typeDeclaration =
          ASTQuery::findTypeDeclaration(declaration, scopeStack, m_ctx.tree);
      if (typeDeclaration &&
          typeDeclaration->getEntityType() == "platformModule") {
        node->setCompilerProperty("domain", domain);
      } else {
        declaration->replacePropertyValue("domain", domain);
      }
    }
  } else if (node->getNodeType() == AST::Function ||
             node->getNodeType() == AST::MemberAccess) {
    node->setCompilerProperty("domain", domain);
  } else if (node->getNodeType() == AST::Real ||
             node->getNodeType() == AST::Int ||
             node->getNodeType() == AST::String ||
             node->getNodeType() == AST::Switch) {
    std::static_pointer_cast<ValueNode>(node)->setDomain(domain);
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    for (const ASTNode &member : node->getChildren()) {
      setDomainForNode(member, domain, scopeStack, force);
    }
  }
}

std::shared_ptr<StreamNode>
DomainResolver::makeStreamFromStack(std::vector<ASTNode> &stack) {
  assert(stack.size() > 1);
  ASTNode lastNode = stack.back();
  stack.pop_back();
  auto newStream = std::make_shared<StreamNode>(
      stack.back(), lastNode, lastNode->getFilename().c_str(),
      lastNode->getLine());
  stack.pop_back();
  while (!stack.empty()) {
    lastNode = stack.back();
    newStream = std::make_shared<StreamNode>(lastNode, newStream,
                                             lastNode->getFilename().c_str(),
                                             lastNode->getLine());
    stack.pop_back();
  }
  return newStream;
}

std::vector<ASTNode>
DomainResolver::sliceStreamByDomain(const std::shared_ptr<StreamNode> &stream,
                                    const ScopeStack &scopeStack) {
  std::vector<ASTNode> streams;
  std::vector<ASTNode> stack;
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();

  while (left) {
    if (left->getNodeType() == AST::Int || left->getNodeType() == AST::Real ||
        left->getNodeType() == AST::Switch) {
      stack.push_back(left);
      if (right && right->getNodeType() == AST::Stream) {
        auto *subStream = static_cast<StreamNode *>(right.get());
        left = subStream->getLeft();
        right = subStream->getRight();
      } else {
        stack.push_back(right);
        streams.push_back(makeStreamFromStack(stack));
        break;
      }
    }
    stack.push_back(left);
    if (right && right->getNodeType() == AST::Stream) {
      auto *subStream = static_cast<StreamNode *>(right.get());
      left = subStream->getLeft();
      right = subStream->getRight();
    } else {
      stack.push_back(right);
      if (!stack.empty()) {
        streams.push_back(makeStreamFromStack(stack));
      }
      left = right = nullptr;
    }
  }
  return streams;
}

void DomainResolver::sliceDomainsInNode(
    const std::shared_ptr<DeclarationNode> &module, ScopeStack scopeStack) {
  if (!module ||
      !ASTQuery::isCodeGenerator(
          ASTQuery::findTypeDeclaration(module, scopeStack, m_ctx.tree),
          scopeStack, m_ctx.tree)) {
    return;
  }
  ASTNode streamsNode = module->getPropertyValue("streams");
  ASTNode entitiesNode = module->getPropertyValue("entities");
  if (!entitiesNode) {
    return;
  }
  if (entitiesNode->getNodeType() == AST::None) {
    module->replacePropertyValue(
        "entities", std::make_shared<ListNode>(nullptr, __FILE__, __LINE__));
    entitiesNode = module->getPropertyValue("entities");
  }

  auto newStreamsList = std::make_shared<ListNode>(nullptr, __FILE__, __LINE__);
  ScopeStack subScope = scopeStack;
  subScope.push_back(
      {module, ASTQuery::getPortsInSubtree(module, scopeStack, m_ctx.tree)});

  auto processStream = [&](const ASTNode &streamNode) {
    if (!streamNode || streamNode->getNodeType() != AST::Stream) {
      return;
    }
    std::vector<ASTNode> sliced = sliceStreamByDomain(
        std::static_pointer_cast<StreamNode>(streamNode), subScope);
    for (const ASTNode &item : sliced) {
      if (item->getNodeType() == AST::Stream) {
        newStreamsList->addChild(item);
      } else if (item->getNodeType() == AST::Declaration ||
                 item->getNodeType() == AST::ArrayDeclaration) {
        entitiesNode->addChild(item);
      }
    }
  };

  if (streamsNode) {
    if (streamsNode->getNodeType() == AST::List) {
      for (const ASTNode &s : streamsNode->getChildren()) {
        processStream(s);
      }
    } else if (streamsNode->getNodeType() == AST::Stream) {
      processStream(streamsNode);
    }
    module->replacePropertyValue("streams", newStreamsList);
  }

  scopeStack.push_back({module, entitiesNode->getChildren()});
  for (const auto &declNode : entitiesNode->getChildren()) {
    if (declNode && declNode->getNodeType() == AST::Declaration) {
      sliceDomainsInNode(std::static_pointer_cast<DeclarationNode>(declNode),
                         scopeStack);
    }
  }
}

ASTNode
DomainResolver::getDomainOfUsedGlobals(const std::vector<ASTNode> &streams) {
  ASTNode found;
  std::string foundId;
  bool conflict = false;
  std::function<void(const ASTNode &)> visit = [&](const ASTNode &node) {
    if (!node || conflict) {
      return;
    }
    if (node->getNodeType() == AST::Entity ||
        node->getNodeType() == AST::Array) {
      auto decl = ASTQuery::findDeclarationForNode(node, {}, m_ctx.tree);
      auto domain = decl ? decl->getDomain() : nullptr;
      if (domain && domain->getNodeType() != AST::None) {
        std::string id =
            ASTQuery::getDomainIdentifier(domain, {}, m_ctx.tree);
        if (!found) {
          found = domain;
          foundId = id;
        } else if (id != foundId) {
          conflict = true;
        }
      }
    }
    for (const auto &child : node->getChildren()) {
      visit(child);
    }
  };
  for (const auto &stream : streams) {
    visit(stream);
  }
  return (found && !conflict) ? found->deepCopy() : nullptr;
}

ASTNode DomainResolver::applyDomainOfUsedGlobals(
    const std::shared_ptr<DeclarationNode> &module,
    const std::vector<ASTNode> &streams) {
  auto globalsDomain = getDomainOfUsedGlobals(streams);
  if (!globalsDomain) {
    return nullptr;
  }
  std::set<std::string> portEntities;
  if (auto ports = module->getPropertyValue("ports");
      ports && ports->getNodeType() == AST::List) {
    for (const auto &port : ports->getChildren()) {
      if (port && port->getNodeType() == AST::Declaration) {
        if (auto portEntity =
                std::static_pointer_cast<DeclarationNode>(port)
                    ->getPropertyValue("entity")) {
          portEntities.insert(ASTQuery::getNodeName(portEntity));
        }
      }
    }
  }
  if (auto entities = module->getPropertyValue("entities");
      entities && entities->getNodeType() == AST::List) {
    for (const auto &entity : entities->getChildren()) {
      if (!entity || (entity->getNodeType() != AST::Declaration &&
                      entity->getNodeType() != AST::ArrayDeclaration)) {
        continue;
      }
      auto entityDecl = std::static_pointer_cast<DeclarationNode>(entity);
      const std::string &et = entityDecl->getEntityType();
      if ((et != "signal" && et != "switch" && et != "iterator" &&
           et != "sequence") ||
          portEntities.count(entityDecl->getName())) {
        continue;
      }
      auto entityDomain = entityDecl->getDomain();
      if (!entityDomain || entityDomain->getNodeType() == AST::None ||
          entityDomain->getNodeType() == AST::MemberAccess) {
        entityDecl->replacePropertyValue("domain", globalsDomain->deepCopy());
      }
    }
  }
  return globalsDomain;
}

ASTNode DomainResolver::getModuleContextDomain(
    const std::shared_ptr<DeclarationNode> &moduleDecl) {
  if (!moduleDecl) {
    return nullptr;
  }
  auto entities = moduleDecl->getPropertyValue("entities");
  if (!entities) {
    return nullptr;
  }
  ScopeStack internalScope = {{moduleDecl, entities->getChildren()}};
  if (auto outputPortEntity = ASTQuery::getModuleMainOutputPort(moduleDecl)) {
    ASTNode dom = CodeAnalysis::getNodeDomain(
        outputPortEntity->getPropertyValue("entity"), internalScope,
        m_ctx.tree);
    if (dom && dom->getNodeType() != AST::None) {
      return dom;
    }
  }
  if (auto inputPortEntity = ASTQuery::getModuleMainInputPort(moduleDecl)) {
    return CodeAnalysis::getNodeDomain(
        inputPortEntity->getPropertyValue("entity"), internalScope, m_ctx.tree);
  }
  return nullptr;
}

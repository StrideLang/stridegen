/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#include "connectionanddependencyanalyzer.hpp"

#include "stride/parser/arraynode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/memberaccessnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <queue>
#include <set>

using namespace strd;

namespace {

struct PersistenceState {
  std::set<std::shared_ptr<DeclarationNode>> readSet;
  std::set<std::shared_ptr<DeclarationNode>> dynamicReadSet;
  std::map<std::shared_ptr<DeclarationNode>, std::set<int64_t>> readIndices;
  std::set<std::shared_ptr<DeclarationNode>> wholeWritten;
  std::set<std::shared_ptr<DeclarationNode>> condOrPartialWritten;
  std::map<std::shared_ptr<DeclarationNode>, std::set<int64_t>>
      constWrittenIndices;
};

std::shared_ptr<DeclarationNode>
findPersistentCandidateDecl(const ASTNode &node, const ScopeStack &scope,
                            const ASTNode &tree) {
  for (int i = static_cast<int>(scope.size()) - 1; i >= 0; --i) {
    auto found = ASTQuery::findDeclarationForNode(node, {scope[i]}, nullptr);
    if (!found) {
      continue;
    }
    ASTNode owner = scope[i].first;
    if (owner && (owner->getNodeType() == AST::Declaration ||
                  owner->getNodeType() == AST::ArrayDeclaration)) {
      auto ownerDecl = std::static_pointer_cast<DeclarationNode>(owner);
      auto typeDecl = ASTQuery::findTypeDeclaration(ownerDecl, scope, tree);
      if (typeDecl && ASTQuery::isStatelessGenerator(typeDecl, scope, tree)) {
        return nullptr;
      }
    }
    return found;
  }
  return ASTQuery::findDeclarationForNode(node, {}, tree);
}

void resolvePersistenceRecursive(const ASTNode &streamsList, ScopeStack &scope,
                                 const ASTNode &tree, const ASTNode &rootTree,
                                 PersistenceState &state,
                                 bool isUnconditional) {
  if (!streamsList) {
    return;
  }

  auto markPersistent = [](const std::shared_ptr<DeclarationNode> &decl) {
    decl->setCompilerProperty(
        "persistent", std::make_shared<ValueNode>(true, __FILE__, __LINE__));
  };

  auto recordRead = [&](const std::shared_ptr<DeclarationNode> &decl) {
    if (!decl) {
      return;
    }
    if (state.condOrPartialWritten.count(decl) &&
        !state.wholeWritten.count(decl)) {
      markPersistent(decl);
    }
    state.readSet.insert(decl);
    state.dynamicReadSet.insert(decl);
  };

  auto tryEvalConstIndex = [&](const std::shared_ptr<ArrayNode> &arrNode,
                               int64_t &outIdx) -> bool {
    if (!arrNode || !arrNode->index() ||
        arrNode->index()->getChildren().size() != 1) {
      return false;
    }
    std::vector<LangError> evalErrors;
    outIdx = ASTFunctions::evaluateConstInteger(
        arrNode->index()->getChildren()[0], scope, tree, &evalErrors);
    return evalErrors.empty();
  };

  auto recordArrayRead = [&](const std::shared_ptr<DeclarationNode> &decl,
                             bool hasConstIdx, int64_t constIdx) {
    if (!decl) {
      return;
    }
    if (!state.wholeWritten.count(decl)) {
      if (state.condOrPartialWritten.count(decl)) {
        markPersistent(decl);
      } else if (hasConstIdx) {
        state.readIndices[decl].insert(constIdx);
      } else {
        auto it = state.constWrittenIndices.find(decl);
        if (it != state.constWrittenIndices.end() && !it->second.empty()) {
          int declSize =
              ASTQuery::getEntityDeclaredSize(decl, scope, tree, nullptr);
          if (declSize <= 0 || static_cast<int>(it->second.size()) < declSize) {
            markPersistent(decl);
          }
        }
        state.dynamicReadSet.insert(decl);
      }
    }
    state.readSet.insert(decl);
  };

  auto recordWrite = [&](const std::shared_ptr<DeclarationNode> &decl,
                         bool isWhole, bool hasConstIdx = false,
                         int64_t constIdx = 0) {
    if (!decl || state.wholeWritten.count(decl)) {
      return;
    }
    if (isWhole) {
      if (state.readSet.count(decl)) {
        markPersistent(decl);
      }
      if (isUnconditional) {
        state.wholeWritten.insert(decl);
      } else {
        state.condOrPartialWritten.insert(decl);
      }
    } else if (isUnconditional && hasConstIdx) {
      if (state.dynamicReadSet.count(decl) ||
          (state.readIndices.count(decl) &&
           state.readIndices[decl].count(constIdx))) {
        markPersistent(decl);
      }
      state.constWrittenIndices[decl].insert(constIdx);
    } else {
      if (state.readSet.count(decl)) {
        markPersistent(decl);
      }
      state.condOrPartialWritten.insert(decl);
    }
  };

  std::function<void(const ASTNode &)> collectReads = [&](const ASTNode &n) {
    if (!n) {
      return;
    }
    AST::Token nt = n->getNodeType();
    if (nt == AST::List || nt == AST::Expression || nt == AST::Range ||
        nt == AST::GeneratorRange) {
      for (const auto &child : n->getChildren()) {
        collectReads(child);
      }
    } else if (nt == AST::Array) {
      auto arrNode = std::static_pointer_cast<ArrayNode>(n);
      collectReads(arrNode->index());
      int64_t constIdx = 0;
      bool hasConstIdx = tryEvalConstIndex(arrNode, constIdx);
      recordArrayRead(findPersistentCandidateDecl(n, scope, tree), hasConstIdx,
                      constIdx);
    } else if (nt == AST::Function) {
      auto funcNode = std::static_pointer_cast<FunctionNode>(n);
      for (const auto &prop : funcNode->getProperties()) {
        if (prop) {
          collectReads(prop->getValue());
        }
      }
    } else if (!ASTQuery::getNodeName(n).empty()) {
      recordRead(findPersistentCandidateDecl(n, scope, tree));
    }
  };

  std::function<void(const ASTNode &)> collectWrites = [&](const ASTNode &n) {
    if (!n) {
      return;
    }
    AST::Token nt = n->getNodeType();
    if (nt == AST::List) {
      for (const auto &child : n->getChildren()) {
        collectWrites(child);
      }
    } else if (nt == AST::Array) {
      auto arrNode = std::static_pointer_cast<ArrayNode>(n);
      collectReads(arrNode->index());
      int64_t constIdx = 0;
      bool hasConstIdx = tryEvalConstIndex(arrNode, constIdx);
      recordWrite(findPersistentCandidateDecl(n, scope, tree),
                  /*isWhole=*/false, hasConstIdx, constIdx);
    } else if (nt != AST::Function && nt != AST::Expression &&
               !ASTQuery::getNodeName(n).empty()) {
      recordWrite(findPersistentCandidateDecl(n, scope, tree),
                  /*isWhole=*/true);
    }
  };

  for (const ASTNode &stream : streamsList->getChildren()) {
    if (!stream || stream->getNodeType() != AST::Stream) {
      continue;
    }
    bool streamStart = true;
    StreamNodeIterator streamIt(std::static_pointer_cast<StreamNode>(stream));
    while (auto node = streamIt.next()) {
      if (node->getNodeType() == AST::Function) {
        collectReads(node);
        auto funcDecl = ASTQuery::findDeclarationForNode(node, scope, rootTree);
        if (funcDecl) {
          auto funcDeclType = ASTQuery::findTypeDeclarationByName(
              funcDecl->getEntityType(), scope, rootTree);
          if (funcDeclType &&
              ASTQuery::isStatelessGenerator(funcDeclType, scope, rootTree)) {
            auto entitiesList = funcDecl->getPropertyValue("entities");
            auto nestedStreams = funcDecl->getPropertyValue("streams");
            if (entitiesList) {
              scope.push_back({funcDecl, entitiesList->getChildren()});
            }
            resolvePersistenceRecursive(nestedStreams, scope, tree, rootTree,
                                        state, /*isUnconditional=*/false);
            if (entitiesList) {
              scope.pop_back();
            }
          }
        } else {
          std::cerr << "ERROR: Can't find declaration for: " << node->toText()
                    << std::endl;
        }
      } else {
        if (!streamStart) {
          collectWrites(node);
        }
        if (streamIt.hasNext()) {
          collectReads(node);
        }
      }
      streamStart = false;
    }
  }
}

std::shared_ptr<ListNode> ensureListProperty(const ASTNode &node,
                                             const std::string &propName) {
  auto list = std::static_pointer_cast<ListNode>(
      node->getCompilerProperty(propName));
  if (!list) {
    list = std::make_shared<ListNode>(nullptr, node->getFilename().c_str(),
                                      node->getLine());
    node->setCompilerProperty(propName, list);
  }
  return list;
}

} // namespace

void ConnectionAndDependencyAnalyzer::storeDeclarations() {
  for (const auto &node : m_ctx.tree->getChildren()) {
    storeDeclarationsForNode(node, {}, m_ctx.tree);
  }
}

void ConnectionAndDependencyAnalyzer::storeDeclarationsForNode(
    const ASTNode &node, ScopeStack scopeStack, const ASTNode &tree) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    auto decl = std::static_pointer_cast<DeclarationNode>(
        node->getCompilerProperty("declaration"));
    if (!decl) {
      decl = ASTQuery::findDeclarationForNode(node, scopeStack, tree);
      if (decl && decl->getEntityType() != "platformModule") {
        node->setCompilerProperty("declaration", decl);
      }
    }
    for (const auto &prop : func->getProperties()) {
      if (prop && prop->getValue()) {
        storeDeclarationsForNode(prop->getValue(), scopeStack, tree);
      }
    }
    std::vector<ASTNode> scopeEntities;
    if (decl) {
      if (auto entities = decl->getPropertyValue("entities")) {
        scopeEntities = entities->getChildren();
      }
    }
    scopeStack.push_back({node, scopeEntities});
  } else if (node->getNodeType() == AST::Entity ||
             node->getNodeType() == AST::Array) {
    if (!node->getCompilerProperty("declaration")) {
      auto decl = ASTQuery::findDeclarationForNode(node, scopeStack, tree);
      if (decl && decl->getEntityType() != "platformModule") {
        node->setCompilerProperty("declaration", decl);
      }
    }
  } else if (node->getNodeType() == AST::Expression ||
             node->getNodeType() == AST::List) {
    for (const auto &child : node->getChildren()) {
      storeDeclarationsForNode(child, scopeStack, tree);
    }
  } else if (node->getNodeType() == AST::Stream) {
    auto stream = std::static_pointer_cast<StreamNode>(node);
    ASTNode streamNode = stream->getLeft();
    while (streamNode) {
      storeDeclarationsForNode(streamNode, scopeStack, m_ctx.tree);
      if (!stream || !stream->getRight()) {
        streamNode = nullptr;
      } else if (stream->getRight()->getNodeType() == AST::Stream) {
        stream = std::static_pointer_cast<StreamNode>(stream->getRight());
        streamNode = stream->getLeft();
      } else {
        streamNode = stream->getRight();
        stream = nullptr;
      }
    }
  } else if (node->getNodeType() == AST::Declaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() == "module" ||
        decl->getEntityType() == "reaction" ||
        decl->getEntityType() == "loop") {
      auto internalStreams = decl->getPropertyValue("streams");
      auto internalEntities = decl->getPropertyValue("entities");
      std::vector<ASTNode> entitiesList;
      if (internalEntities) {
        if (internalEntities->getNodeType() == AST::List) {
          for (const auto &child : internalEntities->getChildren()) {
            storeDeclarationsForNode(
                child, {{node, internalEntities->getChildren()}}, m_ctx.tree);
            if (child && (child->getNodeType() == AST::Declaration ||
                          child->getNodeType() == AST::ArrayDeclaration)) {
              entitiesList.push_back(child);
            } else {
              std::cerr << "Unexpected node type in internal entities"
                        << std::endl;
            }
          }
        } else if (internalEntities->getNodeType() == AST::Declaration ||
                   internalEntities->getNodeType() == AST::ArrayDeclaration) {
          entitiesList.push_back(internalEntities);
        }
      }
      ScopeStack stack;
      if ((decl->getEntityType() == "reaction" ||
           decl->getEntityType() == "loop") &&
          !scopeStack.empty()) {
        stack.push_back(scopeStack.back());
      }
      stack.push_back({decl, entitiesList});
      if (internalStreams && internalStreams->getNodeType() == AST::List) {
        for (const auto &sNode : internalStreams->getChildren()) {
          storeDeclarationsForNode(sNode, stack, m_ctx.tree);
        }
      }
    } else if (decl->getEntityType() == "iterator" ||
               decl->getEntityType() == "sequence") {
      for (const auto &prop : decl->getProperties()) {
        if (prop && prop->getValue() && prop->getName() != "domain" &&
            prop->getName() != "type") {
          storeDeclarationsForNode(prop->getValue(), scopeStack, tree);
        }
      }
    }
  }
}

void ConnectionAndDependencyAnalyzer::analyzeConnections() {
  analyzeChildConnections(m_ctx.tree, {});

  std::vector<std::shared_ptr<DeclarationNode>> knownDomains;
  for (const ASTNode &node : m_ctx.tree->getChildren()) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() == "_domainDefinition" ||
        decl->getEntityType() == "gameDefinition") {
      knownDomains.push_back(decl);
      assert(!decl->getCompilerProperty("domainReads"));
      assert(!decl->getCompilerProperty("domainWrites"));
      decl->setCompilerProperty(
          "domainReads", std::make_shared<ListNode>(__FILE__, __LINE__));
      decl->setCompilerProperty(
          "domainWrites", std::make_shared<ListNode>(__FILE__, __LINE__));
    }
  }

  auto resolveDomainDecl =
      [&](const ASTNode &domain) -> std::shared_ptr<DeclarationNode> {
    if (!domain) {
      return nullptr;
    }
    if (domain->getNodeType() == AST::String) {
      const std::string domainName =
          std::static_pointer_cast<ValueNode>(domain)->getStringValue();
      for (const auto &knownDomain : knownDomains) {
        if (domainName == knownDomain->getName()) {
          return knownDomain;
        }
      }
      return nullptr;
    }
    if (domain->getNodeType() == AST::Entity) {
      return ASTQuery::findDeclarationForNode(domain, {}, m_ctx.tree);
    }
    return nullptr;
  };

  for (const ASTNode &node : m_ctx.tree->getChildren()) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    if (auto reads = node->getCompilerProperty("reads")) {
      for (const auto &domain : reads->getChildren()) {
        if (!domain) {
          continue;
        }
        if (domain->getNodeType() == AST::String ||
            domain->getNodeType() == AST::Entity) {
          if (auto domainDecl = resolveDomainDecl(domain)) {
            if (auto domainReads = domainDecl->getCompilerProperty("domainReads")) {
              domainReads->addChild(node);
            }
          }
        } else if (domain->getNodeType() == AST::None) {
          std::cerr << "Read domain is none. Read code will be generated in "
                       "declared domain."
                    << std::endl;
          auto domainNode = std::static_pointer_cast<DeclarationNode>(node)
                                ->getPropertyValue("domain");
          if (domainNode && domainNode->getNodeType() != AST::None) {
            auto domainId =
                ASTQuery::getDomainIdentifier(domainNode, {}, m_ctx.tree);
            if (auto domainDecl =
                    ASTQuery::findDomainDeclaration(domainId, m_ctx.tree)) {
              if (auto domainReads =
                      domainDecl->getCompilerProperty("domainReads")) {
                domainReads->addChild(node);
              }
            }
          }
        } else {
          assert(domain->getNodeType() == AST::MemberAccess);
        }
      }
    }

    if (auto writes = node->getCompilerProperty("writes")) {
      for (const auto &domain : writes->getChildren()) {
        if (!domain) {
          continue;
        }
        if (domain->getNodeType() == AST::String ||
            domain->getNodeType() == AST::Entity) {
          if (auto domainDecl = resolveDomainDecl(domain)) {
            if (auto domainWrites =
                    domainDecl->getCompilerProperty("domainWrites")) {
              domainWrites->addChild(node);
            } else {
              std::cerr << "ERROR: no domainWrites property for domain: "
                        << ASTQuery::getNodeName(domain) << std::endl;
            }
          }
        } else if (domain->getNodeType() != AST::MemberAccess &&
                   domain->getNodeType() != AST::None) {
          std::cerr << "Expected port property domain" << std::endl;
        }
      }
    }
  }
}

void ConnectionAndDependencyAnalyzer::analyzeChildConnections(
    const ASTNode &node, ScopeStack scopeStack) {
  if (!node) {
    return;
  }

  for (const ASTNode &object : node->getChildren()) {
    if (object && object->getNodeType() == AST::Stream) {
      checkStreamConnections(std::static_pointer_cast<StreamNode>(object),
                             scopeStack);
    }
  }

  for (const auto &child : node->getChildren()) {
    if (!child || (child->getNodeType() != AST::Declaration &&
                   child->getNodeType() != AST::ArrayDeclaration)) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(child);
    auto streamEntities =
        ASTQuery::getContainedStreamEntities(decl, scopeStack, m_ctx.tree);
    ASTNode entities = decl->getPropertyValue("entities");
    if (streamEntities.empty() && !entities) {
      continue;
    }
    scopeStack.push_back(
        {decl, entities ? entities->getChildren() : std::vector<ASTNode>{}});
    for (const auto &ent : streamEntities) {
      for (const auto &stream : ent.streams) {
        checkStreamConnections(stream, scopeStack);
      }
    }
    if (entities) {
      analyzeChildConnections(entities, scopeStack);
    }
    scopeStack.pop_back();
  }
}

void ConnectionAndDependencyAnalyzer::setInputEntityForFunction(
    const std::shared_ptr<FunctionNode> &func, const ASTNode &previous) {
  if (func && previous) {
    func->setCompilerProperty("inputEntity", previous);
  }
}

void ConnectionAndDependencyAnalyzer::setOutputEntityForFunction(
    const std::shared_ptr<FunctionNode> &func, const ASTNode &next) {
  if (func && next) {
    func->setCompilerProperty("outputEntity", next);
  }
}

void ConnectionAndDependencyAnalyzer::checkStreamConnections(
    const std::shared_ptr<StreamNode> &stream, ScopeStack scopeStack,
    ASTNode previous) {
  if (!stream) {
    return;
  }
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();

  markConnectionForNode(left, scopeStack, previous);

  if (left && left->getNodeType() == AST::Function) {
    setInputEntityForFunction(std::static_pointer_cast<FunctionNode>(left),
                              previous);
  } else if (left && left->getNodeType() == AST::Array) {
    auto decl = ASTQuery::getDeclaration(left);
    auto indexList = std::static_pointer_cast<ArrayNode>(left)->index();
    if (indexList) {
      if (decl && !ASTQuery::isConstant(decl, scopeStack, m_ctx.tree)) {
        for (const auto &idxNode : indexList->getChildren()) {
          markConnectionForNode(left, scopeStack, idxNode);
        }
      } else {
        for (const auto &idxNode : indexList->getChildren()) {
          markConnectionForNode(right, scopeStack, idxNode);
        }
      }
    }
  } else if (left && left->getNodeType() == AST::List && previous) {
    if (previous->getNodeType() == AST::List &&
        !previous->getChildren().empty()) {
      const auto &previousChildren = previous->getChildren();
      auto connection = previousChildren.begin();
      for (const auto &child : left->getChildren()) {
        if (connection == previousChildren.end()) {
          connection = previousChildren.begin();
        }
        if (child && child->getNodeType() == AST::Function) {
          setInputEntityForFunction(
              std::static_pointer_cast<FunctionNode>(child), *connection);
        }
        ++connection;
      }
    } else {
      for (const auto &child : left->getChildren()) {
        if (child && child->getNodeType() == AST::Function) {
          setInputEntityForFunction(
              std::static_pointer_cast<FunctionNode>(child), previous);
        }
      }
    }
  } else if (left && left->getNodeType() == AST::Declaration) {
    left->setCompilerProperty("inputEntity", previous);
    auto decl = std::static_pointer_cast<DeclarationNode>(left);
    auto entities = decl->getPropertyValue("entities");
    auto streams = decl->getPropertyValue("streams");
    scopeStack.push_back(
        {decl, entities ? entities->getChildren() : std::vector<ASTNode>{}});
    if (streams) {
      for (const auto &sNode : streams->getChildren()) {
        if (sNode && sNode->getNodeType() == AST::Stream) {
          checkStreamConnections(std::static_pointer_cast<StreamNode>(sNode),
                                 scopeStack, nullptr);
        }
      }
    }
    scopeStack.pop_back();
  }

  if (right && right->getNodeType() == AST::Function) {
    markConnectionForNode(right, scopeStack, left);
    previous = left;
    setInputEntityForFunction(std::static_pointer_cast<FunctionNode>(right),
                              previous);
  } else if (right && right->getNodeType() == AST::List) {
    previous = left;
    for (const auto &child : right->getChildren()) {
      if (child && child->getNodeType() == AST::Function) {
        setInputEntityForFunction(
            std::static_pointer_cast<FunctionNode>(child), previous);
      }
    }
  } else if (right && right->getNodeType() == AST::Declaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(right);
    auto entities = decl->getPropertyValue("entities");
    auto streams = decl->getPropertyValue("streams");
    markConnectionForNode(right, scopeStack, left);
    decl->setCompilerProperty("inputEntity", left);
    scopeStack.push_back(
        {decl, entities ? entities->getChildren() : std::vector<ASTNode>{}});
    if (streams) {
      for (const auto &sNode : streams->getChildren()) {
        if (sNode && sNode->getNodeType() == AST::Stream) {
          checkStreamConnections(std::static_pointer_cast<StreamNode>(sNode),
                                 scopeStack, nullptr);
        }
      }
    }
    scopeStack.pop_back();
  } else {
    previous = left;
  }

  if (right && right->getNodeType() == AST::Stream) {
    auto rightStream = std::static_pointer_cast<StreamNode>(right);
    checkStreamConnections(rightStream, scopeStack, previous);
    ASTNode next = rightStream->getLeft();
    if (next && left && left->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(left);
      setOutputEntityForFunction(func, next);
      next->setCompilerProperty("inputEntity", func);
    } else if (left && left->getNodeType() == AST::List) {
      for (const auto &child : left->getChildren()) {
        if (child && child->getNodeType() == AST::Function) {
          setOutputEntityForFunction(
              std::static_pointer_cast<FunctionNode>(child), next);
        }
      }
    }
  } else {
    markConnectionForNode(right, scopeStack, previous);
    if (left && (left->getNodeType() == AST::List ||
                 left->getNodeType() == AST::Expression)) {
      for (const auto &child : left->getChildren()) {
        if (child && child->getNodeType() == AST::Function) {
          setOutputEntityForFunction(
              std::static_pointer_cast<FunctionNode>(child), right);
        }
      }
    } else if (left && left->getNodeType() == AST::Function) {
      setOutputEntityForFunction(std::static_pointer_cast<FunctionNode>(left),
                                 right);
    }
  }
}

void ConnectionAndDependencyAnalyzer::markPreviousReads(
    const ASTNode &node, const ASTNode &previous,
    const ScopeStack &scopeStack) {
  if (!previous || !node) {
    return;
  }
  auto decl = (node->getNodeType() == AST::Declaration)
                  ? std::static_pointer_cast<DeclarationNode>(node)
                  : ASTQuery::findDeclarationForNode(node, scopeStack, m_ctx.tree);
  ASTNode newReadDomain;
  if (decl && (decl->getEntityType() == "module" ||
               decl->getEntityType() == "reaction" ||
               decl->getEntityType() == "loop")) {
    if (node->getNodeType() == AST::Function) {
      newReadDomain = node->getCompilerProperty("domain");
    }
  } else {
    newReadDomain = CodeAnalysis::getNodeDomain(node, scopeStack, m_ctx.tree);
  }

  auto appendReadDomain = [](const std::shared_ptr<ListNode> &readsList,
                             const ASTNode &domainNode) {
    if (!readsList || !domainNode) {
      return;
    }
    if (domainNode->getNodeType() == AST::MemberAccess) {
      auto newMember = std::static_pointer_cast<MemberAccessNode>(domainNode);
      for (const auto &read : readsList->getChildren()) {
        if (read && read->getNodeType() == AST::MemberAccess) {
          auto existing = std::static_pointer_cast<MemberAccessNode>(read);
          if (newMember->getEntity() == existing->getEntity() &&
              newMember->getPropertyName() == existing->getPropertyName()) {
            return;
          }
        }
      }
    }
    readsList->addChild(domainNode);
  };

  auto previousInstance = ASTQuery::getInstance(previous, scopeStack, m_ctx.tree);
  std::shared_ptr<ListNode> previousReads =
      previousInstance ? ensureListProperty(previousInstance, "reads") : nullptr;

  if (previous->getNodeType() == AST::Entity ||
      previous->getNodeType() == AST::Array) {
    if (node->getNodeType() == AST::Array) {
      if (auto indexList = std::static_pointer_cast<ArrayNode>(node)->index()) {
        for (const auto &indexNode : indexList->getChildren()) {
          markPreviousReads(indexNode, nullptr, scopeStack);
        }
      }
    }
    if (newReadDomain) {
      appendReadDomain(previousReads, newReadDomain);
    }
  } else if (previous->getNodeType() == AST::Expression ||
             previous->getNodeType() == AST::List) {
    newReadDomain = previous->getCompilerProperty("samplingDomain");
    std::function<void(const ASTNode &, const ASTNode &)>
        appendReadDomainForExprList = [&](const ASTNode &exprList,
                                          const ASTNode &dom) {
          if (!exprList) {
            return;
          }
          if (exprList->getNodeType() == AST::List ||
              exprList->getNodeType() == AST::Expression) {
            for (const auto &child : exprList->getChildren()) {
              appendReadDomainForExprList(child, dom);
            }
          } else if (auto inst =
                         ASTQuery::getInstance(exprList, scopeStack, m_ctx.tree)) {
            appendReadDomain(ensureListProperty(inst, "reads"), dom);
          }
        };
    appendReadDomainForExprList(previous, newReadDomain);
  } else if (previous->getNodeType() == AST::MemberAccess && newReadDomain) {
    appendReadDomain(previousReads, newReadDomain);
  }
}

void ConnectionAndDependencyAnalyzer::markConnectionForNode(
    const ASTNode &node, ScopeStack scopeStack, ASTNode previous,
    unsigned int listIndex) {
  if (!node) {
    return;
  }
  AST::Token nt = node->getNodeType();
  if (nt == AST::Entity || nt == AST::Array || nt == AST::Function ||
      nt == AST::Declaration || nt == AST::ArrayDeclaration) {
    if (nt == AST::Array) {
      auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
      auto domain = CodeAnalysis::getNodeDomain(node, scopeStack, m_ctx.tree);
      if (arrayNode->index()) {
        for (const auto &indexChild : arrayNode->index()->getChildren()) {
          markConnectionForNode(indexChild, scopeStack, nullptr);
          if (indexChild && (indexChild->getNodeType() == AST::Entity ||
                             indexChild->getNodeType() == AST::Array)) {
            auto frameworkNode = indexChild->getCompilerProperty("framework");
            std::string frameworkName;
            if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
              frameworkName = std::static_pointer_cast<ValueNode>(frameworkNode)
                                  ->getStringValue();
            }
            if (auto idxDecl = ASTQuery::findDeclarationForNode(
                    indexChild, scopeStack, m_ctx.tree, frameworkName)) {
              if (domain) {
                idxDecl->appendToPropertyValue("reads", domain);
              }
            }
          }
        }
      }
    }

    auto nodeInstance = ASTQuery::getInstance(node, scopeStack, m_ctx.tree);
    if (previous && nodeInstance) {
      auto nodeWritesProperties = ensureListProperty(nodeInstance, "writes");
      bool alreadyInWrites = false;
      ASTNode newWriteDomain;
      if (nt == AST::Function) {
        newWriteDomain = CodeAnalysis::getNodeDomain(
            CodeAnalysis::resolveConnectionEntity(node, scopeStack, m_ctx.tree,
                                                  false),
            scopeStack, m_ctx.tree);
      } else {
        newWriteDomain =
            CodeAnalysis::getNodeDomain(node, scopeStack, m_ctx.tree);
      }
      if (newWriteDomain && newWriteDomain->getNodeType() == AST::MemberAccess) {
        auto newMember =
            std::static_pointer_cast<MemberAccessNode>(newWriteDomain);
        for (const auto &write : nodeWritesProperties->getChildren()) {
          if (write && write->getNodeType() == AST::MemberAccess) {
            auto existing = std::static_pointer_cast<MemberAccessNode>(write);
            if (newMember->getEntity() == existing->getEntity() &&
                newMember->getPropertyName() == existing->getPropertyName()) {
              alreadyInWrites = true;
              break;
            }
          }
        }
      }
      if (!alreadyInWrites && newWriteDomain) {
        nodeWritesProperties->addChild(newWriteDomain);
      }
      node->setCompilerProperty("mainInput", previous);
      previous->setCompilerProperty("mainOutput", node);
    }

    auto decl = (nt == AST::Declaration)
                    ? std::static_pointer_cast<DeclarationNode>(node)
                    : ASTQuery::findDeclarationForNode(node, scopeStack,
                                                       m_ctx.tree);
    if (!decl) {
      return;
    }

    if (decl->getEntityType() == "trigger" && previous) {
      auto sourcesList = ensureListProperty(decl, "triggerSources");
      if (previous->getNodeType() == AST::List) {
        if (listIndex < previous->getChildren().size()) {
          sourcesList->addChild(previous->getChildren()[listIndex]);
        }
      } else {
        sourcesList->addChild(previous);
      }
    }

    if (decl->getEntityType() == "iterator" ||
        decl->getEntityType() == "sequence") {
      if (auto doneTrigger = decl->getPropertyValue("done")) {
        if (auto doneTriggerDecl = ASTQuery::findDeclarationForNode(
                doneTrigger, scopeStack, m_ctx.tree)) {
          auto triggerDests =
              ensureListProperty(doneTriggerDecl, "triggerDestinations");
          const auto &children = triggerDests->getChildren();
          if (std::find(children.begin(), children.end(), decl) ==
              children.end()) {
            triggerDests->addChild(decl);
          }
        }
      }
    }

    if (previous) {
      if (auto previousDecl = ASTQuery::findDeclarationForNode(
              previous, scopeStack, m_ctx.tree)) {
        if (previousDecl->getEntityType() == "trigger") {
          ensureListProperty(previousDecl, "triggerDestinations")
              ->addChild(node);
        }
      }
    }

    auto resetProp = decl->getPropertyValue("reset");
    if (resetProp && (resetProp->getNodeType() == AST::Entity ||
                      resetProp->getNodeType() == AST::Array)) {
      if (auto triggerDecl = ASTQuery::findDeclarationForNode(
              resetProp, scopeStack, m_ctx.tree)) {
        auto resets = ensureListProperty(triggerDecl, "triggerResets");
        bool alreadyRegistered = false;
        for (const auto &registeredDecl : resets->getChildren()) {
          if (std::static_pointer_cast<DeclarationNode>(registeredDecl)
                  ->getName() == decl->getName()) {
            alreadyRegistered = true;
            break;
          }
        }
        if (!alreadyRegistered) {
          resets->addChild(decl);
          auto domain =
              CodeAnalysis::getNodeDomain(node, scopeStack, m_ctx.tree);
          decl->appendToPropertyValue("resetDomains", domain);
        }
      }
    }

    if (nt == AST::Function) {
      if (auto entities = decl->getPropertyValue("entities")) {
        for (const auto &entityNode : entities->getChildren()) {
          if (!entityNode || entityNode->getNodeType() != AST::Declaration) {
            continue;
          }
          auto innerDecl =
              std::static_pointer_cast<DeclarationNode>(entityNode);
          if (innerDecl->getEntityType() == "reaction" ||
              innerDecl->getEntityType() == "loop") {
            auto streamsNode = innerDecl->getPropertyValue("streams");
            auto entitiesNode = innerDecl->getPropertyValue("entities");
            if (streamsNode) {
              if (entitiesNode) {
                scopeStack.push_back({node, entitiesNode->getChildren()});
              }
              for (const auto &stream : streamsNode->getChildren()) {
                if (stream && stream->getNodeType() == AST::Stream) {
                  checkStreamConnections(
                      std::static_pointer_cast<StreamNode>(stream), scopeStack);
                }
              }
              if (entitiesNode) {
                scopeStack.pop_back();
              }
            }
          }
        }
      }
    }

    ensureListProperty(decl, "reads");
    auto writesProperties = ensureListProperty(decl, "writes");
    if (previous) {
      AST::Token pnt = previous->getNodeType();
      if (pnt == AST::Entity || pnt == AST::Array) {
        markPreviousReads(node, previous, scopeStack);
      } else if (pnt == AST::Expression || pnt == AST::List) {
        for (const auto &child : previous->getChildren()) {
          markConnectionForNode(child, scopeStack, nullptr);
          markPreviousReads(node, child, scopeStack);
        }
      } else if (pnt == AST::Function || pnt == AST::Int ||
                 pnt == AST::Real || pnt == AST::Switch ||
                 pnt == AST::String) {
        writesProperties->addChild(decl->getDomain());
      }
    }
  } else if (nt == AST::Int || nt == AST::Real || nt == AST::Switch ||
             nt == AST::String) {
    ensureListProperty(node, "writes");
    if (previous) {
      AST::Token pnt = previous->getNodeType();
      if (pnt == AST::Entity || pnt == AST::Array) {
        markPreviousReads(node, previous, scopeStack);
      } else if (pnt == AST::Expression) {
        for (const auto &child : previous->getChildren()) {
          markConnectionForNode(child, scopeStack, nullptr);
          markPreviousReads(node, child, scopeStack);
        }
      }
    }
  } else if (nt == AST::Expression) {
    for (const auto &child : node->getChildren()) {
      markConnectionForNode(child, scopeStack, previous);
    }
  } else if (nt == AST::List) {
    int index = 0;
    for (const auto &child : node->getChildren()) {
      int inputSize =
          CodeAnalysis::getNodeNumInputs(child, scopeStack, m_ctx.tree);
      markConnectionForNode(child, scopeStack, previous, index);
      index += inputSize;
    }
  }
}

void ConnectionAndDependencyAnalyzer::analyzeParents() {
  for (const auto &node : m_ctx.tree->getChildren()) {
    if (node && (node->getNodeType() == AST::Declaration ||
                 node->getNodeType() == AST::ArrayDeclaration)) {
      appendParent(std::static_pointer_cast<DeclarationNode>(node), nullptr);
    }
  }
}

void ConnectionAndDependencyAnalyzer::appendParent(
    const std::shared_ptr<DeclarationNode> &decl,
    const std::shared_ptr<DeclarationNode> &parent) {
  auto parentList = decl->getCompilerProperty("parentDeclarations");
  if (!parentList) {
    parentList = std::make_shared<ListNode>(__FILE__, __LINE__);
    decl->setCompilerProperty("parentDeclarations", parentList);
  }
  if (parent) {
    for (const auto &existing : parentList->getChildren()) {
      if (existing == parent) {
        return;
      }
    }
    parentList->addChild(parent);
  }
  if (decl->getEntityType() == "module" ||
      decl->getEntityType() == "reaction" || decl->getEntityType() == "loop") {
    if (auto internalEntities = decl->getPropertyValue("entities")) {
      for (const auto &node : internalEntities->getChildren()) {
        if (!node) {
          continue;
        }
        assert(node->getNodeType() == AST::Declaration ||
               node->getNodeType() == AST::ArrayDeclaration);
        if (node->getNodeType() == AST::Declaration ||
            node->getNodeType() == AST::ArrayDeclaration) {
          auto childDecl = std::static_pointer_cast<DeclarationNode>(node);
          for (const auto &grandParent : parentList->getChildren()) {
            if (grandParent &&
                (grandParent->getNodeType() == AST::Declaration ||
                 grandParent->getNodeType() == AST::ArrayDeclaration)) {
              appendParent(childDecl,
                           std::static_pointer_cast<DeclarationNode>(
                               grandParent));
            }
          }
          appendParent(childDecl, decl);
        }
      }
    }
  }
}

void ConnectionAndDependencyAnalyzer::analyzePersistence() {
  ScopeStack scope;
  bool rootStreamsAnalyzed = false;
  for (const auto &child : m_ctx.tree->getChildren()) {
    if (child && (child->getNodeType() == AST::Declaration ||
                  child->getNodeType() == AST::ArrayDeclaration)) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      auto entitiesList = decl->getPropertyValue("entities");
      auto streamsList = decl->getPropertyValue("streams");
      if (streamsList) {
        scope.push_back(
            {decl, entitiesList ? entitiesList->getChildren()
                                : std::vector<ASTNode>{}});
        markNodePersistence(streamsList, scope, m_ctx.tree);
        scope.pop_back();
      }
    } else if (!rootStreamsAnalyzed && child &&
               child->getNodeType() == AST::Stream) {
      markNodePersistence(m_ctx.tree, scope, m_ctx.tree);
      rootStreamsAnalyzed = true;
    }
  }
}

void ConnectionAndDependencyAnalyzer::markNodePersistence(
    const ASTNode &streamsList, ScopeStack &scope, const ASTNode &tree) {
  PersistenceState state;
  resolvePersistenceRecursive(streamsList, scope, tree, m_ctx.tree, state,
                              /*isUnconditional=*/true);
}

void ConnectionAndDependencyAnalyzer::analyzeStreamDependencies() {
  ScopeStack scope;

  for (const auto &child : m_ctx.tree->getChildren()) {
    if (!child || (child->getNodeType() != AST::Declaration &&
                   child->getNodeType() != AST::ArrayDeclaration)) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(child);
    auto entitiesList = decl->getPropertyValue("entities");
    auto streamsList = decl->getPropertyValue("streams");
    if (!streamsList) {
      continue;
    }
    std::vector<std::shared_ptr<StreamNode>> declStreams;
    for (const auto &s : streamsList->getChildren()) {
      if (s && s->getNodeType() == AST::Stream) {
        declStreams.push_back(std::static_pointer_cast<StreamNode>(s));
      }
    }
    if (!declStreams.empty()) {
      scope.push_back(
          {decl, entitiesList ? entitiesList->getChildren()
                              : std::vector<ASTNode>{}});
      analyzeContainerStreamDependencies(declStreams, scope, decl);
      scope.pop_back();
    }
  }

  std::map<std::string, std::vector<std::shared_ptr<StreamNode>>> domainStreams;
  std::vector<std::shared_ptr<StreamNode>> allTopStreams;

  for (const auto &child : m_ctx.tree->getChildren()) {
    if (child && child->getNodeType() == AST::Stream) {
      auto stream = std::static_pointer_cast<StreamNode>(child);
      allTopStreams.push_back(stream);
      std::string domName =
          CodeAnalysis::getNodeDomainName(stream, scope, m_ctx.tree);
      if (domName.empty() && stream->getLeft()) {
        domName =
            CodeAnalysis::getNodeDomainName(stream->getLeft(), scope, m_ctx.tree);
      }
      if (domName.empty()) {
        domName = "RootDomain";
      }
      if (stream->getRight()) {
        std::string rightDom = CodeAnalysis::getNodeDomainName(
            stream->getRight(), scope, m_ctx.tree);
        if (!rightDom.empty() && rightDom != domName &&
            !ASTQuery::getCommonAncestorDomain(domName, rightDom, m_ctx.tree)
                 .empty()) {
          domName = rightDom;
        }
      }
      domainStreams[domName].push_back(stream);
    }
  }

  for (const auto &[domName, dStreams] : domainStreams) {
    auto domDecl = ASTQuery::findDomainDeclaration(domName, m_ctx.tree);
    analyzeContainerStreamDependencies(dStreams, scope, domDecl);
  }

  if (!allTopStreams.empty()) {
    analyzeContainerStreamDependencies(allTopStreams, scope, m_ctx.tree);
  }
}

void ConnectionAndDependencyAnalyzer::analyzeContainerStreamDependencies(
    const std::vector<std::shared_ptr<StreamNode>> &streams,
    const ScopeStack &scope, const ASTNode &containerDecl) {
  if (streams.empty()) {
    return;
  }

  std::function<void(const ASTNode &, std::vector<std::string> &)>
      collectEntityNames =
          [&](const ASTNode &n, std::vector<std::string> &names) {
            if (!n) {
              return;
            }
            if (n->getNodeType() == AST::Entity) {
              std::string nm = ASTQuery::getNodeName(n);
              if (!nm.empty()) {
                names.push_back(nm);
              }
            } else if (n->getNodeType() == AST::Array) {
              std::string nm = ASTQuery::getNodeName(n);
              if (!nm.empty()) {
                names.push_back(nm);
              }
              collectEntityNames(std::static_pointer_cast<ArrayNode>(n)->index(),
                                 names);
            } else if (n->getNodeType() == AST::MemberAccess) {
              if (!n->getChildren().empty()) {
                collectEntityNames(n->getChildren()[0], names);
              }
            } else if (n->getNodeType() == AST::List ||
                       n->getNodeType() == AST::Expression) {
              for (const auto &child : n->getChildren()) {
                collectEntityNames(child, names);
              }
            }
          };

  struct StreamInfo {
    std::shared_ptr<StreamNode> stream;
    std::set<std::string> reads;
    std::set<std::string> writes;
    double rate = -1.0;
    int64_t rank = 0;
    bool isSubdomainCall = false;
    std::set<size_t> dependencies;
    std::set<size_t> dependents;
  };

  std::vector<StreamInfo> streamInfos;
  streamInfos.reserve(streams.size());

  for (size_t i = 0; i < streams.size(); ++i) {
    StreamInfo info;
    info.stream = streams[i];

    StreamNodeIterator streamIt(streams[i]);
    bool streamStart = true;
    while (auto node = streamIt.next()) {
      double nodeRate = CodeAnalysis::getNodeRate(node, scope, m_ctx.tree);
      if (nodeRate > 0.0) {
        info.rate = nodeRate;
      }

      std::vector<std::string> nodeNames;
      if (node->getNodeType() == AST::Function) {
        auto funcNode = std::static_pointer_cast<FunctionNode>(node);
        auto funcDecl =
            ASTQuery::findDeclarationForNode(funcNode, scope, m_ctx.tree);
        if (funcDecl && funcDecl->getEntityType() == "_domainDefinition") {
          info.isSubdomainCall = true;
        }
        for (const auto &prop : funcNode->getProperties()) {
          if (prop && prop->getValue()) {
            collectEntityNames(prop->getValue(), nodeNames);
          }
        }
      } else {
        collectEntityNames(node, nodeNames);
      }

      for (const auto &name : nodeNames) {
        if (streamIt.hasNext()) {
          info.reads.insert(name);
        }
        if (!streamStart) {
          info.writes.insert(name);
        }
      }
      streamStart = false;
    }
    if (info.rate <= 0.0) {
      info.rate = CodeAnalysis::getNodeRate(streams[i], scope, m_ctx.tree);
    }
    if (info.rate <= 0.0 && containerDecl &&
        (containerDecl->getNodeType() == AST::Declaration ||
         containerDecl->getNodeType() == AST::ArrayDeclaration)) {
      auto cDecl = std::static_pointer_cast<DeclarationNode>(containerDecl);
      if (cDecl->getEntityType() == "_domainDefinition" ||
          cDecl->getEntityType() == "gameDefinition") {
        info.rate = ASTQuery::getDomainDefaultRate(cDecl);
      } else if (ASTNode domNode =
                     CodeAnalysis::getNodeDomain(cDecl, scope, m_ctx.tree)) {
        auto dDecl = ASTQuery::findDomainDeclaration(
            ASTQuery::getDomainIdentifier(domNode, scope, m_ctx.tree),
            m_ctx.tree);
        if (dDecl) {
          info.rate = ASTQuery::getDomainDefaultRate(dDecl);
        }
      }
    }
    streamInfos.push_back(std::move(info));
  }

  auto isPersistent = [&](const std::string &name) {
    auto decl = ASTQuery::findDeclarationByName(name, scope, m_ctx.tree);
    return decl && decl->getCompilerProperty("persistent") != nullptr;
  };

  for (size_t j = 0; j < streamInfos.size(); ++j) {
    for (size_t i = 0; i < j; ++i) {
      bool hasDep =
          streamInfos[i].isSubdomainCall || streamInfos[j].isSubdomainCall;
      for (const auto &w : streamInfos[i].writes) {
        if (streamInfos[j].reads.count(w) && !isPersistent(w)) {
          hasDep = true;
          break;
        }
      }
      if (!hasDep) {
        for (const auto &w : streamInfos[i].writes) {
          if (streamInfos[j].writes.count(w)) {
            hasDep = true;
            break;
          }
        }
      }
      if (hasDep) {
        streamInfos[j].dependencies.insert(i);
        streamInfos[i].dependents.insert(j);
      }
    }
  }

  std::vector<size_t> inDegree(streamInfos.size(), 0);
  std::queue<size_t> q;
  for (size_t i = 0; i < streamInfos.size(); ++i) {
    inDegree[i] = streamInfos[i].dependencies.size();
    if (inDegree[i] == 0) {
      streamInfos[i].rank = 0;
      q.push(i);
    }
  }

  size_t visited = 0;
  while (!q.empty()) {
    size_t curr = q.front();
    q.pop();
    ++visited;
    for (size_t succ : streamInfos[curr].dependents) {
      streamInfos[succ].rank =
          std::max(streamInfos[succ].rank, streamInfos[curr].rank + 1);
      if (--inDegree[succ] == 0) {
        q.push(succ);
      }
    }
  }

  if (visited < streamInfos.size()) {
    for (size_t i = 0; i < streamInfos.size(); ++i) {
      streamInfos[i].rank = static_cast<int64_t>(i);
    }
  }

  for (const auto &info : streamInfos) {
    auto readList = std::make_shared<ListNode>(__FILE__, __LINE__);
    for (const auto &r : info.reads) {
      readList->addChild(std::make_shared<EntityNode>(r, __FILE__, __LINE__));
    }
    info.stream->setCompilerProperty("reads", readList);

    auto writeList = std::make_shared<ListNode>(__FILE__, __LINE__);
    for (const auto &w : info.writes) {
      writeList->addChild(std::make_shared<EntityNode>(w, __FILE__, __LINE__));
    }
    info.stream->setCompilerProperty("writes", writeList);

    auto depList = std::make_shared<ListNode>(__FILE__, __LINE__);
    for (size_t d : info.dependencies) {
      depList->addChild(streamInfos[d].stream);
    }
    info.stream->setCompilerProperty("dependencies", depList);

    auto depndList = std::make_shared<ListNode>(__FILE__, __LINE__);
    for (size_t d : info.dependents) {
      depndList->addChild(streamInfos[d].stream);
    }
    info.stream->setCompilerProperty("dependents", depndList);

    info.stream->setCompilerProperty(
        "topologicalRank",
        std::make_shared<ValueNode>(info.rank, __FILE__, __LINE__));
  }

  std::vector<size_t> sortedIndices(streamInfos.size());
  for (size_t i = 0; i < streamInfos.size(); ++i) {
    sortedIndices[i] = i;
  }
  std::sort(sortedIndices.begin(), sortedIndices.end(),
            [&](size_t a, size_t b) {
              if (streamInfos[a].rank != streamInfos[b].rank) {
                return streamInfos[a].rank < streamInfos[b].rank;
              }
              return a < b;
            });

  auto stagesList = std::make_shared<ListNode>(__FILE__, __LINE__);
  std::shared_ptr<ListNode> currentStage;
  double currentStageRate = -999.0;

  for (size_t idx : sortedIndices) {
    double streamRate = streamInfos[idx].rate;
    const bool canCoalesce = (currentStage != nullptr) &&
                             (std::abs(streamRate - currentStageRate) < 1e-9);
    if (canCoalesce) {
      currentStage->addChild(streamInfos[idx].stream);
    } else {
      currentStage = std::make_shared<ListNode>(__FILE__, __LINE__);
      currentStageRate = streamRate;
      if (currentStageRate > 0.0) {
        currentStage->setCompilerProperty(
            "rate",
            std::make_shared<ValueNode>(currentStageRate, __FILE__, __LINE__));
      }
      currentStage->addChild(streamInfos[idx].stream);
      stagesList->addChild(currentStage);
    }
  }

  if (containerDecl) {
    containerDecl->setCompilerProperty("streamStages", stagesList);
  }
}

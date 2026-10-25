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
#include "stride/stridegen/codevalidator.hpp"
// #include "stride/stridegen/stridesystem.hpp"
#include "stride/stridegen/coderesolver.hpp"

#include "stride/parser/arraynode.h"
#include "stride/parser/generatorrangenode.h"
#include "stride/parser/rangenode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

#include <algorithm>
#include <map>
#include <cassert>
#include <iostream>
#include <set>

using namespace strd;

CodeResolver::CodeResolver(ASTNode tree, std::string striderootDir,
                           SystemConfiguration systemConfig)
    : m_systemConfig(systemConfig), m_tree(tree) {
  std::vector<std::shared_ptr<ImportNode>> importList =
      ASTQuery::getImportNodes(tree);
  std::vector<std::shared_ptr<SystemNode>> systems =
      ASTQuery::getSystemNodes(tree);

  if (systems.size() > 0) {
    std::shared_ptr<SystemNode> platformNode = systems.at(0);
    m_system = std::make_shared<StrideSystem>(
        striderootDir, platformNode->platformName(),
        platformNode->majorVersion(), platformNode->minorVersion(), importList);
    m_system->m_systemConfig = systemConfig;
    for (size_t i = 1; i < systems.size(); i++) {
      std::cerr << "Ignoring system: " << platformNode->platformName()
                << std::endl;
      LangError error;
      error.type = LangError::SystemRedefinition;
      error.errorTokens.push_back(platformNode->platformName());
      error.filename = platformNode->getFilename();
      error.lineNumber = platformNode->getLine();
      //      m_errors.append(error);
    }
  } else { // Make a default platform that only inlcudes the common library
    m_system =
        std::make_shared<StrideSystem>(striderootDir, "", -1, -1, importList);
  }
}

CodeResolver::~CodeResolver() {}

void CodeResolver::process() {
  processSystem();
  desugar();
  // insert domain IO entity declarations
  auto ioEntityDecls =
      strd::CodeAnalysis::getDomainIOEntityDeclarations(m_tree);
  for (const auto &decl : ioEntityDecls) {
    m_tree->addChild(decl);
  }
  // Insert objects
  ASTFunctions::insertRequiredObjects(m_tree, m_system->getImportTrees());
  ASTFunctions::fillDefaultProperties(m_tree);
  ASTFunctions::processAnoymousDeclarations(m_tree);

  declareModuleInternalEntities();
  m_system->injectResourceConfiguration(m_tree);

  // Resolve and massage tree
  expandDefaultRanges();
  resolveDeclarationSymbols();
  expandIndexedStreams();
  expandParallel(); // Find better name this expands bundles, functions and
                    // declares undefined bundles
                    //    processResets();
  resolveStreamSymbols();

  // Do another pass to resolve new node dependencies and structure
  ASTFunctions::insertRequiredObjects(m_tree, m_system->getImportTrees());
  ASTFunctions::fillDefaultProperties(m_tree);
  ASTFunctions::processAnoymousDeclarations(m_tree);
  expandDefaultRanges(); // For declarations inserted by the pass above

  if (m_systemConfig.testing) {
    enableTesting();
    // Do another pass to resolve new node dependencies and structure
    ASTFunctions::insertRequiredObjects(m_tree, m_system->getImportTrees());
    ASTFunctions::fillDefaultProperties(m_tree);
    ASTFunctions::processAnoymousDeclarations(m_tree);
  }

  m_system->injectResourceConfiguration(m_tree);

  resolveConstants();
  resolveMemberAccesses();

  processDeclarations();
  processDomains();

  // Prepare additional metadata
  storeDeclarations();
  analyzeConnections();
  analyzeParents();

  // Resolve rates and types from stream connections
  setPortEntityAttributes({}, m_tree);
  resolveAttributes({}, m_tree);
  resolveTypeCasting();
  resolveDeclarationDataTypes();
  analyzePersistence();
}

void CodeResolver::processSystem() {
  // Gather import statements
  std::vector<std::shared_ptr<ImportNode>> importList;
  for (const ASTNode &node : m_tree->getChildren()) {
    if (node->getNodeType() == AST::Import) {
      std::shared_ptr<ImportNode> import =
          std::static_pointer_cast<ImportNode>(node);
      // TODO add namespace support here (e.g. import
      // Platform::Filters::Filter)
      bool imported = false;
      for (const auto &importNode : importList) {
        if ((std::static_pointer_cast<ImportNode>(importNode)->importName() ==
             import->importName()) &&
            (std::static_pointer_cast<ImportNode>(importNode)->importAlias() ==
             import->importAlias())) {
          imported = true;
          break;
        }
      }
      if (!imported) {
        importList.push_back(import);
      }
    }
  }

  // Process includes for system
  // Import root library
  //  m_system->loadImportTree("", "");

  if (m_system->systemName() != "") {

    // Load nodes for imports
    for (const auto &import : importList) {
      std::string importName = import->importName();
      std::string importAlias = import->importAlias();
      m_system->loadImportTree(importName, importAlias);
    }

    // Add platform domain to ensure a minimal system is available
    auto platformDomain = m_system->getPlatformDomain();
    if (platformDomain) {
      std::string domainName = ASTQuery::getNodeName(platformDomain);
      auto existingDomain =
          ASTQuery::findDeclarationByName(domainName, {}, m_tree);
      if (!existingDomain) {
        for (auto objects : m_system->getImportTrees()) {
          auto domainDecl = ASTQuery::findDeclarationByName(
              domainName, {{nullptr, objects.second}}, nullptr);
          if (domainDecl) {
            m_tree->addChild(domainDecl);
            break;
          }
        }
      }
    }
  }
}

void CodeResolver::desugar() {
  std::set<const void *> visited;
  desugarSingleEntryStreams(m_tree, visited);
  for (const auto &pair : m_system->getImportTrees()) {
    for (const auto &node : pair.second) {
      desugarSingleEntryStreams(node, visited);
    }
  }
}

void CodeResolver::desugarSingleEntryStreams(ASTNode node,
                                             std::set<const void *> &visited) {
  if (!node || visited.count(node.get()) > 0) {
    return;
  }
  visited.insert(node.get());

  if (node->getNodeType() == AST::Stream) {
    auto stream = std::static_pointer_cast<StreamNode>(node);
    if (!stream->getRight()) {
      // Desugar single entry stream into: on >> left
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
    auto streamBlocks =
        ASTQuery::getContainedStreamBlocks(decl, ScopeStack(), m_tree);
    for (const auto &block : streamBlocks) {
      for (const auto &stream : block.streams) {
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

void CodeResolver::resolveStreamRatesReverse(std::shared_ptr<StreamNode> stream,
                                             const ScopeStack &scope,
                                             ASTNode tree) {
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  if (!right) {
    return;
  }
  double rate = CodeAnalysis::getNodeRate(left, scope, tree);
  double rightRate = -1;
  if (right->getNodeType() == AST::Stream) {
    resolveStreamRatesReverse(std::static_pointer_cast<StreamNode>(right),
                              scope, tree);
    rightRate = CodeAnalysis::getNodeRate(
        static_cast<StreamNode *>(right.get())->getLeft(), scope, tree);
  } else {
    rightRate = CodeAnalysis::getNodeRate(right, scope, tree);
  }
  if (rate < 0 && rightRate >= 0) {
    CodeResolver::setNodeRate(left, rightRate, scope, tree);
  }
}

void CodeResolver::resolveStreamRates(std::shared_ptr<StreamNode> stream,
                                      const ScopeStack &scope, ASTNode tree) {
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  double rate = CodeAnalysis::getNodeRate(left, {}, m_tree);
  if (rate < 0) {
    // FIXME we should resolve rate to rate in the block's domain, then force to
    // platform rate Force node rate to platform rate
    if (m_system && m_system->getPlatformDomain()) {
      std::shared_ptr<DeclarationNode> domainDeclaration =
          CodeAnalysis::findDomainDeclaration(
              ASTQuery::getNodeName(m_system->getPlatformDomain()), m_tree);
      if (domainDeclaration) {
        ASTNode rateValue = domainDeclaration->getPropertyValue("rate");
        if (rateValue->getNodeType() == AST::Int ||
            rateValue->getNodeType() == AST::Real) {
          rate = static_cast<ValueNode *>(rateValue.get())->toReal();
          CodeResolver::setNodeRate(left, rate, {}, m_tree);
        } else if (rateValue->getNodeType() == AST::MemberAccess) {
          if (left->getNodeType() == AST::Declaration) {
            auto decl = std::static_pointer_cast<DeclarationNode>(left);
            decl->replacePropertyValue("rate", rateValue->deepCopy());
          }
        } else {
          std::cerr << "Unexpected type for rate in domain declaration: "
                    << ASTQuery::getNodeName(m_system->getPlatformDomain())
                    << std::endl;
        }
      }
    }
  }
  if (!right)
    return;
  double rightRate = -1;
  if (right->getNodeType() == AST::Stream) {
    rightRate = CodeAnalysis::getNodeRate(
        static_cast<StreamNode *>(right.get())->getLeft(), {}, m_tree);
    if (rightRate <= 0 && rate >= 0) {
      CodeResolver::setNodeRate(
          static_cast<StreamNode *>(right.get())->getLeft(), rate, {}, m_tree);
    }
    resolveStreamRates(std::static_pointer_cast<StreamNode>(right), scope,
                       tree);
  } else {
    rightRate = CodeAnalysis::getNodeRate(right, {}, m_tree);
    if (rightRate <= 0) {
      if (rate >= 0) {
        CodeResolver::setNodeRate(right, rate, {}, m_tree);
      } else {
        auto rightDomain = CodeAnalysis::getNodeDomain(right, {}, m_tree);
        if (rightDomain) {
          auto domainDecl = CodeAnalysis::findDomainDeclaration(
              CodeAnalysis::getDomainIdentifier(rightDomain, {}, m_tree),
              m_tree);
          if (domainDecl) {
            auto defaultRate = CodeAnalysis::getDomainDefaultRate(domainDecl);
            CodeResolver::setNodeRate(right, defaultRate, {}, m_tree);

          } else {
            std::cout
                << __FILE__ << ":" << __LINE__
                << " ERROR: can't find domain declaration to set rate for "
                << right->toText() << std::endl;
          }
        }
      }
    }
  }
}

void CodeResolver::resolveStreamTypesReverse(std::shared_ptr<StreamNode> stream,
                                             ScopeStack stack, ASTNode tree) {
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  if (!right)
    return;
  auto types = CodeAnalysis::getOutputDataTypes(left, stack, tree);
  std::vector<ASTNode> rightTypes;
  if (right->getNodeType() == AST::Stream) {
    resolveStreamTypesReverse(std::static_pointer_cast<StreamNode>(right),
                              stack, tree);
    rightTypes = CodeAnalysis::getInputDataTypes(
        std::static_pointer_cast<StreamNode>(right)->getLeft(), stack, tree);
  } else {
    rightTypes = CodeAnalysis::getInputDataTypes(right, stack, tree);
  }
  if (rightTypes.size() == types.size()) {
    // TODO support lists, expressions and bundle ranges
    for (int i = 0; i < rightTypes.size(); i++) {
      if (rightTypes[i] && (rightTypes[i] != nullptr &&
                            rightTypes[i]->getNodeType()) != AST::None) {
        CodeResolver::setNodeType(left, rightTypes[i], stack, tree);
      }
    }
  }
}

void CodeResolver::markUnresolvedOutputType(ASTNode source, ASTNode target,
                                            ScopeStack scope, ASTNode tree) {
  // A module whose output type comes from its own output port can't give a
  // type to an undeclared block it writes to. ASTValidation reports it.
  if (source->getNodeType() != AST::Function ||
      (target->getNodeType() != AST::Entity &&
       target->getNodeType() != AST::Array)) {
    return;
  }
  auto funcName = ASTQuery::getNodeName(source);
  auto funcDecl = ASTQuery::findDeclarationByName(funcName, scope, tree);
  if (!funcDecl) {
    funcDecl = ASTQuery::findDeclarationByName(funcName, {}, m_tree);
  }
  if (!funcDecl || !CodeAnalysis::isOutputTypeFromOutputPort(funcDecl)) {
    return;
  }
  auto targetName = ASTQuery::getNodeName(target);
  auto targetDecl = ASTQuery::findDeclarationByName(targetName, scope, tree);
  if (!targetDecl) {
    targetDecl = ASTQuery::findDeclarationByName(targetName, {}, m_tree);
  }
  if (!targetDecl || !targetDecl->getCompilerProperty("autoDeclared")) {
    return;
  }
  auto type = targetDecl->getPropertyValue("type");
  if (!type || type->getNodeType() == AST::None) {
    targetDecl->setCompilerProperty(
        "unresolvedType",
        std::make_shared<ValueNode>(funcDecl->getName(), __FILE__, __LINE__));
  }
}

void CodeResolver::resolveStreamTypesForward(std::shared_ptr<StreamNode> stream,
                                             ScopeStack scope, ASTNode tree) {
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  if (!right)
    return;
  auto types = CodeAnalysis::getOutputDataTypes(left, scope, tree);

  if (right->getNodeType() == AST::Stream) {
    markUnresolvedOutputType(
        left, std::static_pointer_cast<StreamNode>(right)->getLeft(), scope,
        tree);
    auto rightTypes = CodeAnalysis::getInputDataTypes(
        std::static_pointer_cast<StreamNode>(right)->getLeft(), scope, tree);
    if (rightTypes.size() == types.size()) {
      for (int i = 0; i < rightTypes.size(); i++) {
        if (!rightTypes[i] || rightTypes[i]->getNodeType() == AST::None) {
          CodeResolver::setNodeType(
              static_cast<StreamNode *>(right.get())->getLeft(), types[i],
              scope, tree);
        }
      }
    }
    resolveStreamTypesForward(std::static_pointer_cast<StreamNode>(right),
                              scope, tree);
  } else {
    markUnresolvedOutputType(left, right, scope, tree);
    auto rightTypes = CodeAnalysis::getInputDataTypes(right, scope, tree);
    if (rightTypes.size() == types.size()) {
      if (right->getNodeType() == AST::List) {
        // TODO handle nested lists
        if (right->getChildren().size() == types.size()) {
          for (int i = 0; i < rightTypes.size(); i++) {
            CodeResolver::setNodeType(right->getChildren()[i], types[i], scope,
                                      tree);
          }
        }
      } else if (right->getNodeType() == AST::Expression) {
        for (int i = 0; i < rightTypes.size(); i++) {
          CodeResolver::setNodeType(right->getChildren()[i], types[i], scope,
                                    tree);
        }
      } else {
        // TODO support bundle ranges
        if (types.size() == 1 && types[0] != nullptr) {
          CodeResolver::setNodeType(right, types[0], scope, tree);
        } else if (right->getChildren().size() == types.size()) {
          for (int i = 0; i < rightTypes.size(); i++) {
            CodeResolver::setNodeType(right->getChildren()[i], types[i], scope,
                                      tree);
          }
        }
      }
    }
  }
}

void CodeResolver::markNodePersistence(ASTNode streamsList, ASTNode blocksList,
                                       ScopeStack &scope, ASTNode tree) {
  if (!streamsList)
    return;
  std::set<std::string> blocksWritten;
  std::set<std::string> blocksRead;

  std::function<void(const ASTNode &, std::vector<std::string> &)>
      collectNodeNames =
          [&](const ASTNode &n, std::vector<std::string> &names) {
            if (!n)
              return;
            if (n->getNodeType() == AST::List ||
                n->getNodeType() == AST::Expression) {
              for (const auto &element : n->getChildren()) {
                collectNodeNames(element, names);
              }
            } else if (n->getNodeType() != AST::Function) {
              std::string nm = ASTQuery::getNodeName(n);
              if (!nm.empty()) {
                names.push_back(nm);
              }
            }
          };

  for (const ASTNode &stream : streamsList->getChildren()) {
    if (stream->getNodeType() == AST::Stream) {
      bool streamStart = true;

      // Check all blocks and determine which local blocks need to be made
      // persistent
      auto streamIt =
          StreamNodeIterator(std::static_pointer_cast<StreamNode>(stream));
      while (auto node = streamIt.next()) {
        std::vector<std::string> nodeNames;

        // TODO handle streams inside functions recursively.
        if (node->getNodeType() == AST::Function) {
          auto funcDecl = ASTQuery::findDeclarationByName(
              ASTQuery::getNodeName(node), scope, m_tree);
          if (funcDecl) {
            auto funcDeclType = ASTQuery::findTypeDeclarationByName(
                funcDecl->getEntityType(), scope, m_tree);
            if (funcDeclType &&
                ASTQuery::isCodeGenerator(funcDeclType, scope, m_tree)) {
              auto blocksList = funcDecl->getPropertyValue("blocks");
              auto streamsList = funcDecl->getPropertyValue("streams");
              if (blocksList) {
                scope.push_back({funcDecl, blocksList->getChildren()});
              }
              markNodePersistence(streamsList, blocksList, scope, tree);
              if (blocksList) {
                scope.pop_back();
              }
            }
          } else {
            std::cerr << "ERROR: Can't find declaration for: " << node->toText()
                      << std::endl;
          }
        } else {
          collectNodeNames(node, nodeNames);
        }

        for (const auto &nodeName : nodeNames) {
          auto blockDeclInternal =
              ASTQuery::findDeclarationByName(nodeName, scope, tree);
          if (blockDeclInternal) {
            if (streamIt.hasNext()) { // If there is a next node, this is
              // a read
              blocksRead.insert(nodeName);
            }
            if (!streamStart) {
              if (std::find(blocksRead.begin(), blocksRead.end(), nodeName) !=
                  blocksRead.end()) {
                if (std::find(blocksWritten.begin(), blocksWritten.end(),
                              nodeName) == blocksWritten.end()) {
                  // First time writing but we have already read, this block
                  // should to be persistent. But we need to check if scope
                  // allows persistence.
                  // TODO should this happen instead by explicitly making the
                  // reset first before processing here?
                  bool persistenceAllowed = true;
                  if (scope.size() > 0) {
                    auto parentNode = scope.back().first;
                    if (parentNode &&
                        (parentNode->getNodeType() == AST::Declaration ||
                         parentNode->getNodeType() == AST::ArrayDeclaration)) {
                      auto parentDecl =
                          std::static_pointer_cast<DeclarationNode>(parentNode);
                      auto typeDecl = ASTQuery::findTypeDeclaration(
                          parentDecl, scope, tree);
                      // Assume type is stateful unless proven otherwise.
                      // Is this the best idea?
                      if (!typeDecl || (!ASTQuery::isStatelessGenerator(
                                           typeDecl, scope, tree))) {
                        if (persistenceAllowed) {
                          blockDeclInternal->setCompilerProperty(
                              "persistent", std::make_shared<ValueNode>(
                                                true, __FILE__, __LINE__));
                        }
                      }
                    }
                  }
                }
              }
              blocksWritten.insert(nodeName);
            }
          }
        }
        streamStart = false;
      }
    }
  }
}

void CodeResolver::analyzeChildConnections(ASTNode node,
                                           ScopeStack scopeStack) {
  if (!node) {
    return;
  }

  for (const ASTNode &object : node->getChildren()) {
    if (object->getNodeType() == AST::Stream) {
      checkStreamConnections(std::static_pointer_cast<StreamNode>(object),
                             scopeStack);
    }
  }

  auto getDeclarations = [](ASTNode n) {
    std::vector<std::shared_ptr<DeclarationNode>> decls;
    if (!n) {
      return decls;
    }
    for (const auto &child : n->getChildren()) {
      if (child->getNodeType() == AST::Declaration ||
          child->getNodeType() == AST::ArrayDeclaration) {
        decls.push_back(std::static_pointer_cast<DeclarationNode>(child));
      }
    }
    return decls;
  };

  for (const auto &decl : getDeclarations(node)) {
    auto streamBlocks =
        ASTQuery::getContainedStreamBlocks(decl, scopeStack, m_tree);
    ASTNode blocks = decl->getPropertyValue("blocks");
    if (streamBlocks.empty() && !blocks) {
      continue;
    }
    scopeStack.push_back(
        {decl, blocks ? blocks->getChildren() : std::vector<ASTNode>{}});
    for (const auto &block : streamBlocks) {
      for (const auto &stream : block.streams) {
        checkStreamConnections(stream, scopeStack);
      }
    }
    if (blocks) {
      analyzeChildConnections(blocks, scopeStack);
    }
    scopeStack.pop_back();
  }
}

void CodeResolver::enableTesting() {
  auto treeChildren = m_tree->getChildren();
  for (auto platform : m_system->getFrameworks()) {
    std::vector<ASTNode> testingObjs = platform->getPlatformTestingObjectsRef();
    for (size_t i = 0; i < treeChildren.size(); i++) {
      if (treeChildren[i]->getNodeType() == AST::Declaration ||
          treeChildren[i]->getNodeType() == AST::ArrayDeclaration) {
        std::shared_ptr<DeclarationNode> decl =
            std::static_pointer_cast<DeclarationNode>(treeChildren[i]);
        for (const ASTNode &testingObj : testingObjs) {
          if (testingObj->getNodeType() == AST::Declaration ||
              testingObj->getNodeType() == AST::ArrayDeclaration) {
            std::shared_ptr<DeclarationNode> testDecl =
                std::static_pointer_cast<DeclarationNode>(testingObj);
            if (decl->getName() ==
                testDecl
                    ->getName()) { // FIXME we need to check for namespace too
              treeChildren[i] = testDecl;
              break;
            }
          } else {
            std::cerr << "Unexpected node in testing file." << std::endl;
          }
        }
      }
    }
  }
  m_tree->setChildren(treeChildren);
}

void CodeResolver::declareModuleInternalEntities() {
  for (const ASTNode &node : m_tree->getChildren()) {
    declareInternalEntitiesForNode(node, {});
  }

  //    populateContextDomains(m_tree->getChildren());
}

void CodeResolver::expandParallelStream(std::shared_ptr<StreamNode> stream,
                                        ScopeStack scopeStack, ASTNode tree) {
  std::shared_ptr<StreamNode> subStream = stream;

  // Figure out stream IO sizes
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  std::vector<std::pair<int, int>> IOs;
  while (right) {
    if (left->getNodeType() ==
        AST::Function) { // Expand from properties size to list
      auto funcInputs = CodeAnalysis::getNodeNumInputs(left, scopeStack, tree);
      if (funcInputs != CodeAnalysis::SIZE_MEMBER_ACCESS) {
        ASTNode newFunctions = expandFunctionFromProperties(
            std::static_pointer_cast<FunctionNode>(left), scopeStack, tree);
        if (newFunctions) {
          subStream->setLeft(newFunctions);
          left = subStream->getLeft();
        }
      }
    }
    IOs.emplace_back(std::pair<int, int>{
        CodeAnalysis::getNodeNumInputs(left, scopeStack, tree),
        CodeAnalysis::getNodeNumOutputs(left, scopeStack, tree)});

    if (right->getNodeType() == AST::Stream) {
      subStream = std::static_pointer_cast<StreamNode>(right);
      left = subStream->getLeft();
      right = subStream->getRight();
    } else {
      if (right->getNodeType() == AST::Function) {
        ASTNode newFunctions = expandFunctionFromProperties(
            std::static_pointer_cast<FunctionNode>(right), scopeStack, tree);
        if (newFunctions) {
          subStream->setRight(newFunctions);
          right = subStream->getRight();
        }
      }
      IOs.emplace_back(std::pair<int, int>{
          CodeAnalysis::getNodeNumInputs(right, scopeStack, tree),
          CodeAnalysis::getNodeNumOutputs(right, scopeStack, tree)});
      right = nullptr;
    }
  }
  // Now go through comparing number of outputs to number of inputs to figure
  // out if we need to duplicate any members
  std::vector<int> numCopies;
  numCopies.push_back(1);
  for (size_t i = 1; i < IOs.size(); ++i) {
    int numPrevOut = IOs[i - 1].second * numCopies.back();
    int numCurIn = IOs[i].first;
    if (numPrevOut == -1) { // Found undeclared block
      numCopies.push_back(1);
      continue;
    }
    if (numPrevOut > numCurIn) { // Need to clone next
      if (numCurIn > 0) {
        if (numPrevOut / (float)numCurIn == numPrevOut / numCurIn) {
          numCopies.push_back(numPrevOut / numCurIn);
        } else {
          // Stream size mismatch. Stop expansion. The error will be reported
          // later by CodeValidator.
          numCopies.push_back(1);
          std::cerr << "Could not clone "
                    << IOs[i - 1].second * numCopies.back() << " outputs into "
                    << IOs[i].first << " inputs." << std::endl;
        }
      } else {
        // Cloning with size 1
        numCopies.push_back(1);
      }
    } else if (numPrevOut < numCurIn &&
               numPrevOut > 0) { // Need to clone all existing left side
      if (numCurIn / (float)numPrevOut == numCurIn / numPrevOut) {
        //                        int newNumCopies = numCurIn/numPrevOut;
        //                        for(int i = 0; i < numCopies.size(); ++i) {
        //                            numCopies[i] *= newNumCopies;
        //                        }
        // Should not be expanded but connected recursively and interleaved
        // (according to the rules of the language)
        numCopies.push_back(1);
      } else {
        // Stream size mismatch. Stop expansion. The error will be reported
        // later by CodeValidator.
        std::cerr << "Could not clone " << IOs[i - 1].second << " outputs into "
                  << IOs[i].first << " inputs." << std::endl;
        numCopies.push_back(1);
      }

    } else { // Size match, no need to clone
      numCopies.push_back(1);
    }
  }
  if (numCopies.size() ==
      IOs.size()) { // Expansion calculation went fine, so expand
                    //                std::cerr << "Will expand";
    expandStreamToSizes(stream, numCopies, -1, scopeStack);
  }
}

void CodeResolver::processDeclarations() {
  //    For block and bundle declarations move streams and blocks outside
  //    properties to the "streams" and "blocks" properties

  std::function<void(std::vector<ASTNode>)> processDeclarationsForTree =
      [&](std::vector<ASTNode> children) {
        for (const ASTNode &node : children) {
          if (node->getNodeType() == AST::Declaration ||
              node->getNodeType() == AST::ArrayDeclaration) {
            std::shared_ptr<DeclarationNode> decl =
                std::static_pointer_cast<DeclarationNode>(node);
            std::vector<ASTNode> toDelete;
            for (auto prop : decl->getChildren()) {
              if (prop->getNodeType() == AST::Stream) {
                toDelete.push_back(prop);
                if (!decl->getPropertyValue("streams") ||
                    decl->getPropertyValue("streams")->getNodeType() ==
                        AST::None) {
                  auto streamList = std::make_shared<ListNode>(
                      prop, prop->getFilename().c_str(), prop->getLine());
                  decl->setPropertyValue("streams", streamList);
                } else if (decl->getPropertyValue("streams")->getNodeType() !=
                           AST::List) {
                  auto streamList = std::make_shared<ListNode>(
                      decl->getPropertyValue("streams"), __FILE__, __LINE__);
                  streamList->addChild(node);
                  decl->setPropertyValue("streams", streamList);
                } else {
                  decl->getPropertyValue("streams")->addChild(node);
                }
              } else if (prop->getNodeType() == AST::Declaration ||
                         prop->getNodeType() == AST::ArrayDeclaration) {
                toDelete.push_back(prop);
                if (!decl->getPropertyValue("blocks") ||
                    decl->getPropertyValue("blocks")->getNodeType() ==
                        AST::None) {
                  auto streamList = std::make_shared<ListNode>(
                      prop, prop->getFilename().c_str(), prop->getLine());
                  decl->setPropertyValue("blocks", streamList);
                } else if (decl->getPropertyValue("blocks")->getNodeType() !=
                           AST::List) {
                  auto streamList = std::make_shared<ListNode>(
                      decl->getPropertyValue("blocks"), __FILE__, __LINE__);
                  streamList->addChild(node);
                  decl->setPropertyValue("blocks", streamList);
                } else {
                  decl->getPropertyValue("blocks")->addChild(node);
                }
              } else if (prop->getNodeType() == AST::Property) {
                if (std::static_pointer_cast<PropertyNode>(prop)
                        ->getValue()
                        ->getNodeType() == AST::List) {
                  processDeclarationsForTree(
                      std::static_pointer_cast<PropertyNode>(prop)
                          ->getValue()
                          ->getChildren());
                } else {
                  processDeclarationsForTree(std::vector<ASTNode>{
                      std::static_pointer_cast<PropertyNode>(prop)
                          ->getValue()});
                }
              }
            }
            for (const auto &deleteme : toDelete) {
              decl->removeProperty(deleteme);
            }
          } else {
            processDeclarationsForTree(node->getChildren());
          }
        }
      };
  processDeclarationsForTree(m_tree->getChildren());
}

static ASTNode expandRangesInASTNode(ASTNode node, const ScopeStack &scope,
                                     ASTNode tree) {
  if (!node)
    return nullptr;

  if (node->getNodeType() == AST::GeneratorRange) {
    auto genRange = std::static_pointer_cast<GeneratorRangeNode>(node);
    int64_t start = ASTFunctions::evaluateConstInteger(genRange->startIndex(),
                                                       scope, tree, nullptr);
    int64_t end = ASTFunctions::evaluateConstInteger(genRange->endIndex(),
                                                     scope, tree, nullptr);
    int64_t step = 1;
    if (genRange->stepIndex()) {
      step = ASTFunctions::evaluateConstInteger(genRange->stepIndex(), scope,
                                                tree, nullptr);
    }
    auto expandedList = std::make_shared<ListNode>(
        nullptr, node->getFilename().c_str(), node->getLine());
    if (step > 0 && end >= start) {
      for (int64_t i = start; i <= end; i += step) {
        expandedList->addChild(std::make_shared<ValueNode>(
            i, node->getFilename().c_str(), node->getLine()));
      }
    } else if (step < 0 && end <= start) {
      for (int64_t i = start; i >= end; i += step) {
        expandedList->addChild(std::make_shared<ValueNode>(
            i, node->getFilename().c_str(), node->getLine()));
      }
    }
    return expandedList;
  }

  if (node->getNodeType() == AST::Array) {
    auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
    auto indexList = arrayNode->index();
    if (!indexList || indexList->getChildren().empty()) {
      return node;
    }

    auto resolveArrayElement =
        [&](const std::string &arrayName, ASTNode idxNode,
            const std::vector<std::string> &ns) -> ASTNode {
      std::vector<LangError> evalErrors;
      int64_t evaluatedIdx =
          ASTFunctions::evaluateConstInteger(idxNode, scope, tree, &evalErrors);
      if (evalErrors.empty()) {
        if (evaluatedIdx < 0) {
          auto decl =
              ASTQuery::findDeclarationByName(arrayName, scope, tree, ns);
          if (decl) {
            std::string entityType = decl->getEntityType();
            if (entityType == "switch" ||
                CodeAnalysis::getDataTypeForDeclaration(decl, scope, tree) ==
                    "_SwitchType") {
              return std::make_shared<ValueNode>(
                  false, node->getFilename().c_str(), node->getLine());
            } else {
              return std::make_shared<ValueNode>(
                  (int64_t)0, node->getFilename().c_str(), node->getLine());
            }
          }
          return std::make_shared<ValueNode>(false, node->getFilename().c_str(),
                                             node->getLine());
        } else {
          auto singleIdxList = std::make_shared<ListNode>(
              std::make_shared<ValueNode>(
                  evaluatedIdx, node->getFilename().c_str(), node->getLine()),
              node->getFilename().c_str(), node->getLine());
          return std::make_shared<ArrayNode>(arrayName, singleIdxList,
                                             node->getFilename().c_str(),
                                             node->getLine(), ns);
        }
      }
      auto singleIdxList = std::make_shared<ListNode>(
          idxNode->deepCopy(), node->getFilename().c_str(), node->getLine());
      return std::make_shared<ArrayNode>(arrayName, singleIdxList,
                                         node->getFilename().c_str(),
                                         node->getLine(), ns);
    };

    bool hasCollection = false;
    std::vector<ASTNode> expandedIndexItems;
    for (const auto &idx : indexList->getChildren()) {
      if (idx->getNodeType() == AST::Range) {
        hasCollection = true;
        auto range = std::static_pointer_cast<RangeNode>(idx);
        int64_t start = ASTFunctions::evaluateConstInteger(
            range->startIndex(), scope, tree, nullptr);
        int64_t end = ASTFunctions::evaluateConstInteger(range->endIndex(),
                                                         scope, tree, nullptr);
        if (end >= start) {
          for (int64_t i = start; i <= end; ++i) {
            expandedIndexItems.push_back(std::make_shared<ValueNode>(
                i, node->getFilename().c_str(), node->getLine()));
          }
        }
      } else {
        auto expIdx = expandRangesInASTNode(idx, scope, tree);
        if (expIdx && expIdx->getNodeType() == AST::List) {
          hasCollection = true;
          for (const auto &sub : expIdx->getChildren()) {
            expandedIndexItems.push_back(sub);
          }
        } else {
          expandedIndexItems.push_back(expIdx ? expIdx : idx);
        }
      }
    }

    if (hasCollection) {
      auto expandedList = std::make_shared<ListNode>(
          nullptr, node->getFilename().c_str(), node->getLine());
      for (const auto &idxItem : expandedIndexItems) {
        expandedList->addChild(resolveArrayElement(
            arrayNode->getName(), idxItem, arrayNode->getNamespaceList()));
      }
      return expandedList;
    } else if (expandedIndexItems.size() == 1) {
      std::vector<LangError> evalErrors;
      int64_t evaluatedIdx = ASTFunctions::evaluateConstInteger(
          expandedIndexItems[0], scope, tree, &evalErrors);
      if (evalErrors.empty() && evaluatedIdx < 0) {
        auto decl = ASTQuery::findDeclarationByName(
            arrayNode->getName(), scope, tree, arrayNode->getNamespaceList());
        if (decl) {
          std::string entityType = decl->getEntityType();
          if (entityType == "switch" ||
              CodeAnalysis::getDataTypeForDeclaration(decl, scope, tree) ==
                  "_SwitchType") {
            return std::make_shared<ValueNode>(
                false, node->getFilename().c_str(), node->getLine());
          } else {
            return std::make_shared<ValueNode>(
                (int64_t)0, node->getFilename().c_str(), node->getLine());
          }
        }
        return std::make_shared<ValueNode>(false, node->getFilename().c_str(),
                                           node->getLine());
      }
    }

    return node;
  }

  if (node->getNodeType() == AST::List) {
    auto listNode = std::static_pointer_cast<ListNode>(node);
    auto newList = std::make_shared<ListNode>(
        nullptr, node->getFilename().c_str(), node->getLine());
    for (const auto &child : listNode->getChildren()) {
      auto expChild = expandRangesInASTNode(child, scope, tree);
      if (child->getNodeType() == AST::GeneratorRange && expChild &&
          expChild->getNodeType() == AST::List) {
        for (const auto &item : expChild->getChildren()) {
          newList->addChild(item);
        }
      } else {
        newList->addChild(expChild);
      }
    }
    return newList;
  }

  return node;
}

// Expands a stream through a multi-instance function (F[]()) into one stream
// per instance. Bundles the caller must declare (name, size) to hold the
// instance outputs are appended to newBundles.
static std::vector<ASTNode> expandSingleIndexedStream(
    std::shared_ptr<StreamNode> stream, const ScopeStack &scope, ASTNode tree,
    std::vector<std::pair<std::string, int64_t>> &newBundles) {
  if (!stream)
    return {};

  ASTNode left = expandRangesInASTNode(stream->getLeft(), scope, tree);
  ASTNode right = stream->getRight();

  std::shared_ptr<FunctionNode> funcNode = nullptr;
  ASTNode outNode = nullptr;

  if (right) {
    if (right->getNodeType() == AST::Stream) {
      auto subStream = std::static_pointer_cast<StreamNode>(right);
      if (subStream->getLeft() &&
          subStream->getLeft()->getNodeType() == AST::Function) {
        funcNode = std::static_pointer_cast<FunctionNode>(subStream->getLeft());
        outNode = expandRangesInASTNode(subStream->getRight(), scope, tree);
      }
    } else if (right->getNodeType() == AST::Function) {
      funcNode = std::static_pointer_cast<FunctionNode>(right);
      outNode = nullptr;
    }
  }

  if (!funcNode) {
    stream->setLeft(left);
    if (right && right->getNodeType() == AST::Stream) {
      auto subStream = std::static_pointer_cast<StreamNode>(right);
      subStream->setRight(
          expandRangesInASTNode(subStream->getRight(), scope, tree));
    } else if (right) {
      stream->setRight(expandRangesInASTNode(right, scope, tree));
    }
    return {};
  }

  auto multiInstProp = funcNode->getCompilerProperty("_multiInstance");
  if (!multiInstProp) {
    multiInstProp = funcNode->getCompilerProperty("_arrayIndices");
  }
  if (!multiInstProp) {
    if (left && left->getNodeType() == AST::List) {
      auto inList = std::static_pointer_cast<ListNode>(left);
      bool hasNestedList = false;
      for (const auto &c : inList->getChildren()) {
        if (c && c->getNodeType() == AST::List) {
          hasNestedList = true;
          break;
        }
      }
      if (hasNestedList) {
        auto flatList = std::make_shared<ListNode>(
            nullptr, inList->getFilename().c_str(), inList->getLine());
        std::function<void(ASTNode)> flatten = [&](ASTNode item) {
          if (item && item->getNodeType() == AST::List) {
            for (const auto &child : item->getChildren()) {
              flatten(child);
            }
          } else if (item) {
            flatList->addChild(item);
          }
        };
        flatten(inList);
        left = flatList;
      }
    }
    stream->setLeft(left);
    if (outNode) {
      auto subStream = std::static_pointer_cast<StreamNode>(right);
      subStream->setRight(outNode);
    }
    return {};
  }

  // The stream may continue after the instance outputs, e.g.
  //   In >> F[]() >> Out[0:8] >> G() >> X;  or  In >> F[]() >> G() >> X;
  // Each instance writes one element of Out (or of a new bundle when a
  // function follows), and one more stream continues from the whole bundle.
  ASTNode tailHead = nullptr;
  ASTNode tailRest = nullptr;
  if (outNode && outNode->getNodeType() == AST::Stream) {
    auto tail = std::static_pointer_cast<StreamNode>(outNode);
    tailHead = tail->getLeft();
    tailRest = tail->getRight();
  } else if (outNode && outNode->getNodeType() == AST::Function) {
    tailHead = outNode;
  }
  const bool outputsToBundle =
      tailHead && tailHead->getNodeType() == AST::Function;
  if (tailHead) {
    outNode = outputsToBundle ? nullptr
                              : expandRangesInASTNode(tailHead, scope, tree);
  }

  int64_t N = 0;
  if (left && left->getNodeType() == AST::List) {
    auto inList = std::static_pointer_cast<ListNode>(left);
    for (const auto &item : inList->getChildren()) {
      if (item->getNodeType() == AST::List) {
        int s = static_cast<int>(item->getChildren().size());
        if (s > N)
          N = s;
      }
    }
    if (N <= 0) {
      auto funcDecl = ASTQuery::findDeclarationByName(
          funcNode->getName(), scope, tree, funcNode->getNamespaceList());
      int numDeclInputs = 1;
      if (funcDecl) {
        numDeclInputs =
            CodeAnalysis::getTypeNumInputs(funcDecl, scope, tree, nullptr);
      }
      if (numDeclInputs >= 0 && numDeclInputs <= 1) {
        N = static_cast<int>(inList->getChildren().size());
      }
    }
  } else if (left && left->getNodeType() == AST::Array) {
    // Single bundle on left (e.g. InA[0:2] before list conversion)
    int s = CodeAnalysis::getNodeNumOutputs(left, scope, tree, nullptr);
    if (s > N)
      N = s;
  }

  if (N <= 0 && outNode && outNode->getNodeType() == AST::List) {
    auto outList = std::static_pointer_cast<ListNode>(outNode);
    if (outList->getChildren().size() == 1 &&
        outList->getChildren()[0]->getNodeType() == AST::List) {
      N = static_cast<int>(outList->getChildren()[0]->getChildren().size());
    } else {
      N = static_cast<int>(outList->getChildren().size());
    }
  }

  if (N <= 0) {
    N = 1;
  }

  std::vector<ASTNode> instanceInputs(N);
  if (left && left->getNodeType() == AST::List) {
    auto inList = std::static_pointer_cast<ListNode>(left);
    const auto &inChildren = inList->getChildren();

    bool isFlatList = false;
    if (static_cast<int>(inChildren.size()) == N) {
      bool allScalars = true;
      for (const auto &c : inChildren) {
        if (c->getNodeType() == AST::List &&
            static_cast<int>(c->getChildren().size()) == N) {
          allScalars = false;
          break;
        }
      }
      if (allScalars) {
        auto funcDecl = ASTQuery::findDeclarationByName(
            funcNode->getName(), scope, tree, funcNode->getNamespaceList());
        int numDeclInputs = 1;
        if (funcDecl) {
          numDeclInputs =
              CodeAnalysis::getTypeNumInputs(funcDecl, scope, tree, nullptr);
        }
        if (numDeclInputs >= 0 && numDeclInputs <= 1) {
          isFlatList = true;
        }
      }
    }

    if (isFlatList) {
      for (int i = 0; i < N; ++i) {
        instanceInputs[i] = inChildren[i]->deepCopy();
      }
    } else {
      for (int i = 0; i < N; ++i) {
        auto tupleList = std::make_shared<ListNode>(
            nullptr, stream->getFilename().c_str(), stream->getLine());
        for (const auto &c : inChildren) {
          if (c->getNodeType() == AST::List &&
              static_cast<int>(c->getChildren().size()) == N) {
            tupleList->addChild(c->getChildren()[i]->deepCopy());
          } else {
            tupleList->addChild(c->deepCopy());
          }
        }
        instanceInputs[i] = tupleList;
      }
    }
  } else if (left) {
    for (int i = 0; i < N; ++i) {
      instanceInputs[i] = left->deepCopy();
    }
  }

  std::vector<ASTNode> instanceOutputs(N);
  if (outNode) {
    if (outNode->getNodeType() == AST::List) {
      auto outList = std::static_pointer_cast<ListNode>(outNode);
      if (static_cast<int>(outList->getChildren().size()) == N) {
        for (int i = 0; i < N; ++i) {
          instanceOutputs[i] = outList->getChildren()[i]->deepCopy();
        }
      } else if (outList->getChildren().size() == 1 &&
                 outList->getChildren()[0]->getNodeType() == AST::List &&
                 static_cast<int>(
                     outList->getChildren()[0]->getChildren().size()) == N) {
        for (int i = 0; i < N; ++i) {
          instanceOutputs[i] =
              outList->getChildren()[0]->getChildren()[i]->deepCopy();
        }
      } else {
        for (int i = 0; i < N; ++i) {
          instanceOutputs[i] = outNode->deepCopy();
        }
      }
    } else {
      for (int i = 0; i < N; ++i) {
        instanceOutputs[i] = outNode->deepCopy();
      }
    }
  }

  static int s_parallelGroupId = 0;
  int64_t currentGroupId = ++s_parallelGroupId;
  std::string originFuncName = funcNode->getName();

  // Bundle holding the instance outputs when a function follows them
  std::shared_ptr<ListNode> bundleElements;
  if (outputsToBundle) {
    std::string bundleName = "__" + originFuncName + "_Out" +
                             std::to_string(currentGroupId);
    newBundles.push_back({bundleName, N});
    bundleElements = std::make_shared<ListNode>(
        nullptr, stream->getFilename().c_str(), stream->getLine());
    for (int64_t i = 0; i < N; ++i) {
      ASTNode element;
      if (N == 1) {
        // createSignalDeclaration declares a scalar for size 1
        element = std::make_shared<EntityNode>(
            bundleName, stream->getFilename().c_str(), stream->getLine());
      } else {
        auto indexList = std::make_shared<ListNode>(
            std::make_shared<ValueNode>(i, stream->getFilename().c_str(),
                                        stream->getLine()),
            stream->getFilename().c_str(), stream->getLine());
        element = std::make_shared<ArrayNode>(bundleName, indexList,
                                              stream->getFilename().c_str(),
                                              stream->getLine());
      }
      instanceOutputs[i] = element;
      bundleElements->addChild(element->deepCopy());
    }
  }

  std::vector<ASTNode> expandedStreams;
  expandedStreams.reserve(N);
  for (int64_t i = 0; i < N; ++i) {
    auto funcClone =
        std::static_pointer_cast<FunctionNode>(funcNode->deepCopy());
    funcClone->removeCompilerProperty("_arrayIndices");
    funcClone->removeCompilerProperty("_multiInstance");
    funcClone->setCompilerProperty(
        "_parallelGroup",
        std::make_shared<ValueNode>(
            currentGroupId, stream->getFilename().c_str(), stream->getLine()));
    funcClone->setCompilerProperty(
        "_instanceIndex",
        std::make_shared<ValueNode>(i, stream->getFilename().c_str(),
                                    stream->getLine()));
    funcClone->setCompilerProperty(
        "_instanceCount",
        std::make_shared<ValueNode>(N, stream->getFilename().c_str(),
                                    stream->getLine()));
    funcClone->setCompilerProperty(
        "_unrolledFrom",
        std::make_shared<ValueNode>(
            originFuncName, stream->getFilename().c_str(), stream->getLine()));

    ASTNode singleStream;
    if (instanceOutputs[i]) {
      auto sub = std::make_shared<StreamNode>(funcClone, instanceOutputs[i],
                                              stream->getFilename().c_str(),
                                              stream->getLine());
      singleStream = std::make_shared<StreamNode>(instanceInputs[i], sub,
                                                  stream->getFilename().c_str(),
                                                  stream->getLine());
    } else {
      singleStream = std::make_shared<StreamNode>(instanceInputs[i], funcClone,
                                                  stream->getFilename().c_str(),
                                                  stream->getLine());
    }

    singleStream->setCompilerProperty(
        "_parallelGroup",
        std::make_shared<ValueNode>(
            currentGroupId, stream->getFilename().c_str(), stream->getLine()));
    singleStream->setCompilerProperty(
        "_instanceIndex",
        std::make_shared<ValueNode>(i, stream->getFilename().c_str(),
                                    stream->getLine()));
    singleStream->setCompilerProperty(
        "_instanceCount",
        std::make_shared<ValueNode>(N, stream->getFilename().c_str(),
                                    stream->getLine()));
    singleStream->setCompilerProperty(
        "_unrolledFrom",
        std::make_shared<ValueNode>(
            originFuncName, stream->getFilename().c_str(), stream->getLine()));

    expandedStreams.push_back(singleStream);
  }

  // Continue the stream from the whole output bundle
  if (outputsToBundle || tailRest) {
    std::shared_ptr<StreamNode> continuation;
    if (outputsToBundle) {
      ASTNode rest = tailHead;
      if (tailRest) {
        rest = std::make_shared<StreamNode>(tailHead, tailRest,
                                            stream->getFilename().c_str(),
                                            stream->getLine());
      }
      continuation = std::make_shared<StreamNode>(
          bundleElements, rest, stream->getFilename().c_str(),
          stream->getLine());
    } else {
      continuation = std::make_shared<StreamNode>(
          tailHead, tailRest, stream->getFilename().c_str(),
          stream->getLine());
    }
    // The continuation may itself go through a multi-instance function
    auto expandedContinuation =
        expandSingleIndexedStream(continuation, scope, tree, newBundles);
    if (expandedContinuation.empty()) {
      expandedStreams.push_back(continuation);
    } else {
      expandedStreams.insert(expandedStreams.end(),
                             expandedContinuation.begin(),
                             expandedContinuation.end());
    }
  }

  return expandedStreams;
}

void CodeResolver::expandIndexedStreams() {
  // Bundles for instance outputs are declared in the blocks of the
  // declaration owning the streams, or in the root if it has no blocks.
  std::vector<ASTNode> rootDeclarations;
  auto declareBundles =
      [&](const std::vector<std::pair<std::string, int64_t>> &bundles,
          std::shared_ptr<DeclarationNode> owner) {
        for (const auto &[name, size] : bundles) {
          auto bundle = createSignalDeclaration(name, size, {}, m_tree);
          auto blocks = owner ? owner->getPropertyValue("blocks") : nullptr;
          if (blocks && blocks->getNodeType() == AST::List) {
            blocks->addChild(bundle);
          } else {
            rootDeclarations.push_back(bundle);
          }
        }
      };

  std::function<void(ASTNode, std::shared_ptr<DeclarationNode>)>
      processContainer = [&](ASTNode container,
                             std::shared_ptr<DeclarationNode> owner) {
    if (!container)
      return;
    auto children = container->getChildren();
    std::vector<ASTNode> newChildren;
    bool modified = false;

    for (const auto &child : children) {
      if (child->getNodeType() == AST::Stream) {
        auto stream = std::static_pointer_cast<StreamNode>(child);
        std::vector<std::pair<std::string, int64_t>> newBundles;
        auto expanded =
            expandSingleIndexedStream(stream, {}, m_tree, newBundles);
        declareBundles(newBundles, owner);
        if (!expanded.empty()) {
          newChildren.insert(newChildren.end(), expanded.begin(),
                             expanded.end());
          modified = true;
        } else {
          newChildren.push_back(child);
        }
      } else {
        newChildren.push_back(child);
        if (child->getNodeType() == AST::Declaration ||
            child->getNodeType() == AST::ArrayDeclaration) {
          auto decl = std::static_pointer_cast<DeclarationNode>(child);
          const std::vector<std::string> streamProps = {
              "streams",   "updateGuard", "onEntry",
              "onProcess", "onExit",      "onTransition"};
          for (const auto &propName : streamProps) {
            auto streams = decl->getPropertyValue(propName);
            if (streams) {
              if (streams->getNodeType() == AST::List) {
                processContainer(streams, decl);
              } else if (streams->getNodeType() == AST::Stream) {
                auto stream = std::static_pointer_cast<StreamNode>(streams);
                std::vector<std::pair<std::string, int64_t>> newBundles;
                auto expanded =
                    expandSingleIndexedStream(stream, {}, m_tree, newBundles);
                declareBundles(newBundles, decl);
                if (!expanded.empty()) {
                  auto newStreamsList = std::make_shared<ListNode>(
                      nullptr, streams->getFilename().c_str(),
                      streams->getLine());
                  for (const auto &s : expanded) {
                    newStreamsList->addChild(s);
                  }
                  decl->replacePropertyValue(propName, newStreamsList);
                }
              }
            }
          }
          for (const auto &p : decl->getProperties()) {
            if (p->getValue() && p->getValue()->getNodeType() == AST::List) {
              for (const auto &item : p->getValue()->getChildren()) {
                if (item->getNodeType() == AST::Declaration) {
                  processContainer(p->getValue(), decl);
                  break;
                }
              }
            }
          }
        }
      }
    }
    if (modified) {
      container->setChildren(newChildren);
    }
  };

  processContainer(m_tree, nullptr);
  for (const auto &decl : rootDeclarations) {
    m_tree->addChild(decl);
  }
}

// Expands generator ranges in list defaults, e.g. [0..2, 7] -> [0, 1, 2, 7],
// so code generation gets one value per array element. Other properties keep
// their ranges.
void CodeResolver::expandDefaultRanges() {
  std::function<void(ASTNode)> processContainer = [&](ASTNode container) {
    if (!container)
      return;
    for (const auto &child : container->getChildren()) {
      if (child->getNodeType() != AST::Declaration &&
          child->getNodeType() != AST::ArrayDeclaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      auto defaultValue = decl->getPropertyValue("default");
      if (defaultValue && defaultValue->getNodeType() == AST::List) {
        bool hasRange = false;
        for (const auto &item : defaultValue->getChildren()) {
          if (item->getNodeType() == AST::GeneratorRange) {
            hasRange = true;
            break;
          }
        }
        if (hasRange) {
          auto expanded = std::make_shared<ListNode>(
              nullptr, defaultValue->getFilename().c_str(),
              defaultValue->getLine());
          for (const auto &item : defaultValue->getChildren()) {
            if (item->getNodeType() == AST::GeneratorRange) {
              auto values = expandRangesInASTNode(item, {}, m_tree);
              for (const auto &value : values->getChildren()) {
                expanded->addChild(value);
              }
            } else {
              expanded->addChild(item);
            }
          }
          decl->replacePropertyValue("default", expanded);
        }
      }
      // Declarations nested in blocks
      for (const auto &p : decl->getProperties()) {
        if (p->getValue() && p->getValue()->getNodeType() == AST::List) {
          processContainer(p->getValue());
        }
      }
    }
  };
  processContainer(m_tree);
}

void CodeResolver::expandParallel() {
  std::vector<ASTNode> children = m_tree->getChildren();
  for (const ASTNode &node : children) {
    ScopeStack scopeStack;
    if (node->getNodeType() == AST::Stream) {
      std::shared_ptr<StreamNode> stream =
          std::static_pointer_cast<StreamNode>(node);
      expandParallelStream(stream, scopeStack, m_tree);
      // We need to process unknown symbols to make sure we can expand the
      // following streams to the right size.
      std::vector<ASTNode> declarations =
          declareUnknownStreamSymbols(stream, nullptr, ScopeStack(), m_tree);
      for (const ASTNode &decl : declarations) {
        m_tree->addChild(decl);
      }
    }
  }
}

void CodeResolver::expandStreamToSizes(std::shared_ptr<StreamNode> stream,
                                       std::vector<int> &neededCopies,
                                       int previousOutSize,
                                       ScopeStack scopeStack) {
  ASTNode left = stream->getLeft();
  //    int leftSize = CodeValidator::getNodeSize(left, scopeStack, m_tree);
  if (previousOutSize == -1) {
    previousOutSize = 1;
  }

  if (left->getNodeType() == AST::Function) {
    int numCopies = neededCopies.front();
    //         if (leftSize < 0 && left->getNodeType() == AST::Block) {
    //            std::vector<ASTNode > newDeclaration =
    //            declareUnknownName(static_pointer_cast<BlockNode>(left),
    //            previousOutSize, scopeStack, m_tree); for(ASTNode
    //            decl:newDeclaration) {
    //                m_tree->addChild(decl);
    //            }
    //        }
    if (numCopies > 1 && left->getNodeType() == AST::Function) {
      std::shared_ptr<ListNode> newLeft = std::make_shared<ListNode>(
          left, left->getFilename().data(), left->getLine());
      for (int i = 1; i < numCopies; i++) {
        newLeft->addChild(left->deepCopy());
      }
      stream->setLeft(newLeft);
    }
  }
  previousOutSize =
      CodeAnalysis::getNodeNumOutputs(stream->getLeft(), scopeStack, m_tree);
  if (previousOutSize < 0) {
    previousOutSize = 1;
  }
  neededCopies.erase(neededCopies.begin());
  ASTNode right = stream->getRight();
  if (right->getNodeType() == AST::Stream) {
    expandStreamToSizes(std::static_pointer_cast<StreamNode>(right),
                        neededCopies, previousOutSize, scopeStack);
  } else {
    int rightSize = ASTQuery::getNodeSize(right, scopeStack, m_tree);
    if (right->getNodeType() == AST::Entity ||
        right->getNodeType() == AST::Function) {
      int numCopies = neededCopies.front();
      if (rightSize < 0 && right->getNodeType() == AST::Entity) {
        std::vector<ASTNode> newDeclaration =
            declareUnknownName(std::static_pointer_cast<EntityNode>(right),
                               previousOutSize, scopeStack, m_tree);
        for (const ASTNode &decl : newDeclaration) {
          m_tree->addChild(decl);
        }
      } else if (numCopies > 1 && right->getNodeType() ==
                                      AST::Function) { // Only functions should
                                                       // be expanded this way
        std::shared_ptr<ListNode> newRight = std::make_shared<ListNode>(
            right, right->getFilename().data(), right->getLine());
        for (int i = 1; i < numCopies; i++) {
          newRight->addChild(right);
        }
        stream->setRight(newRight);
      }
    }
    neededCopies.erase(neededCopies.begin());
    assert(neededCopies.size() ==
           0); // This is the end of the stream there should be no sizes left
  }
}

ASTNode CodeResolver::expandFunctionFromProperties(
    std::shared_ptr<FunctionNode> func, ScopeStack scopeStack, ASTNode tree) {
  std::shared_ptr<ListNode> newFunctions = nullptr;
  //    int dataSize = CodeValidator::getFunctionDataSize(func, scopeStack,
  //    tree, errors);
  int dataSize = CodeAnalysis::getFunctionNumInstances(func, scopeStack, tree);
  if (dataSize > 1) {
    std::vector<std::shared_ptr<PropertyNode>> props = func->getProperties();
    newFunctions = std::make_shared<ListNode>(
        nullptr, func->getFilename().c_str(), func->getLine());
    for (int i = 0; i < dataSize;
         ++i) { // FIXME this assumes each function takes a single input. Need
                // to check the actual input size.
      newFunctions->addChild(func->deepCopy());
    }
    for (const auto &prop : props) {
      ASTNode value = prop->getValue();
      int numOuts = CodeAnalysis::getNodeNumOutputs(value, scopeStack, tree);
      if (numOuts != 1 && numOuts != dataSize) {
        //        LangError error;
        //        error.type = LangError::ArraySizeMismatch;
        //        error.filename = func->getFilename();
        //        error.lineNumber = func->getLine();
        //        error.errorTokens.push_back(func->getName());
        //        error.errorTokens.push_back(QString::number(numOuts).toStdString());
        //        error.errorTokens.push_back(QString::number(dataSize).toStdString());
        //        errors << error;
        return nullptr;
      }
      if (numOuts == 1) { // Single value given, duplicate for all copies.
        for (const ASTNode &newFunction : newFunctions->getChildren()) {
          std::static_pointer_cast<FunctionNode>(newFunction)
              ->replacePropertyValue(prop->getName(),
                                     prop->getValue()->deepCopy());
        }
      } else {
        if (value->getNodeType() == AST::Array) {
          // FIXME write support for ranges

        } else if (value->getNodeType() == AST::Entity) {
          EntityNode *name = static_cast<EntityNode *>(value.get());
          std::shared_ptr<DeclarationNode> decl =
              ASTQuery::findDeclarationByName(name->getName(), scopeStack, tree,
                                              name->getNamespaceList());
          int size = ASTQuery::getEntityDeclaredSize(decl, scopeStack, tree);
          assert(size == dataSize);
          for (int i = 0; i < size; ++i) {
            std::shared_ptr<ListNode> indexList = std::make_shared<ListNode>(
                std::make_shared<ValueNode>(
                    (int64_t)i, prop->getFilename().c_str(), prop->getLine()),
                prop->getFilename().c_str(), prop->getLine());
            std::shared_ptr<ArrayNode> newArrayNode =
                std::make_shared<ArrayNode>(name->getName(), indexList,
                                            prop->getFilename().c_str(),
                                            prop->getLine());
            std::static_pointer_cast<FunctionNode>(
                newFunctions->getChildren()[i])
                ->replacePropertyValue(prop->getName(), newArrayNode);
          }
        } else if (value->getNodeType() == AST::List) {
          // FIXME we need to split the list according to the expected size.
          // This currently assumes size == 1
          std::vector<ASTNode> values =
              std::static_pointer_cast<ListNode>(value)->getChildren();
          std::vector<ASTNode> functions = newFunctions->getChildren();
          assert(values.size() == functions.size());
          for (size_t i = 0; i < (size_t)dataSize; ++i) {
            std::shared_ptr<PropertyNode> newProp =
                std::static_pointer_cast<PropertyNode>(prop);
            std::static_pointer_cast<FunctionNode>(functions[i])
                ->setPropertyValue(newProp->getName(), values[i]->deepCopy());
          }
        } else {
          std::cerr << "Error. Don't know how to expand property." << std::endl;
        }
      }
    }
  }
  return newFunctions;
}

void CodeResolver::processDomains() {
  // Fill missing domain information (propagate domains)
  // First we need to traverse the streams backwards to make sure we propagate
  // the domains from the furthest point down the line
  std::vector<ASTNode> children = m_tree->getChildren();
  std::vector<ASTNode>::reverse_iterator rit = children.rbegin();
  ScopeStack scopeStack; // = QVector<AST*>::fromStdVector(children);

  ASTNode domainBlock = m_system->getPlatformDomain();
  mContextDomainStack.push_back(domainBlock);
  while (rit != children.rend()) {
    ASTNode node = *rit;
    propagateDomainsForNode(node, scopeStack);
    rit++;
  }
  // Now split streams when there is a domain change
  std::vector<ASTNode> new_tree;
  for (const ASTNode &node : m_tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      std::vector<ASTNode> streams = sliceStreamByDomain(
          std::static_pointer_cast<StreamNode>(node), ScopeStack());
      for (const ASTNode &stream : streams) {
        new_tree.push_back(stream);
      }
    } else if (node->getNodeType() == AST::Declaration) {
      std::shared_ptr<DeclarationNode> module =
          std::static_pointer_cast<DeclarationNode>(node);
      sliceDomainsInNode(module, ScopeStack());
      new_tree.push_back(module);
    } else {
      new_tree.push_back(node);
    }
  }
  m_tree->setChildren(new_tree);
}

void CodeResolver::setPortEntityAttributes(const ScopeStack &scope,
                                           ASTNode tree) {
  std::vector<ASTNode> children = m_tree->getChildren();
  // First set null types for port blocks inside modules and resolve streams
  // in CodeGenerator_
  for (const ASTNode &node : children) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      auto typeDecl = ASTQuery::findTypeDeclaration(decl, {}, m_tree);
      if (ASTQuery::isCodeGenerator(typeDecl, scope, tree)) {
        setPortEntityAttributesForDeclaration(decl, {}, m_tree);
        auto blocks = decl->getPropertyValue("blocks");
        for (const auto &node : blocks->getChildren()) {
          if (node->getNodeType() == AST::Declaration) {
            auto decl = std::static_pointer_cast<DeclarationNode>(node);
            auto typeDecl = ASTQuery::findTypeDeclaration(decl, {}, m_tree);
            if (ASTQuery::isCodeGenerator(typeDecl, scope, tree)) {
              setPortEntityAttributesForDeclaration(
                  decl, {{nullptr, blocks->getChildren()}}, m_tree);
            }
          }
        }
      }
    }
  }
}

void CodeResolver::resolveAttributes(const ScopeStack &scope, ASTNode tree) {
  // FIXME we should repeat this until nothing is left to resolve. There
  // could be complex cases where more than one pass is needed.
  // Go through backwards to prioritize pull
  std::vector<ASTNode> children = m_tree->getChildren();
  std::vector<ASTNode>::reverse_iterator rit = children.rbegin();
  ASTNode prevNode;
  // Process streams inside code generators.
  for (const ASTNode &node : children) {
    if (node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      auto typeDecl = ASTQuery::findTypeDeclaration(decl, {}, m_tree);
      if (ASTQuery::isCodeGenerator(typeDecl, scope, tree)) {
        auto streams = decl->getPropertyValue("streams");
        auto streamsChildren = streams->getChildren();
        auto rit = streamsChildren.rbegin();
        while (rit != streamsChildren.rend()) {
          ASTNode node = *rit;
          if (node->getNodeType() == AST::Stream) {
            resolveStreamTypesReverse(
                std::static_pointer_cast<StreamNode>(node), {},
                decl->getPropertyValue("blocks"));
          }
          rit++;
        }
        if (streams) {
          for (const auto &streamNode : streams->getChildren()) {
            if (streamNode->getNodeType() == AST::Stream) {
              resolveStreamTypesForward(
                  std::static_pointer_cast<StreamNode>(streamNode), {},
                  decl->getPropertyValue("blocks"));
            }
          }
        }
        // FIXME recurse into module declarations inside this module
        // https://github.com/StrideLang/stridegen/issues/1
      }
    }
  }

  while (rit != children.rend()) {
    ASTNode node = *rit;

    if (node->getNodeType() == AST::Stream) {
      resolveStreamTypesReverse(std::static_pointer_cast<StreamNode>(node),
                                scope, tree);
      resolveStreamRatesReverse(std::static_pointer_cast<StreamNode>(node),
                                scope, tree);
    }
    prevNode = *rit;
    rit++;
  }
  // Then do it again from the top to try to resolve the rest
  for (const ASTNode &node : children) {
    if (node->getNodeType() == AST::Stream) {
      resolveStreamTypesForward(std::static_pointer_cast<StreamNode>(node),
                                scope, tree);
      resolveStreamRates(std::static_pointer_cast<StreamNode>(node), scope,
                         tree);
    }
  }
}

void CodeResolver::analyzeConnections() {
  analyzeChildConnections(m_tree);

  std::vector<std::shared_ptr<DeclarationNode>> knownDomains;
  for (const ASTNode &node : m_tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      std::shared_ptr<DeclarationNode> decl =
          std::static_pointer_cast<DeclarationNode>(node);
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
  }

  // Now go through the tree to match domain reads and domain writes
  for (const ASTNode &node : m_tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration) {
      std::shared_ptr<DeclarationNode> decl;
      if (node->getCompilerProperty("reads")) {
        for (const auto &domain :
             node->getCompilerProperty("reads")->getChildren()) {
          if (domain) {
            if (domain->getNodeType() == AST::String) {
              std::string domainName =
                  std::static_pointer_cast<ValueNode>(domain)->getStringValue();
              for (const auto &knownDomain : knownDomains) {
                if (domainName == knownDomain->getName()) {
                  decl = knownDomain;
                  break;
                }
              }
              if (decl) {
                decl->getCompilerProperty("domainReads")->addChild(node);
              }
            } else if (domain->getNodeType() == AST::Entity) {
              decl = ASTQuery::findDeclarationByName(
                  ASTQuery::getNodeName(domain), {}, m_tree);
              if (decl) {
                decl->getCompilerProperty("domainReads")->addChild(node);
              }
            } else if (domain->getNodeType() == AST::None) {
              std::cerr << "Read domain is none. Read code will be generated "
                           "in declared domain."
                        << std::endl;
              auto domainNode = std::static_pointer_cast<DeclarationNode>(node)
                                    ->getPropertyValue("domain");
              if (domainNode && domainNode->getNodeType() != AST::None) {
                auto domainId =
                    CodeAnalysis::getDomainIdentifier(domainNode, {}, m_tree);
                auto domainDecl =
                    CodeAnalysis::findDomainDeclaration(domainId, m_tree);
                if (domainDecl) {
                  domainDecl->getCompilerProperty("domainReads")
                      ->addChild(node);
                }
              }

            } else {
              assert(domain->getNodeType() == AST::MemberAccess);
            }
          }
        }
      }

      if (node->getCompilerProperty("writes")) {
        for (const auto &domain :
             node->getCompilerProperty("writes")->getChildren()) {
          if (domain) {
            if (domain->getNodeType() == AST::String) {
              std::string domainName =
                  std::static_pointer_cast<ValueNode>(domain)->getStringValue();
              for (const auto &knownDomain : knownDomains) {
                if (domainName == knownDomain->getName()) {
                  decl = knownDomain;
                  break;
                }
              }
              if (decl) {
                decl->getCompilerProperty("domainWrites")->addChild(node);
              }
            } else if (domain->getNodeType() == AST::Entity) {
              decl = ASTQuery::findDeclarationByName(
                  ASTQuery::getNodeName(domain), {}, m_tree);
              if (decl && decl->getCompilerProperty("domainReads")) {
                decl->getCompilerProperty("domainReads")->addChild(node);
              } else {
                std::cerr << "ERROR: no domainReads property for domain: "
                          << ASTQuery::getNodeName(domain) << std::endl;
              }
            } else {
              if (domain->getNodeType() != AST::MemberAccess &&
                  domain->getNodeType() != AST::None) {
                std::cerr << "Expected port property domain" << std::endl;
              }
            }
          }
        }
      }
    }
  }
}

void CodeResolver::resolveTypeCasting() {
  for (const auto &node : m_tree->getChildren()) {
    resolveTypeCastForNode(node, {}, m_tree);
  }
}

void CodeResolver::resolveDeclarationDataTypes() {
  std::set<const void *> visited;
  for (const auto &node : m_tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration ||
        node->getNodeType() == AST::ArrayDeclaration) {
      resolveDataTypeForDeclaration(
          std::static_pointer_cast<DeclarationNode>(node), visited);
    }
  }
}

void CodeResolver::resolveDataTypeForDeclaration(
    std::shared_ptr<DeclarationNode> decl, std::set<const void *> &visited) {
  if (visited.count(decl.get()) > 0) {
    return;
  }
  visited.insert(decl.get());

  // The class is looked up even when a type is given, so it is in the tree
  // for ASTValidation to check the type against.
  auto typeClass = getDataTypeClass(decl);
  auto typesNode = typeClass ? typeClass->getPropertyValue("types") : nullptr;
  auto typeNode = decl->getPropertyValue("type");
  if (typesNode && typesNode->getChildren().size() > 0 &&
      (!typeNode || typeNode->getNodeType() == AST::None)) {
    // The first member of the class is the default
    auto defaultType = ASTQuery::getNodeName(typesNode->getChildren().front());
    decl->setPropertyValue("type", std::make_shared<EntityNode>(
                                       defaultType, __FILE__, __LINE__));
  }

  // Recurse into internal declarations (e.g. module blocks)
  for (const auto &property : decl->getProperties()) {
    auto value = property->getValue();
    if (!value) {
      continue;
    }
    std::vector<ASTNode> children;
    if (value->getNodeType() == AST::List) {
      children = value->getChildren();
    } else {
      children.push_back(value);
    }
    for (const auto &child : children) {
      if (child->getNodeType() == AST::Declaration ||
          child->getNodeType() == AST::ArrayDeclaration) {
        resolveDataTypeForDeclaration(
            std::static_pointer_cast<DeclarationNode>(child), visited);
      }
    }
  }
}

std::shared_ptr<DeclarationNode>
CodeResolver::getDataTypeClass(std::shared_ptr<DeclarationNode> decl) {
  // Typed declarations (e.g. signal) list a typeClass in the allowedTypes of
  // their type property. Untyped ones (switch, string, ...) have a builtin
  // data type and no type property.
  std::string frameworkName;
  auto frameworkNode = decl->getCompilerProperty("framework");
  if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
    frameworkName =
        std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
  }
  for (const auto &className : ASTQuery::getDataTypeClassNames(decl, m_tree)) {
    auto typeClass = findTypeClass(className, frameworkName);
    if (typeClass) {
      return typeClass;
    }
  }
  return nullptr;
}

void CodeResolver::setDeclarationType(std::shared_ptr<DeclarationNode> decl,
                                      ASTNode type) {
  auto oldType = decl->getPropertyValue("type");
  if (oldType && oldType->getNodeType() != AST::None) {
    return;
  }
  if (ASTQuery::getDataTypeClassNames(decl, m_tree).empty()) {
    return; // Builtin data type, e.g. switch or string
  }
  auto typeClass = getDataTypeClass(decl);
  if (typeClass && type->getNodeType() == AST::Entity &&
      !ASTQuery::isTypeClassMember(typeClass, ASTQuery::getNodeName(type)) &&
      decl->getCompilerProperty("autoDeclared")) {
    // A compiler declared signal receiving e.g. _SwitchType becomes a switch.
    // User declarations keep the type, which ASTValidation then rejects.
    static const std::map<std::string, std::string> builtinTypeDeclarations = {
        {"_SwitchType", "switch"}, {"_StringType", "string"}};
    auto builtin = builtinTypeDeclarations.find(ASTQuery::getNodeName(type));
    if (builtin != builtinTypeDeclarations.end()) {
      changeDeclarationType(decl, builtin->second);
      return;
    }
  }
  decl->setPropertyValue("type", type);
}

void CodeResolver::changeDeclarationType(std::shared_ptr<DeclarationNode> decl,
                                         std::string entityType) {
  decl->setEntityType(entityType);
  auto typeDecl = ASTQuery::findTypeDeclaration(decl, {}, m_tree);
  if (!typeDecl) {
    auto objects = m_system->getImportTrees();
    ASTFunctions::insertRequiredObjectsForNode(decl, objects, m_tree);
    typeDecl = ASTQuery::findTypeDeclaration(decl, {}, m_tree);
  }
  std::vector<std::string> portNames;
  if (typeDecl) {
    for (const auto &port :
         ASTQuery::getPortsForTypeEntity(typeDecl, {}, m_tree)) {
      auto portDecl = std::static_pointer_cast<DeclarationNode>(port);
      auto nameNode = portDecl->getPropertyValue("name");
      if (nameNode && nameNode->getNodeType() == AST::String) {
        portNames.push_back(
            std::static_pointer_cast<ValueNode>(nameNode)->getStringValue());
      }
    }
  }
  // Remove the previous type's default and properties the new type lacks
  auto properties = decl->getProperties();
  for (const auto &property : properties) {
    if (property->getName() == "default" ||
        (typeDecl && std::find(portNames.begin(), portNames.end(),
                               property->getName()) == portNames.end())) {
      decl->removeProperty(property);
    }
  }
  ASTFunctions::fillDefaultPropertiesForNode(decl, m_tree->getChildren());
}

std::shared_ptr<DeclarationNode>
CodeResolver::findTypeClass(std::string name, std::string framework) {
  auto hasFramework = [](const std::shared_ptr<DeclarationNode> &decl) {
    auto frameworkNode = decl->getCompilerProperty("framework");
    return frameworkNode && frameworkNode->getNodeType() == AST::String &&
           !std::static_pointer_cast<ValueNode>(frameworkNode)
                ->getStringValue()
                .empty();
  };
  auto typeClass = ASTQuery::findTypeClass(name, m_tree, framework);
  if (typeClass && hasFramework(typeClass)) {
    return typeClass;
  }
  // Only the dummy library class (or none) is in the tree. Bring in the
  // framework's class, which overlays it.
  auto objects = m_system->getImportTrees();
  for (const auto &objectsPair : objects) {
    for (const auto &node : objectsPair.second) {
      if (node->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "typeClass" && decl->getName() == name &&
          hasFramework(decl)) {
        m_tree->addChild(decl);
        ASTFunctions::insertRequiredObjectsForNode(decl, objects, m_tree);
        return ASTQuery::findTypeClass(name, m_tree, framework);
      }
    }
  }
  return typeClass;
}

void CodeResolver::storeDeclarations() {
  for (const auto &node : m_tree->getChildren()) {
    storeDeclarationsForNode(node, {}, m_tree);
  }
}

void CodeResolver::analyzeParents() {
  for (const auto &node : m_tree->getChildren()) {
    if (node->getNodeType() == AST::Declaration ||
        node->getNodeType() == AST::ArrayDeclaration) {
      appendParent(std::static_pointer_cast<DeclarationNode>(node), nullptr);
    }
  }
}

void CodeResolver::analyzePersistence() {
  auto scope = ScopeStack();
  for (const auto &child : m_tree->getChildren()) {
    if (child && (child->getNodeType() == AST::Declaration ||
                  child->getNodeType() == AST::ArrayDeclaration)) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      auto blocksList = decl->getPropertyValue("blocks");
      auto streamsList = decl->getPropertyValue("streams");
      if (streamsList) {
        if (blocksList) {
          scope.push_back({decl, blocksList->getChildren()});
        } else {
          scope.push_back({decl, {}});
        }
        markNodePersistence(streamsList, blocksList, scope, m_tree);
        scope.pop_back();
      }
    } else if (child && child->getNodeType() == AST::Stream) {
      markNodePersistence(m_tree, nullptr, scope, m_tree);
      break;
    }
  }
}

void CodeResolver::resolveDomainsForStream(std::shared_ptr<StreamNode> stream,
                                           ScopeStack scopeStack,
                                           ASTNode contextDomainNode) {
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  std::vector<ASTNode> domainStack;
  ASTNode previousDomain =
      CodeAnalysis::getNodeDomain(left, scopeStack, m_tree);
  std::string previousDomainId =
      CodeAnalysis::getDomainIdentifier(previousDomain, scopeStack, m_tree);

  //    std::string contextDomain =
  //    CodeAnalysis::getDomainIdentifier(contextDomainNode, scopeStack,
  //    m_tree);

  if (!previousDomain || previousDomain->getNodeType() == AST::None) {
    previousDomain = contextDomainNode;
  }
  ASTNode domainNode;
  while (right) {
    domainNode = processDomainsForNode(left, scopeStack, domainStack);
    std::string domainId;
    if (domainNode) {
      domainId =
          CodeAnalysis::getDomainIdentifier(domainNode, scopeStack, m_tree);
    }
    if (left == right &&
        (!domainNode ||
         domainNode->getNodeType() ==
             AST::None)) { // If this is is the last pass then set the
                           // unknown domains to context domain
      if (left->getNodeType() == AST::Function) {
        // Have functions/loops/reactions be assigned to whatever they are
        // connected to if they are final in a stream.
        // FIXME we should look inside the modules themselves to see if we
        // can infer the domain better
        auto decl = CodeAnalysis::getDeclaration(left);
        if (previousDomain && previousDomain->getNodeType() != AST::None) {
          domainNode = previousDomain;
        } else {
          previousDomain = contextDomainNode;
        }
      } else if (left->getNodeType() == AST::Entity ||
                 left->getNodeType() == AST::Array) {
        if (previousDomain && previousDomain->getNodeType() != AST::None) {
          //              domainNode = previousDomain;
        } else {
          previousDomain = contextDomainNode;
        }
      } else {
        // FIXME This is needed in cases where signals with no set domain
        // are connected to input ports on other objects. Domains are not
        // propagated across secondary ports, but they should be

        previousDomain = contextDomainNode;
      }
      // Unneeded as it gets added in the above call to
      // processDomainsForNode
      //            domainStack << left;
    }

    if (domainNode && domainNode->getNodeType() != AST::None &&
        previousDomainId != domainId) {
      auto resolvingInstance =
          CodeAnalysis::getInstance(left, scopeStack, m_tree);
      setDomainForStack(domainStack, resolvingInstance, domainNode, scopeStack);
      domainStack.clear();
    }
    if (left->getNodeType() == AST::Expression ||
        left->getNodeType() == AST::List) {
      auto samplingDomain =
          CodeAnalysis::getNodeDomain(stream->getRight(), scopeStack, m_tree);
      if (samplingDomain) {
        std::function<void(ASTNode node, ASTNode samplingDomain)> func =
            [&](ASTNode node, ASTNode samplingDomain) {
              for (auto child : node->getChildren()) {
                if (child->getNodeType() == AST::Expression ||
                    child->getNodeType() == AST::List) {
                  child->setCompilerProperty("samplingDomain", samplingDomain);
                  func(child, samplingDomain);
                } else {
                  if (child->getNodeType() == AST::Entity ||
                      child->getNodeType() == AST::Array ||
                      child->getNodeType() == AST::MemberAccess) {
                    // Check if expression element has no domain set. If it
                    // doesn't assign to samplingDomain
                    // TODO this does not cover the case where sampling
                    // domain is not resolvable at this point.
                    auto domain =
                        CodeAnalysis::getNodeDomain(child, scopeStack, m_tree);
                    if (!domain || domain->getNodeType() == AST::None) {
                      auto instance =
                          CodeAnalysis::getInstance(child, scopeStack, m_tree);
                      if (instance) {
                        if (instance->getNodeType() == AST::Declaration ||
                            instance->getNodeType() == AST::ArrayDeclaration) {
                          auto decl = std::static_pointer_cast<DeclarationNode>(
                              instance);
                          auto declType = ASTQuery::findTypeDeclaration(
                              decl, scopeStack, m_tree);
                          auto inheritList = ASTQuery::getInheritedTypes(
                              declType, scopeStack, m_tree);
                          bool inheritsDomainType = false;
                          for (auto inherits : inheritList) {
                            auto typeName =
                                inherits->getPropertyValue("typeName");
                            if (typeName->getNodeType() == AST::String) {
                              if (std::static_pointer_cast<ValueNode>(typeName)
                                      ->getStringValue() == "domainMember") {
                                inheritsDomainType = true;
                                break;
                              }
                            }
                          }
                          if (inheritsDomainType) {

                            decl->setPropertyValue("domain", samplingDomain);
                          }
                        } else if (instance->getNodeType() ==
                                   AST::MemberAccess) {
                          instance->setCompilerProperty("domain",
                                                        samplingDomain);
                        }
                      }
                    }
                  } else if (child->getNodeType() == AST::Function) {
                    // TODO complete
                  }
                }
              }
            };
        left->setCompilerProperty("samplingDomain", samplingDomain);
        func(left, samplingDomain);
      }
    } else if (left->getNodeType() == AST::Function) {
      auto func = std::static_pointer_cast<FunctionNode>(left);

      auto decl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(left),
                                                  scopeStack, m_tree);

      if (decl) {
        if (decl->getEntityType() == "module" ||
            decl->getEntityType() == "reaction" ||
            decl->getEntityType() == "loop") {
          //

          domainNode = processDomainsForNode(decl, scopeStack, domainStack);
          auto internalStreams = decl->getPropertyValue("streams");
          auto internalBlocks = decl->getPropertyValue("blocks");
          auto internalScopeStack = scopeStack;

          if (internalStreams && internalBlocks) {
            internalScopeStack.push_back({func, internalBlocks->getChildren()});
            for (const auto &internalStream : internalStreams->getChildren()) {
              assert(internalStream->getNodeType() == AST::Stream);
              if (internalStream->getNodeType() == AST::Stream) {
                // No context domain as all domains within modules must be
                // resolvable internally
                resolveDomainsForStream(
                    std::static_pointer_cast<StreamNode>(internalStream),
                    internalScopeStack, nullptr);
              }
            }
          }
        }
      }
      // -- Process domains for connected ports
      ASTNode samplingDomainName;
      for (const auto &property : func->getProperties()) {
        samplingDomainName = processDomainsForNode(property->getValue(),
                                                   scopeStack, domainStack);
      }
    }

    if (left == right) {
      if (previousDomain && previousDomain->getNodeType() != AST::None) {
        auto resolvingInstance =
            CodeAnalysis::getInstance(left, scopeStack, m_tree);
        setDomainForStack(domainStack, resolvingInstance, previousDomain,
                          scopeStack);
        domainStack.clear();
      }
      right = left = nullptr; // End
    } else if (right->getNodeType() == AST::Stream) {
      stream = std::static_pointer_cast<StreamNode>(right);
      left = stream->getLeft();
      right = stream->getRight();
    } else {
      if (left->getNodeType() == AST::Expression ||
          left->getNodeType() == AST::List) {
        //        auto domainNode =
        //            CodeAnalysis::getNodeDomain(right, scopeStack,
        //            m_tree);
        //        if (!domainNode) {
        //          domainNode = std::make_shared<ValueNode>(__FILE__,
        //          __LINE__);
        //        }
        //        left->setCompilerProperty("samplingDomain", domainNode);
      }
      left = right; // Last pass (process right, call it left)
    }
    previousDomain = domainNode;
    previousDomainId = domainId;
  }
}

void CodeResolver::setDomainForNode(ASTNode node, ASTNode domain,
                                    ScopeStack scopeStack, ASTNode tree,
                                    bool force) {
  auto existingDomain = CodeAnalysis::getNodeDomain(node, scopeStack, tree);
  if (existingDomain && existingDomain->getNodeType() != AST::None && !force) {
    return;
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    std::shared_ptr<DeclarationNode> decl =
        std::static_pointer_cast<DeclarationNode>(node);
    if (decl) {
      decl->replacePropertyValue("domain", domain);
    }
  } else if (node->getNodeType() == AST::Entity ||
             node->getNodeType() == AST::Array) {
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(ASTQuery::getNodeName(node), scopeStack,
                                        tree);
    if (declaration) {
      auto typeDeclaration =
          ASTQuery::findTypeDeclaration(declaration, scopeStack, tree);
      if (typeDeclaration &&
          typeDeclaration->getEntityType() == "platformModule") {
        node->setCompilerProperty("domain", domain);
      } else {
        declaration->replacePropertyValue("domain", domain);
      }
    }
  } else if (node->getNodeType() == AST::Function) {
    std::shared_ptr<FunctionNode> func =
        std::static_pointer_cast<FunctionNode>(node);
    if (func) {
      func->setCompilerProperty("domain", domain);
    }
  } else if (node->getNodeType() == AST::Real ||
             node->getNodeType() == AST::Int ||
             node->getNodeType() == AST::String ||
             node->getNodeType() == AST::Switch) {
    std::shared_ptr<ValueNode> val = std::static_pointer_cast<ValueNode>(node);
    if (val) {
      val->setDomain(domain);
    }
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    for (const ASTNode &member : node->getChildren()) {
      setDomainForNode(member, domain, scopeStack, tree, force);
    }
  } else if (node->getNodeType() == AST::MemberAccess) {
    node->setCompilerProperty("domain", domain);
  }
}

ASTNode CodeResolver::processDomainsForNode(ASTNode node, ScopeStack scopeStack,
                                            std::vector<ASTNode> &domainStack) {
  ASTNode currentDomain;
  resolveDomainForStreamNode(node, scopeStack);
  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(ASTQuery::getNodeName(node), scopeStack,
                                        m_tree, node->getNamespaceList());

    if (declaration) {
      ASTNode domain = declaration->getDomain();
      auto typeDeclaration =
          ASTQuery::findTypeDeclaration(declaration, scopeStack, m_tree);
      if (typeDeclaration &&
          typeDeclaration->getEntityType() == "platformModule") {
        // The instance for a platform module is the block itself, not the
        // declaration
        domainStack.push_back(node);
      } else {
        if (!domain || domain->getNodeType() == AST::None) {
          // Put declaration in stack to set domain once domain is resolved
          domainStack.push_back(declaration);
        } else if (domain->getNodeType() == AST::String ||
                   domain->getNodeType() == AST::Entity ||
                   domain->getNodeType() == AST::MemberAccess) {
          currentDomain = domain->deepCopy();
          currentDomain->setNamespaceList(node->getNamespaceList());
          //            currentDomain->addScope()
        } else {
          std::cerr << "unrecognized domain type" << std::endl;
        }
      }
      if (node->getNodeType() == AST::Array) {
        auto indexNode = std::static_pointer_cast<ArrayNode>(node)->index();
        for (const auto &indexElement : indexNode->getChildren()) {
          resolveDomainForStreamNode(indexElement, scopeStack);
          std::vector<ASTNode> indexDomainStack;
          processDomainsForNode(indexElement, scopeStack, indexDomainStack);
          if (indexElement->getNodeType() == AST::String ||
              indexElement->getNodeType() == AST::Int ||
              indexElement->getNodeType() == AST::Real) {
            // No need to do resolve domain.
          } else if (indexElement->getNodeType() != AST::List &&
                     indexElement->getNodeType() != AST::Expression &&
                     indexElement->getNodeType() != AST::Range) {
            ASTNode domain =
                CodeAnalysis::getNodeDomain(indexElement, scopeStack, m_tree);
            if (domain && domain->getNodeType() != AST::None) {
              setDomainForNode(indexElement, domain, scopeStack, m_tree);
            } else {
              std::cerr << "WARNING: domain has not resolved for bundle when "
                           "resolving index: "
                        << node->toText() << std::endl;
            }

          } else {
            // FIXME implement
            assert(0 == 1);
          }
        }
      }
      // Check if declared in the current scope. If declared here then store
      // parent list

      if (scopeStack.size() > 0) {
        std::shared_ptr<DeclarationNode> scopeDeclaration =
            ASTQuery::findDeclarationByName(ASTQuery::getNodeName(node),
                                            {scopeStack.back()}, nullptr);
        if (scopeDeclaration) {
          std::shared_ptr<ListNode> parentList =
              std::make_shared<ListNode>(__FILE__, __LINE__);
          if (declaration->getCompilerProperty("parentInstances") == nullptr) {
            for (const auto &subScope : scopeStack) {
              std::string name;
              if (subScope.first) {
                name = ASTQuery::getNodeName(subScope.first);
              }
              parentList->addChild(
                  std::make_shared<ValueNode>(name, __FILE__, __LINE__));
            }
            declaration->setCompilerProperty("parentInstances", parentList);
          }
        }
      }
    }
  } else if (node->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(node);
    ASTNode domain = CodeAnalysis::getNodeDomain(node, scopeStack, m_tree);
    if (!domain) {
      // Put declaration in stack to set domain once domain is resolved
      domainStack.push_back(node);
    } else {
      if (domain->getNodeType() == AST::String ||
          domain->getNodeType() == AST::Entity ||
          domain->getNodeType() == AST::MemberAccess) {
        currentDomain = domain;
      } else {
        domainStack.push_back(node);
      }
    }
    for (const auto &member : func->getProperties()) {
      ASTNode propertyDomainName =
          processDomainsForNode(member->getValue(), scopeStack, domainStack);
      if (!propertyDomainName ||
          propertyDomainName->getNodeType() ==
              AST::None) { // Include property block in domain stack because
                           // domain undefined
        domainStack.push_back(member->getValue());
      }
    }
  } else if (node->getNodeType() == AST::List) {
    std::vector<ASTNode> listDomainStack;
    ASTNode resolvingInstance;
    for (const ASTNode &member : node->getChildren()) {
      ASTNode memberDomainName =
          processDomainsForNode(member, scopeStack, domainStack);

      if (!currentDomain || currentDomain->getNodeType() == AST::None) {
        // list takes domain from first element that has a domain
        if (memberDomainName &&
            (memberDomainName->getNodeType() == AST::String ||
             memberDomainName->getNodeType() == AST::Entity ||
             memberDomainName->getNodeType() == AST::MemberAccess)) {
          currentDomain = memberDomainName;
          resolvingInstance = member;
        } else {
          domainStack.push_back(node);
        }
      }
      if (!memberDomainName || memberDomainName->getNodeType() == AST::None) {
        // Put declaration in stack to set domain once domain is resolved

        listDomainStack.push_back(member);
        // FIMXE: This is very simplistic (or plain wrong....)
        // It assumes that the next found domain affects all elements
        // in the list that don't have domains. This is likely a
        // common case but list elements should inherit domains from
        // the port to which they are connected.
      }
    }
    if (currentDomain && currentDomain->getNodeType() != AST::None) {
      setDomainForStack(listDomainStack, resolvingInstance, currentDomain,
                        scopeStack);
    } else {
      domainStack.insert(domainStack.end(), listDomainStack.begin(),
                         listDomainStack.end());
      // If list has no defined domain, pass
      // all elements to be set later
    }
  } else if (node->getNodeType() == AST::Expression) {
    ASTNode samplingDomain;
    for (const ASTNode &member : node->getChildren()) {
      ASTNode newDomainName =
          processDomainsForNode(member, scopeStack, domainStack);
      // if member doesn't have a domain, put in the stack to resolve later.
      if (!newDomainName || newDomainName->getNodeType() == AST::None) {
        // Put declaration in stack to set domain once domain is resolved
        domainStack.push_back(member);
      } else {
        samplingDomain = newDomainName;
        currentDomain = newDomainName;
        // Take first domain available
        break;
      }
    }
    if (samplingDomain) {
      node->setCompilerProperty("samplingDomain", samplingDomain);
    } else {
    }
    if (scopeStack.size() > 0) {
      if (node->getCompilerProperty("parentInstances") == nullptr) {
        std::shared_ptr<ListNode> parentList =
            std::make_shared<ListNode>(__FILE__, __LINE__);
        for (const auto &subScope : scopeStack) {
          std::string name;
          if (subScope.first) {
            name = ASTQuery::getNodeName(subScope.first);
          }
          parentList->addChild(
              std::make_shared<ValueNode>(name, __FILE__, __LINE__));
        }
        node->setCompilerProperty("parentInstances", parentList);
      }
    }

  } else if (node->getNodeType() == AST::Int ||
             node->getNodeType() == AST::Real ||
             node->getNodeType() == AST::String ||
             node->getNodeType() == AST::Switch) {
    domainStack.push_back(node);
  } else if (node->getNodeType() == AST::Declaration) {
    // An anonymous reaction declaration.
    domainStack.push_back(node);
  }
  return currentDomain;
}

void CodeResolver::setDomainForStack(std::vector<ASTNode> domainStack,
                                     ASTNode resolvingInstance,
                                     ASTNode domainName,
                                     ScopeStack scopeStack) {
  for (ASTNode relatedNode : domainStack) {
    if (resolvingInstance->getNodeType() == AST::Declaration &&
        std::static_pointer_cast<DeclarationNode>(resolvingInstance)
                ->getEntityType() == "table") {
      // Table captures this stream
      relatedNode->setCompilerProperty("tableCaptureInstance",
                                       resolvingInstance);
    }

    setDomainForNode(relatedNode, domainName, scopeStack, m_tree);
  }
}

std::shared_ptr<DeclarationNode>
CodeResolver::createSignalDeclaration(std::string name, int size,
                                      ScopeStack scope, ASTNode tree) {
  std::shared_ptr<DeclarationNode> newBlock = nullptr;
  const char *fname = tree ? tree->getFilename().c_str() : "";
  int line = tree ? tree->getLine() : -1;
  int column = tree ? tree->getColumn() : -1;
  if (size == 0) { // Is it OK to generate a signal of size 1 in this case??
    newBlock = std::make_shared<DeclarationNode>(name, "signal", nullptr,
                                                 fname, line, column);
  } else if (size == 1) {
    newBlock = std::make_shared<DeclarationNode>(name, "signal", nullptr,
                                                 fname, line, column);
  } else if (size > 1) {
    std::shared_ptr<ListNode> indexList = std::make_shared<ListNode>(
        std::make_shared<ValueNode>((int64_t)size, fname, line, column), fname, line, column);
    std::shared_ptr<ArrayNode> arrayNode =
        std::make_shared<ArrayNode>(name, indexList, fname, line, column);
    newBlock =
        std::make_shared<DeclarationNode>(arrayNode, "signal", nullptr, fname, line, column);
  }
  assert(newBlock);
  // Lets the resolver turn it into a switch or string once its type is known
  newBlock->setCompilerProperty(
      "autoDeclared", std::make_shared<ValueNode>(true, __FILE__, __LINE__));
  if (tree) {
    tree->copyLocationTo(newBlock);
  }
  ASTFunctions::fillDefaultPropertiesForNode(newBlock, tree->getChildren());

  ASTFunctions::resolveConstantsInNode(newBlock, scope, tree);
  return newBlock;
}

void CodeResolver::setNodeRate(ASTNode node, double rate, ScopeStack scope,
                               ASTNode tree) {
  if (node->getNodeType() == AST::Entity) {
    EntityNode *name = static_cast<EntityNode *>(node.get());
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(name->getName(), scope, tree,
                                        name->getNamespaceList());
    // Declarations that are not rated are reactive and have no rate to set
    if (declaration && ASTQuery::isRated(declaration, m_tree)) {
      std::shared_ptr<ValueNode> value =
          std::make_shared<ValueNode>(rate, __FILE__, __LINE__);
      declaration->replacePropertyValue("rate", value);
    }
    return;
  } else if (node->getNodeType() == AST::Array) {
    ArrayNode *arrayNode = static_cast<ArrayNode *>(node.get());
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(arrayNode->getName(), scope, tree,
                                        arrayNode->getNamespaceList());
    if (declaration && ASTQuery::isRated(declaration, m_tree)) {
      std::shared_ptr<ValueNode> value =
          std::make_shared<ValueNode>(rate, __FILE__, __LINE__);
      if (!declaration->replacePropertyValue("rate", value)) {
        std::cerr << "Couldn't set rate. Rate property does not exist."
                  << std ::endl;
      }
    }
    return;
  } else if (node->getNodeType() == AST::Function) {
    FunctionNode *func = static_cast<FunctionNode *>(node.get());
    func->setRate(rate);
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    for (const ASTNode &element : node->getChildren()) {
      double elementRate = CodeAnalysis::getNodeRate(element, scope, tree);
      if (elementRate < 0.0) {
        CodeResolver::setNodeRate(element, rate, scope, tree);
      }
    }
  }
}

void CodeResolver::setNodeType(ASTNode node, ASTNode type, ScopeStack scope,
                               ASTNode tree) {
  if (node->getNodeType() == AST::Entity) {
    std::shared_ptr<EntityNode> name =
        std::static_pointer_cast<EntityNode>(node);
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(name->getName(), scope, tree,
                                        name->getNamespaceList());
    if (declaration) {
      setDeclarationType(declaration, type);
    }
    return;
  } else if (node->getNodeType() == AST::Array) {
    ArrayNode *arrayNode = static_cast<ArrayNode *>(node.get());
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(arrayNode->getName(), scope, tree,
                                        arrayNode->getNamespaceList());
    if (declaration) {
      setDeclarationType(declaration, type);
    }
  } else if (node->getNodeType() == AST::Function) {
    // Perhaps nothing needs to be done, as the compiler properties showing
    // connections and port blocks can describe all that's needed
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    for (const ASTNode &element : node->getChildren()) {
      // assert(0 == 1);
      // TODO implement
      if (type) {
        // TODO verify that node doesn't have type already
        // CodeResolver::setNodeType(element, type, scope, tree);
      }
    }
  }
}

std::vector<ASTNode>
CodeResolver::declareUnknownName(std::shared_ptr<EntityNode> entityNode,
                                 int size, ScopeStack localScope,
                                 ASTNode tree) {
  // TODO store found declarations in compiler property.
  // https://github.com/StrideLang/strideutils/issues/1
  std::vector<ASTNode> declarations;
  std::shared_ptr<DeclarationNode> decl = ASTQuery::findDeclarationByName(
      entityNode->getName(), localScope, tree, entityNode->getNamespaceList());
  if (!decl) { // Not declared, so make declaration
    std::shared_ptr<DeclarationNode> newSignal = createSignalDeclaration(
        entityNode->getName(), size, localScope, m_tree);
    double rate = CodeAnalysis::getNodeRate(newSignal, ScopeStack(), tree);

    CodeResolver::setNodeRate(entityNode, rate, localScope, tree);
    declarations.push_back(newSignal);
  }
  return declarations;
}

std::vector<ASTNode>
CodeResolver::declareUnknownArray(std::shared_ptr<ArrayNode> arrayNode,
                                  int size, ScopeStack localScope,
                                  ASTNode tree) {
  std::vector<ASTNode> declarations;
  std::shared_ptr<DeclarationNode> decl = ASTQuery::findDeclarationByName(
      arrayNode->getName(), localScope, tree, arrayNode->getNamespaceList());
  if (!decl) { // Not declared, so make declaration
    std::shared_ptr<DeclarationNode> newSignal =
        createSignalDeclaration(arrayNode->getName(), size, localScope, m_tree);
    double rate = CodeAnalysis::getNodeRate(newSignal, ScopeStack(), tree);
    CodeResolver::setNodeRate(arrayNode, rate, localScope, tree);
    declarations.push_back(newSignal);
  }
  return declarations;
}

std::shared_ptr<DeclarationNode>
CodeResolver::createConstantDeclaration(std::string name, ASTNode value) {
  const char *fname = value ? value->getFilename().c_str() : "";
  int line = value ? value->getLine() : -1;
  int column = value ? value->getColumn() : -1;
  std::shared_ptr<DeclarationNode> constant = std::make_shared<DeclarationNode>(
      name, "constant", nullptr, fname, line, column);
  if (value) {
    value->copyLocationTo(constant);
  }
  std::shared_ptr<PropertyNode> valueProperty =
      std::make_shared<PropertyNode>("value", value, fname, line, column);
  if (value) {
    value->copyLocationTo(valueProperty);
  }
  constant->addProperty(valueProperty);
  return constant;
}

void CodeResolver::declareIfMissing(std::string name, ASTNode declList,
                                    ASTNode value) {
  std::shared_ptr<DeclarationNode> declaration = nullptr;
  if (declList->getNodeType() == AST::List) {
    ListNode *declListNode = static_cast<ListNode *>(declList.get());
    // First check if entity has been declared
    for (const ASTNode &declNode : declListNode->getChildren()) {
      if (declNode->getNodeType() == AST::Declaration ||
          declNode->getNodeType() == AST::ArrayDeclaration) {
        std::shared_ptr<DeclarationNode> declaredDecl =
            std::static_pointer_cast<DeclarationNode>(declNode);
        if (declaredDecl->getName() == name) {
          declaration = declaredDecl;
          break;
        }
      }
    }
    if (!declaration) {
      declaration = createConstantDeclaration(name, value);
      ASTFunctions::fillDefaultPropertiesForNode(declaration,
                                                 m_tree->getChildren());
      declListNode->addChild(declaration);
    } else {
      //            delete value;
    }
  } else {
    //        delete value;
    std::cerr << "CodeResolver::declareIfMissing() declList is not list"
              << std::endl;
  }
}

std::vector<ASTNode> CodeResolver::declareUnknownExpressionSymbols(
    std::shared_ptr<ExpressionNode> expr, int size, ScopeStack scopeStack,
    ASTNode tree) {
  std::vector<ASTNode> newDeclarations;
  if (expr->isUnary()) {
    if (expr->getValue()->getNodeType() == AST::Entity) {
      std::shared_ptr<EntityNode> name =
          std::static_pointer_cast<EntityNode>(expr->getValue());
      std::vector<ASTNode> decls =
          declareUnknownName(name, size, scopeStack, tree);
      newDeclarations.insert(newDeclarations.end(), decls.begin(), decls.end());
    } else if (expr->getValue()->getNodeType() == AST::Expression) {
      std::shared_ptr<ExpressionNode> name =
          std::static_pointer_cast<ExpressionNode>(expr->getValue());
      std::vector<ASTNode> decls =
          declareUnknownExpressionSymbols(name, size, scopeStack, tree);
      newDeclarations.insert(newDeclarations.end(), decls.begin(), decls.end());
    }
  } else {
    if (expr->getLeft()->getNodeType() == AST::Entity) {
      std::shared_ptr<EntityNode> name =
          std::static_pointer_cast<EntityNode>(expr->getLeft());
      std::vector<ASTNode> decls =
          declareUnknownName(name, size, scopeStack, tree);
      newDeclarations.insert(newDeclarations.end(), decls.begin(), decls.end());
    } else if (expr->getLeft()->getNodeType() == AST::Expression) {
      std::shared_ptr<ExpressionNode> inner_expr =
          std::static_pointer_cast<ExpressionNode>(expr->getLeft());
      std::vector<ASTNode> decls =
          declareUnknownExpressionSymbols(inner_expr, size, scopeStack, tree);
      newDeclarations.insert(newDeclarations.end(), decls.begin(), decls.end());
    }
    if (expr->getRight()->getNodeType() == AST::Entity) {
      std::shared_ptr<EntityNode> name =
          std::static_pointer_cast<EntityNode>(expr->getRight());
      std::vector<ASTNode> decls =
          declareUnknownName(name, size, scopeStack, tree);
      newDeclarations.insert(newDeclarations.end(), decls.begin(), decls.end());
    } else if (expr->getRight()->getNodeType() == AST::Expression) {
      std::shared_ptr<ExpressionNode> inner_expr =
          std::static_pointer_cast<ExpressionNode>(expr->getRight());
      std::vector<ASTNode> decls =
          declareUnknownExpressionSymbols(inner_expr, size, scopeStack, tree);
      newDeclarations.insert(newDeclarations.end(), decls.begin(), decls.end());
    }
  }
  return newDeclarations;
}

std::vector<ASTNode> CodeResolver::declareUnknownFunctionSymbols(
    std::shared_ptr<FunctionNode> func, ScopeStack scopeStack, ASTNode tree) {
  std::vector<ASTNode> newDeclarations;
  std::vector<std::shared_ptr<PropertyNode>> properties = func->getProperties();

  for (const auto &property : properties) {
    ASTNode value = property->getValue();
    if (value->getNodeType() == AST::Entity) {
      std::shared_ptr<EntityNode> entityNode =
          std::static_pointer_cast<EntityNode>(value);
      std::vector<ASTNode> declarations =
          declareUnknownName(entityNode, 1, scopeStack, tree);
      for (const ASTNode &declaration : declarations) {
        tree->addChild(declaration);
      }
    } else if (value->getNodeType() == AST::Array) {
      // FIXME should we have some heuristics for arrays, or just not allow
      // auto delcarations?
    }
  }

  return newDeclarations;
}

std::shared_ptr<ListNode> CodeResolver::expandNameToList(EntityNode *name,
                                                         int size) {
  std::shared_ptr<ListNode> list = std::make_shared<ListNode>(
      nullptr, name->getFilename().data(), name->getLine());
  for (int i = 0; i < size; i++) {
    std::shared_ptr<ListNode> indexList = std::make_shared<ListNode>(
        std::make_shared<ValueNode>((int64_t)i, name->getFilename().data(),
                                    name->getLine()),
        name->getFilename().data(), name->getLine());
    std::shared_ptr<ArrayNode> arrayNode = std::make_shared<ArrayNode>(
        name->getName(), indexList, name->getFilename().data(),
        name->getLine());
    list->addChild(arrayNode);
  }
  return list;
}

void CodeResolver::expandNamesToArrays(std::shared_ptr<StreamNode> stream,
                                       ASTNode tree) {
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  //    ASTNode nextStreamMember;
  //    if (right->getNodeType() != AST::Stream) {
  //        nextStreamMember = right;
  //    } else {
  //        nextStreamMember = static_cast<StreamNode *>(right)->getLeft();
  //    }

  if (left->getNodeType() == AST::Entity) {
    EntityNode *name = static_cast<EntityNode *>(left.get());
    ScopeStack scope;
    std::shared_ptr<DeclarationNode> decl =
        ASTQuery::findDeclarationByName(name->getName(), scope, tree);
    int size = 0;
    if (decl) {
      if (decl->getNodeType() == AST::ArrayDeclaration) {
        size = ASTQuery::getEntityDeclaredSize(decl, scope, tree);
      } else if (decl->getNodeType() == AST::Declaration) {
        size = 1;
      }
    }
    if (size > 1) {
      std::shared_ptr<ListNode> list = expandNameToList(name, size);
      stream->setLeft(list);
    }
  }
  if (right->getNodeType() == AST::Entity) {
    EntityNode *name = static_cast<EntityNode *>(right.get());
    ScopeStack scope;
    std::shared_ptr<DeclarationNode> decl = ASTQuery::findDeclarationByName(
        name->getName(), scope, tree, name->getNamespaceList());
    int size = 0;
    if (decl) {
      if (decl->getNodeType() == AST::ArrayDeclaration) {
        size = ASTQuery::getEntityDeclaredSize(decl, scope, tree);
      } else if (decl->getNodeType() == AST::Declaration) {
        size = 1;
      }
    }
    if (size > 1) {
      std::shared_ptr<ListNode> list = expandNameToList(name, size);
      stream->setRight(list);
    }
  } else if (right->getNodeType() == AST::Stream) {
    expandNamesToArrays(std::static_pointer_cast<StreamNode>(right), tree);
  }
}

std::vector<ASTNode>
CodeResolver::declareUnknownStreamSymbols(std::shared_ptr<StreamNode> stream,
                                          ASTNode previousStreamMember,
                                          ScopeStack localScope, ASTNode tree) {
  std::vector<ASTNode> newDeclarations;

  if (m_declareUnknownSymbols) {
    ASTNode left = stream->getLeft();
    ASTNode right = stream->getRight();

    ASTNode nextStreamMember;
    if (right->getNodeType() != AST::Stream) {
      nextStreamMember = right;
    } else {
      nextStreamMember = std::static_pointer_cast<StreamNode>(right)->getLeft();
    }

    if (left->getNodeType() == AST::Entity) {
      std::shared_ptr<EntityNode> name =
          std::static_pointer_cast<EntityNode>(left);
      int size = -1;
      if (previousStreamMember) {
        size = CodeAnalysis::getNodeNumOutputs(previousStreamMember, localScope,
                                               m_tree);
      }
      if (size <= 0 && previousStreamMember) { // Look to the right if can't
                                               // resolve from the left
        // Size for first member should always be 1
        size = CodeAnalysis::getNodeNumInputs(nextStreamMember, localScope,
                                              m_tree);
      }
      if (size <= 0) { // None of the elements in the stream have size
        size = 1;
      }
      std::vector<ASTNode> declarations =
          declareUnknownName(name, size, localScope, tree);
      newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                             declarations.end());
    } else if (left->getNodeType() == AST::Expression) {
      int size = 1; // FIXME implement size detection for expressions
      std::shared_ptr<ExpressionNode> expr =
          std::static_pointer_cast<ExpressionNode>(left);
      std::vector<ASTNode> declarations =
          declareUnknownExpressionSymbols(expr, size, localScope, tree);
      newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                             declarations.end());
    } else if (left->getNodeType() == AST::Function) {
      std::shared_ptr<FunctionNode> func =
          std::static_pointer_cast<FunctionNode>(left);
      std::vector<ASTNode> declarations =
          declareUnknownFunctionSymbols(func, localScope, tree);
      newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                             declarations.end());
    }

    if (right->getNodeType() == AST::Stream) {
      std::vector<ASTNode> declarations = declareUnknownStreamSymbols(
          std::static_pointer_cast<StreamNode>(right), left, localScope, tree);
      newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                             declarations.end());
    } else if (right->getNodeType() == AST::Entity) {
      std::shared_ptr<EntityNode> name =
          std::static_pointer_cast<EntityNode>(right);
      int size = 0;
      if (left->getNodeType() == AST::Function) {
        auto funcDecl = ASTQuery::findDeclarationByName(
            ASTQuery::getNodeName(left), localScope, tree);
        if (funcDecl) {
          size = CodeAnalysis::getTypeNumOutputs(funcDecl, localScope, m_tree);
        }
      } else {
        size = CodeAnalysis::getNodeNumOutputs(left, localScope, m_tree);
      }
      if (size <= 0) { // None of the elements in the stream have size
        size = 1;
      }
      std::vector<ASTNode> declarations =
          declareUnknownName(name, size, localScope, tree);
      newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                             declarations.end());
    } else if (right->getNodeType() == AST::Function) {
      std::shared_ptr<FunctionNode> func =
          std::static_pointer_cast<FunctionNode>(right);
      std::vector<ASTNode> declarations =
          declareUnknownFunctionSymbols(func, localScope, tree);
      newDeclarations.insert(newDeclarations.end(), declarations.begin(),
                             declarations.end());
    }
  }
  return newDeclarations;
}

std::vector<ASTNode>
CodeResolver::getModuleStreams(std::shared_ptr<DeclarationNode> module) {
  std::vector<ASTNode> streams;

  ASTNode streamsNode = module->getPropertyValue("streams");
  if (!streamsNode) {
    return streams;
  }
  if (streamsNode->getNodeType() == AST::Stream) {
    streams.push_back(streamsNode);
  } else if (streamsNode->getNodeType() == AST::List) {
    for (const ASTNode &node : streamsNode->getChildren()) {
      if (node->getNodeType() == AST::Stream) {
        streams.push_back(node);
      }
    }
  }
  return streams;
}

void CodeResolver::declareInternalEntitiesForNode(ASTNode node,
                                                  ScopeStack subScope) {
  //  {{decl->getName(), internalBlocks->getChildren()}}
  auto findDeclarationWithinFunctionScope = [&](ASTNode portEntity,
                                                ScopeStack innerScope) {
    auto portBlockDecl = ASTQuery::findDeclarationByName(
        ASTQuery::getNodeName(portEntity), innerScope, nullptr);
    if (!portBlockDecl) {
      // Check for constants in parent nodes
      auto rootPortBlockDecl = ASTQuery::findDeclarationByName(
          ASTQuery::getNodeName(portEntity), subScope, m_tree);
      if (rootPortBlockDecl &&
          ASTQuery::isConstant(rootPortBlockDecl, subScope, m_tree)) {
        portBlockDecl = rootPortBlockDecl;
      }
    }
    return portBlockDecl;
  };

  if (node->getNodeType() == AST::Declaration) {
    std::shared_ptr<DeclarationNode> decl =
        std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() ==
        "loop") { // We need to define an internal domain for loops
      ListNode *ports =
          static_cast<ListNode *>(decl->getPropertyValue("ports").get());
      if (!ports) {
        decl->setPropertyValue(
            "ports", std::make_shared<ValueNode>(
                         __FILE__,
                         __LINE__)); // Make a None node to trigger next branch
      }
      if (ports && ports->getNodeType() == AST::None) {
        decl->replacePropertyValue(
            "ports", std::make_shared<ListNode>(nullptr, __FILE__, __LINE__));
        ports = static_cast<ListNode *>(decl->getPropertyValue("ports").get());
      }
      ASTNode internalBlocks = decl->getPropertyValue("blocks");
      //            internalBlocks->addChild(createDomainDeclaration(QString::fromStdString("_OutputDomain")));
    }
    if (decl->getEntityType() == "module" ||
        decl->getEntityType() == "reaction" ||
        decl->getEntityType() == "loop") {
      // First insert and resolve input and output domains for main ports.
      // The input port takes the output domain if undefined.

      ASTNode internalBlocks = decl->getPropertyValue("blocks");
      if (!internalBlocks || internalBlocks->getNodeType() !=
                                 AST::List) { // Turn blocks property into list
        decl->setPropertyValue(
            "blocks",
            std::make_shared<ListNode>(internalBlocks, __FILE__, __LINE__));
        internalBlocks = decl->getPropertyValue("blocks");
      }

      // Check Output port and declare block if not declared
      std::shared_ptr<DeclarationNode> outputPortBlock =
          ASTQuery::getModuleMainOutputPort(decl);
      ASTNode mainPortsDefaultDomain;
      if (outputPortBlock) {
        ASTNode portEntity = outputPortBlock->getPropertyValue("block");
        ASTNode outputDomain;
        if (!outputPortBlock->getCompilerProperty("anonymous")) {
          outputDomain = std::make_shared<MemberAccessNode>(
              outputPortBlock->getName(), "domain", __FILE__, __LINE__);
          mainPortsDefaultDomain = outputDomain;
        }
        // First give port a block and its declaration if it doesn't have
        // one.
        if (!portEntity || portEntity->getNodeType() == AST::None) {
          std::string possibleName = "Output";
          auto outputDecl = ASTQuery::findDeclarationByName(
              possibleName, ScopeStack(), internalBlocks);
          int counter = 0;
          while (outputDecl) {
            possibleName = "Output_" + std::to_string(counter);
            outputDecl = ASTQuery::findDeclarationByName(
                possibleName, ScopeStack(), internalBlocks);
            counter++;
          }
          portEntity =
              std::make_shared<EntityNode>(possibleName, __FILE__, __LINE__);
          outputPortBlock->setPropertyValue("block", portEntity);
        }

        // Add declaration if not there
        // We need to find declaration only in the internal blocks, as port
        // blocks can only be internal.
        auto portBlockDecl = findDeclarationWithinFunctionScope(
            portEntity, {{node, internalBlocks->getChildren()}});

        if (!portBlockDecl) {
          // FIXME this has to check wether block is a bundle and create
          // declaration accordingly
          portBlockDecl = createSignalDeclaration(
              ASTQuery::getNodeName(portEntity), 1, subScope, m_tree);
          internalBlocks->addChild(portBlockDecl);
          ASTFunctions::fillDefaultPropertiesForNode(portBlockDecl,
                                                     m_tree->getChildren());
        }

        // Now check if domain set, if not, set to port domain
        ASTNode domainNode = CodeAnalysis::getNodeDomain(
            portEntity, {{node, internalBlocks->getChildren()}}, m_tree);
        if (!domainNode || domainNode->getNodeType() == AST::None) {
          if (portBlockDecl && mainPortsDefaultDomain) {
            portBlockDecl->setPropertyValue("domain", mainPortsDefaultDomain);
          }
        }
      }
      // Check Input port and declare block if not declared, pass default
      // domain if available from output port or set default mode from input
      // port
      std::shared_ptr<DeclarationNode> inputPortBlock =
          ASTQuery::getModuleMainInputPort(decl);
      if (inputPortBlock) {
        ASTNode portEntity = inputPortBlock->getPropertyValue("block");
        ASTNode inputDomain;
        if (!inputPortBlock->getCompilerProperty("anonymous")) {
          inputDomain = std::make_shared<MemberAccessNode>(
              inputPortBlock->getName(), "domain", __FILE__, __LINE__);
        }

        if (!mainPortsDefaultDomain && inputDomain) { // If not set from output
                                                      // port, set here
          mainPortsDefaultDomain = inputDomain;
        }
        // First give port a block and its declaration if it doesn't have
        // one.
        if (!portEntity || portEntity->getNodeType() == AST::None) {
          std::string possibleName = "Input";
          auto inputDecl = ASTQuery::findDeclarationByName(
              possibleName, ScopeStack(), internalBlocks);
          int counter = 0;
          while (inputDecl) {
            possibleName = "Input_" + std::to_string(counter);
            inputDecl = ASTQuery::findDeclarationByName(
                possibleName, ScopeStack(), internalBlocks);
            counter++;
          }
          portEntity =
              std::make_shared<EntityNode>(possibleName, __FILE__, __LINE__);
          inputPortBlock->setPropertyValue("block", portEntity);
        }

        // Add declaration if not there
        // Port blocks can only be internal, so look for declaration only in
        // internal blocks

        auto portBlockDecl = findDeclarationWithinFunctionScope(
            portEntity, {{node, internalBlocks->getChildren()}});
        if (!portBlockDecl) {
          // FIXME this has to check wether block is a bundle and create
          // declaration accordingly
          portBlockDecl = createSignalDeclaration(
              ASTQuery::getNodeName(portEntity), 1, subScope, m_tree);
          internalBlocks->addChild(portBlockDecl);
          ASTFunctions::fillDefaultPropertiesForNode(portBlockDecl,
                                                     m_tree->getChildren());
        }

        // Now check if domain set, if not, set to port domain
        ASTNode domainNode = CodeAnalysis::getNodeDomain(
            portEntity, {{node, internalBlocks->getChildren()}}, m_tree);
        if (!domainNode || domainNode->getNodeType() == AST::None) {
          if (portBlockDecl && mainPortsDefaultDomain &&
              !ASTQuery::isConstant(portBlockDecl,
                                    {{node, internalBlocks->getChildren()}},
                                    m_tree)) {
            portBlockDecl->setPropertyValue("domain", mainPortsDefaultDomain);
          }
        }
      }

      // Find OutputPort node or give a default Block and domain
      auto ports =
          std::static_pointer_cast<ListNode>(decl->getPropertyValue("ports"));

      internalBlocks = decl->getPropertyValue("blocks");
      // Then go through ports autodeclaring blocks
      if (ports && ports->getNodeType() == AST::List) {
        for (const ASTNode &port : ports->getChildren()) {
          assert(port->getNodeType() == AST::Declaration);
          auto portDeclaration =
              std::static_pointer_cast<DeclarationNode>(port);

          // Properties that we need to auto-declare for
          ASTNode blockPortValue = portDeclaration->getPropertyValue("block");
          auto portDomain = portDeclaration->getPropertyValue("domain");
          if (!portDomain &&
              !portDeclaration->getCompilerProperty("anonymous")) {
            portDomain = std::make_shared<MemberAccessNode>(
                portDeclaration->getName(), "domain", __FILE__, __LINE__);
          }
          auto portRate = std::make_shared<MemberAccessNode>(
              portDeclaration->getName(), "rate", __FILE__, __LINE__);

          if (blockPortValue) {
            //                        // Now do auto declaration of IO
            //                        blocks if not declared.
            assert(blockPortValue->getNodeType() == AST::Entity ||
                   blockPortValue->getNodeType() ==
                       AST::None); // Catch on debug but fail gracefully on
                                   // release
            if (blockPortValue->getNodeType() == AST::Entity) {
              std::shared_ptr<EntityNode> nameNode =
                  std::static_pointer_cast<EntityNode>(blockPortValue);
              std::string name = nameNode->getName();

              auto portEntityDecl = findDeclarationWithinFunctionScope(
                  nameNode, {{node, internalBlocks->getChildren()}});
              if (!portEntityDecl) { // If entity is given but not
                                     // declared, declare
                                     // and assign port domain
                int size = 1;
                portEntityDecl =
                    createSignalDeclaration(name, size, subScope, m_tree);
                portEntityDecl->replacePropertyValue(
                    "rate", std::make_shared<ValueNode>(__FILE__, __LINE__));
                internalBlocks->addChild(portEntityDecl);
              }
              // set its domain to the port domain if not set
              auto declType = ASTQuery::findTypeDeclaration(portEntityDecl,
                                                            subScope, m_tree);
              if (declType) {
                auto inheritList =
                    ASTQuery::getInheritedTypes(declType, subScope, m_tree);
                bool inheritsDomainType = false;
                for (auto inherits : inheritList) {
                  auto typeName = inherits->getPropertyValue("typeName");
                  if (typeName->getNodeType() == AST::String) {
                    if (std::static_pointer_cast<ValueNode>(typeName)
                            ->getStringValue() == "domainMember") {
                      inheritsDomainType = true;
                      break;
                    }
                  }
                }
                if (inheritsDomainType) {
                  ASTNode entityDomain = portEntityDecl->getDomain();
                  if (!entityDomain ||
                      entityDomain->getNodeType() == AST::None) {
                    if (portDomain) {
                      portEntityDecl->setPropertyValue("domain", portDomain);
                    }
                  }
                  if (ASTQuery::isRated(portEntityDecl, m_tree) &&
                      (!portEntityDecl->getPropertyValue("rate") ||
                       portEntityDecl->getPropertyValue("rate")
                               ->getNodeType() == AST::None)) {
                    portEntityDecl->setPropertyValue("rate", portRate);
                  }
                }
              }
            } else if (blockPortValue->getNodeType() == AST::None) {
              // TODO Make sure block name not being used
              std::string defaultName = portDeclaration->getName() + "_Block";
              std::shared_ptr<EntityNode> name =
                  std::make_shared<EntityNode>(defaultName, __FILE__, __LINE__);
              portDeclaration->replacePropertyValue("block", name);
              std::shared_ptr<DeclarationNode> newSignal =
                  ASTQuery::findDeclarationByName(defaultName, ScopeStack(),
                                                  internalBlocks);
              if (!newSignal) {
                newSignal =
                    createSignalDeclaration(defaultName, 1, subScope, m_tree);
                internalBlocks->addChild(newSignal);
                ASTFunctions::fillDefaultPropertiesForNode(
                    newSignal, m_tree->getChildren());
                ASTNode blockDomain = newSignal->getDomain();
                if (!blockDomain || blockDomain->getNodeType() == AST::None) {
                  newSignal->setPropertyValue("domain", portDomain);
                  if (newSignal->getPropertyValue("rate")->getNodeType() ==
                      AST::None) {
                    newSignal->setPropertyValue("rate", portRate);
                  }
                }
              }
            }
          }
        }
      } else if (ports && ports->getNodeType() == AST::None) {
        // If port list is None, then ignore
      } else {
        std::cerr << "ERROR! ports property must be a list or None!"
                  << std::endl;
      }

      // Go through child blocks and autodeclare for internal modules and
      // reactions
      //            ASTNode internalBlocks =
      //            block->getPropertyValue("blocks");
      for (const ASTNode &node : internalBlocks->getChildren()) {
        declareInternalEntitiesForNode(node, subScope);
      }
    }
  }
}

void CodeResolver::resolveStreamSymbols() {
  // FIMXE we need to resolve the streams in the root tree in reverse order
  // as we do for streams within modules.
  for (ASTNode node : m_tree->getChildren()) {
    if (node->getNodeType() == AST::Stream) {
      std::shared_ptr<StreamNode> stream =
          std::static_pointer_cast<StreamNode>(node);
      std::vector<ASTNode> declarations = declareUnknownStreamSymbols(
          stream, nullptr, ScopeStack(),
          m_tree); // FIXME Is this already done in expandParallelFunctions?
      for (const ASTNode &decl : declarations) {
        m_tree->addChild(decl);
      }
      //      expandNamesToBundles(stream, m_tree);
    } else if (node->getNodeType() == AST::Declaration) {
      std::shared_ptr<DeclarationNode> decl =
          std::static_pointer_cast<DeclarationNode>(node);
      ScopeStack scope;
      auto streamBlocks =
          ASTQuery::getContainedStreamBlocks(decl, scope, m_tree);
      if (!streamBlocks.empty()) {
        ScopeStack scopeStack;
        ASTNode blocks = decl->getPropertyValue("blocks");
        if (blocks && blocks->getNodeType() == AST::List) {
          scopeStack.push_back({node, blocks->getChildren()});
        }
        for (auto blockIt = streamBlocks.rbegin();
             blockIt != streamBlocks.rend(); ++blockIt) {
          for (auto sIt = blockIt->streams.rbegin();
               sIt != blockIt->streams.rend(); ++sIt) {
            std::shared_ptr<StreamNode> stream = *sIt;
            std::vector<ASTNode> declarations = declareUnknownStreamSymbols(
                stream, nullptr, scopeStack, m_tree);
            std::shared_ptr<ListNode> blockList =
                std::static_pointer_cast<ListNode>(
                    decl->getPropertyValue("blocks"));
            if (blockList && blockList->getNodeType() == AST::List) {
              for (const ASTNode &newDecl : declarations) {
                blockList->addChild(newDecl);
              }
              // FIXME is this needed ?
              scopeStack.push_back({node, blockList->getChildren()});
            }
          }
        }
      }
    }
  }
}

void CodeResolver::resolveDeclarationSymbols() {
  for (const auto &node : m_tree->getChildren()) {
    resolveNodeSymbols(node, ScopeStack(), m_tree);
  }
}

void CodeResolver::resolveConstants() {
  for (auto override = m_systemConfig.overrides["all"].cbegin();
       override != m_systemConfig.overrides["all"].cend(); ++override) {
    //        std::cerr << override.key();
    std::shared_ptr<DeclarationNode> decl =
        ASTQuery::findDeclarationByName(override->first, {}, m_tree);
    if (decl) {
      if (ASTQuery::isConstant(decl, {}, m_tree)) {
        if (std::holds_alternative<std::string>(override->second)) {
          decl->replacePropertyValue(
              "value",
              std::make_shared<ValueNode>(
                  std::get<std::string>(override->second), __FILE__, __LINE__));
        } else if (std::holds_alternative<int64_t>(override->second)) {
          decl->replacePropertyValue(
              "value",
              std::make_shared<ValueNode>(std::get<int64_t>(override->second),
                                          __FILE__, __LINE__));
        }
      } else {
        std::cerr << "WARNING: Ignoring configuration override '" +
                         override->first + "'. Not constant."
                  << std::endl;
      }
    } else {
      std::cerr << "WARNING: Configuration override not found: " +
                       override->first
                << std::endl;
    }
  }
  for (const ASTNode &node : m_tree->getChildren()) {
    ASTFunctions::resolveConstantsInNode(node, {}, m_tree);
  }
}

void CodeResolver::resolveMemberAccesses() {
  for (const auto &node : m_tree->getChildren()) {
    resolveNodeSymbols(node, ScopeStack(), m_tree);
  }
}

void CodeResolver::setPortEntityAttributesForDeclaration(
    std::shared_ptr<DeclarationNode> decl, ScopeStack stack, ASTNode tree) {

  auto inputPortNode = ASTQuery::getModuleMainInputPort(decl);
  if (inputPortNode) {
    auto portEntityNode = inputPortNode->getPropertyValue("block");
    // TODO allow other options apart from block
    if (portEntityNode && portEntityNode->getNodeType() == AST::Entity) {
      auto entityNode = std::static_pointer_cast<EntityNode>(portEntityNode);

      auto entityDecl = ASTQuery::findDeclarationByName(
          entityNode->getName(), {}, decl->getPropertyValue("blocks"));
      if (entityDecl) {
        setPortAttributesForBlock(entityDecl, inputPortNode->getName());
      }
    }
  }
  auto outputPortNode = ASTQuery::getModuleMainOutputPort(decl);
  if (outputPortNode) {
    auto portEntityNode = outputPortNode->getPropertyValue("block");
    // TODO allow other options apart from block
    if (portEntityNode && portEntityNode->getNodeType() == AST::Entity) {
      auto entityNode = std::static_pointer_cast<EntityNode>(portEntityNode);

      auto entityDecl = ASTQuery::findDeclarationByName(
          entityNode->getName(), {}, decl->getPropertyValue("blocks"));
      if (entityDecl) {
        setPortAttributesForBlock(entityDecl, outputPortNode->getName());
      }
    }
  }
}

void CodeResolver::setPortAttributesForBlock(
    std::shared_ptr<DeclarationNode> blockDecl, std::string portName) {
  // Only typed blocks (e.g. signal) take the data type from the port. Others
  // (switch, string, ...) have a builtin data type.
  if (!ASTQuery::getDataTypeClassNames(blockDecl, m_tree).empty()) {
    auto type = blockDecl->getPropertyValue("type");
    if (!type || type->getNodeType() == AST::None) {
      blockDecl->setPropertyValue(
          "type",
          std::make_shared<MemberAccessNode>(portName, "type", __FILE__,
                                             __LINE__));
    }
  }
  // Blocks that are not rated are reactive and take the rate of the writer
  if (ASTQuery::isRated(blockDecl, m_tree)) {
    auto rate = blockDecl->getPropertyValue("rate");
    if (!rate || rate->getNodeType() == AST::None ||
        (rate->getNodeType() == AST::Int &&
         std::static_pointer_cast<ValueNode>(rate)->getIntValue() < 0)) {
      blockDecl->setPropertyValue(
          "rate",
          std::make_shared<MemberAccessNode>(portName, "rate", __FILE__,
                                             __LINE__));
    }
  }
}

void CodeResolver::propagateDomainsForNode(ASTNode node,
                                           ScopeStack scopeStack) {
  if (node->getNodeType() == AST::Stream) {
    ASTNode contextDomain = mContextDomainStack.back();
    resolveDomainsForStream(std::static_pointer_cast<StreamNode>(node),
                            scopeStack, contextDomain);
  } else if (node->getNodeType() == AST::Declaration) {
    std::shared_ptr<DeclarationNode> module =
        std::static_pointer_cast<DeclarationNode>(node);
    auto streamBlocks =
        ASTQuery::getContainedStreamBlocks(module, scopeStack, m_tree);
    if (!streamBlocks.empty() || module->getPropertyValue("blocks")) {
      std::vector<ASTNode> streamsNode;
      for (const auto &b : streamBlocks) {
        for (const auto &s : b.streams) {
          streamsNode.push_back(s);
        }
      }
      std::vector<ASTNode>::reverse_iterator streamIt = streamsNode.rbegin();

      std::vector<ASTNode> moduleEntities = ASTQuery::getModuleEntities(module);
      ASTNode ports = module->getPropertyValue("ports");

      if (ports && ports->getNodeType() ==
                       AST::List) { // We need to add ports to stack because
                                    // users might need to query their
                                    // properties e.g. Port.domain
        auto portList = ports->getChildren();
        moduleEntities.insert(moduleEntities.begin(), portList.begin(),
                              portList.end());
      }

      scopeStack.push_back({node, moduleEntities});

      auto contextDomainNode = getModuleContextDomain(module);

      while (streamIt != streamsNode.rend()) {
        const ASTNode streamNode = *streamIt;
        if (streamNode->getNodeType() == AST::Stream) {
          resolveDomainsForStream(
              std::static_pointer_cast<StreamNode>(streamNode), scopeStack,
              contextDomainNode);
        } else {
          std::cerr << "ERROR: Expecting stream." << std::endl;
        }
        streamIt++;
      }

      // Now go forward to process anything that was missed
      std::vector<ASTNode>::iterator streamIt2 = streamsNode.begin();
      while (streamIt2 != streamsNode.end()) {
        const ASTNode streamNode = *streamIt2;
        if (streamNode->getNodeType() == AST::Stream) {
          resolveDomainsForStream(
              std::static_pointer_cast<StreamNode>(streamNode), scopeStack,
              contextDomainNode);
        } else {
          std::cerr << "ERROR: Expecting stream." << std::endl;
        }
        streamIt2++;
      }
      //            scopeStack <<
      //            QVector<ASTNode>::fromStdVector(moduleBlocks);
      for (const auto &declNode : moduleEntities) {
        propagateDomainsForNode(declNode, scopeStack);
      }
      //            if (contextDomainDecl) {
      //                setContextDomain(blocks, contextDomainDecl);
      //            }
    }
  }
}

std::shared_ptr<StreamNode>
CodeResolver::makeStreamFromStack(std::vector<ASTNode> &stack) {
  assert(stack.size() > 1);
  ASTNode lastNode = stack.back();
  stack.pop_back();
  std::shared_ptr<StreamNode> newStream = std::make_shared<StreamNode>(
      stack.back(), lastNode, lastNode->getFilename().c_str(),
      lastNode->getLine());
  stack.pop_back();
  while (stack.size() > 0) {
    lastNode = stack.back();
    newStream = std::make_shared<StreamNode>(lastNode, newStream,
                                             lastNode->getFilename().c_str(),
                                             lastNode->getLine());
    stack.pop_back();
  }
  return newStream;
}

std::vector<ASTNode>
CodeResolver::sliceStreamByDomain(std::shared_ptr<StreamNode> stream,
                                  ScopeStack scopeStack) {
  std::vector<ASTNode> streams;
  std::vector<ASTNode> stack;

  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  std::string domainName;
  std::string previousDomainName =
      CodeAnalysis::getNodeDomainName(left, scopeStack, m_tree);
  while (left) {
    domainName = CodeAnalysis::getNodeDomainName(left, scopeStack, m_tree);
    if (left->getNodeType() == AST::Int || left->getNodeType() == AST::Real ||
        left->getNodeType() == AST::Switch) {
      // If constant, ignore domain and go to next member
      stack.push_back(left);
      if (right->getNodeType() == AST::Stream) {
        //            stream = static_pointer_cast<StreamNode>(right);
        StreamNode *subStream = static_cast<StreamNode *>(right.get());
        left = subStream->getLeft();
        right = subStream->getRight();
      } else {
        left = right; // Last pass (process right, call it left)
        stack.push_back(right);
        if (stack.size() != 0) {
          streams.push_back(makeStreamFromStack(stack));
        }
        break;
      }
      // Force it to take the domain from the left to avoid domain slicing
      domainName = CodeAnalysis::getNodeDomainName(left, scopeStack, m_tree);
      if (previousDomainName.size() == 0) {
        previousDomainName = domainName;
      }
    }
    stack.push_back(left);
    if (right->getNodeType() == AST::Stream) {
      //            stream = static_pointer_cast<StreamNode>(right);
      StreamNode *subStream = static_cast<StreamNode *>(right.get());
      left = subStream->getLeft();
      right = subStream->getRight();
    } else {
      stack.push_back(right);
      ASTNode lastNode = nullptr;
      previousDomainName = domainName;
      domainName = CodeAnalysis::getNodeDomainName(right, scopeStack, m_tree);

      if (domainName != previousDomainName) {
        bool skipSlice = false;
        if (right->getNodeType() == AST::Function) {
          std::shared_ptr<FunctionNode> func =
              std::static_pointer_cast<FunctionNode>(right);
          std::shared_ptr<DeclarationNode> decl =
              ASTQuery::findDeclarationByName(func->getName(), scopeStack,
                                              m_tree);
          //                        if (decl->getEntityType() == "reaction"
          //                                || decl->getEntityType() ==
          //                                "loop")
          //                                {
          // TODO hack to avoid slice. The right thing should be to have
          // reactions and loops take the right domain...
          skipSlice = true;
          //                        }
        }
        if (stack.size() > 0) {
          if (left->getNodeType() == AST::Function) {
            skipSlice = true;
          } else if (left->getNodeType() == AST::List) {
            bool allFuncs = true;
            for (const auto &elem : left->getChildren()) {
              if (elem->getNodeType() != AST::Function) {
                allFuncs = false;
                break;
              }
            }
            if (allFuncs) {
              skipSlice = true;
            }
          }
        }

        // Don't slice by domain, this will be taken care of in the code
        // generation.
        //                if (!skipSlice && stack.size() > 2) {
        //                    lastNode = stack.back();
        //                    stack.pop_back();
        //                    auto oneFromLastNode = stack.back();
        //                    streams.push_back(makeStreamFromStack(stack));
        //                    stack.push_back(oneFromLastNode);
        //                    stack.push_back(lastNode);
        //                    // terminateStackWithBridge(lastNode, streams,
        //                    stack, scopeStack);
        //                }
      }
      if (stack.size() != 0) {
        streams.push_back(makeStreamFromStack(stack));
      }

      left = right = nullptr; // Mark last pass;
    }
    previousDomainName = domainName;
  }
  return streams;
}

void CodeResolver::sliceDomainsInNode(std::shared_ptr<DeclarationNode> module,
                                      ScopeStack scopeStack) {
  if (module->getEntityType() == "module" ||
      module->getEntityType() == "reaction" ||
      module->getEntityType() ==
          "loop") { // TODO how to handle different domains within loops and
                    // reactions?
    ASTNode streamsNode = module->getPropertyValue("streams");
    ASTNode blocksNode = module->getPropertyValue("blocks");

    assert(blocksNode);
    if (blocksNode->getNodeType() == AST::None) {
      module->replacePropertyValue(
          "blocks", std::make_shared<ListNode>(nullptr, __FILE__, __LINE__));
      blocksNode = module->getPropertyValue("blocks");
    }

    std::vector<ASTNode> new_tree;
    scopeStack.push_back({module, blocksNode->getChildren()});
    for (const auto &stream : streamsNode->getChildren()) {
      std::vector<ASTNode> streams = sliceStreamByDomain(
          std::static_pointer_cast<StreamNode>(stream), ScopeStack());
      for (const ASTNode &stream : streams) {
        new_tree.push_back(stream);
      }
    }
    streamsNode->setChildren(new_tree);
    for (const auto &declNode : blocksNode->getChildren()) {
      if (declNode->getNodeType() == AST::Declaration) {
        sliceDomainsInNode(std::static_pointer_cast<DeclarationNode>(declNode),
                           scopeStack);
      }
    }
    scopeStack.pop_back();

    //        if (!streamsNode) {
    //           module->setPropertyValue("streams",
    //           std::make_shared<ListNode>(nullptr, __FILE__, __LINE__));
    //           streamsNode = module->getPropertyValue("streams");
    //        }

    std::shared_ptr<ListNode> newStreamsList =
        std::make_shared<ListNode>(nullptr, __FILE__, __LINE__);
    scopeStack.push_back(
        {module, CodeAnalysis::getEntitiesInScope(module, scopeStack, m_tree)});
    if (streamsNode->getNodeType() == AST::List) {
      for (const ASTNode &stream : streamsNode->getChildren()) {
        if (stream->getNodeType() == AST::Stream) {
          std::vector<ASTNode> streams = sliceStreamByDomain(
              std::static_pointer_cast<StreamNode>(stream), scopeStack);
          for (const ASTNode &streamNode : streams) {
            if (streamNode->getNodeType() == AST::Stream) {
              newStreamsList->addChild(streamNode);
            } else if (streamNode->getNodeType() == AST::Declaration ||
                       streamNode->getNodeType() == AST::ArrayDeclaration) {
              blocksNode->addChild(streamNode);
            } else {
              std::cerr << "Stream slicing must result in streams or blocks."
                        << std::endl;
            }
          }
        }
      }
    } else if (streamsNode->getNodeType() == AST::Stream) {
      std::vector<ASTNode> streams = sliceStreamByDomain(
          std::static_pointer_cast<StreamNode>(streamsNode), scopeStack);
      for (const ASTNode &streamNode : streams) {
        if (streamNode->getNodeType() == AST::Stream) {
          newStreamsList->addChild(streamNode);
        } else if (streamNode->getNodeType() == AST::Declaration ||
                   streamNode->getNodeType() == AST::ArrayDeclaration) {
          blocksNode->addChild(streamNode);
        } else {
          std::cerr << "Stream slicing must result in streams or blocks."
                    << std::endl;
        }
      }
    }
    module->replacePropertyValue("streams", newStreamsList);
    for (const auto &declNode : blocksNode->getChildren()) {
      if (declNode->getNodeType() == AST::Declaration) {
        // TODO this is untested and likely not completely working...
        std::shared_ptr<DeclarationNode> decl =
            std::static_pointer_cast<DeclarationNode>(declNode);
        auto internalBlocksNode = decl->getPropertyValue("blocks");
        if (internalBlocksNode) {
          auto subScope = scopeStack;
          subScope.push_back({module, blocksNode->getChildren()});
          sliceDomainsInNode(decl, subScope);
        }
      }
    }
  }
}

ASTNode CodeResolver::getModuleContextDomain(
    std::shared_ptr<DeclarationNode> moduleDecl) {
  auto blocks = moduleDecl->getPropertyValue("blocks");
  auto outputPortBlock = ASTQuery::getModuleMainOutputPort(moduleDecl);
  ASTNode contextDomainBlock;
  bool contextDomainSet = false;
  if (outputPortBlock) {
    if (blocks) {
      contextDomainBlock = CodeAnalysis::getNodeDomain(
          outputPortBlock->getPropertyValue("block"),
          {{moduleDecl, blocks->getChildren()}}, m_tree);
    }
  }
  auto inputPortBlock = ASTQuery::getModuleMainInputPort(moduleDecl);
  if (inputPortBlock && !contextDomainSet) {
    if (blocks) {
      contextDomainBlock = CodeAnalysis::getNodeDomain(
          inputPortBlock->getPropertyValue("block"),
          {{moduleDecl, blocks->getChildren()}}, m_tree);
    }
  }
  return contextDomainBlock;
}

void CodeResolver::resolveDomainForStreamNode(ASTNode node,
                                              ScopeStack scopeStack) {
  ASTNode domain = nullptr;
  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array) {
    std::shared_ptr<DeclarationNode> declaration =
        ASTQuery::findDeclarationByName(ASTQuery::getNodeName(node), scopeStack,
                                        m_tree, node->getNamespaceList());
    if (declaration) {
      auto typeDeclaration =
          ASTQuery::findTypeDeclaration(declaration, scopeStack, m_tree);
      if (typeDeclaration &&
          typeDeclaration->getEntityType() == "platformBlock") {
        domain = node->getCompilerProperty("domain");
      } else {
        domain = static_cast<DeclarationNode *>(declaration.get())->getDomain();
      }
    }
  } else if (node->getNodeType() == AST::Function) {
    domain = static_cast<FunctionNode *>(node.get())->getDomain();
  } else if (node->getNodeType() == AST::List) {
    for (const ASTNode &member : node->getChildren()) {
      resolveDomainForStreamNode(member, scopeStack);
    }
    return;
  } else if (node->getNodeType() == AST::Expression) {
    ExpressionNode *expr = static_cast<ExpressionNode *>(node.get());
    if (expr->isUnary()) {
      resolveDomainForStreamNode(expr->getValue(), scopeStack);
    } else {
      resolveDomainForStreamNode(expr->getLeft(), scopeStack);
      resolveDomainForStreamNode(expr->getRight(), scopeStack);
    }
    return;
  } else if (node->getNodeType() == AST::MemberAccess) {
    domain = node->getCompilerProperty("domain");
  }
}

ASTNode CodeResolver::resolveMemberAccess(
    std::shared_ptr<MemberAccessNode> memberAccess, ScopeStack scopeStack) {
  ASTNode resolved;
  auto decl = ASTQuery::findDeclarationByName(memberAccess->getEntity(),
                                              scopeStack, m_tree);
  if (decl) {
    resolved = decl->getPropertyValue(memberAccess->getPropertyName());
    if (resolved && resolved->getNodeType() == AST::MemberAccess) {
      // Resolve member access recursively
      auto resolvedMemberAccess =
          std::static_pointer_cast<MemberAccessNode>(resolved);
      if (decl->getName() !=
          resolvedMemberAccess->getEntity()) { // To avoid infinite recursion
        resolved = resolveMemberAccess(resolvedMemberAccess, scopeStack);
      }
    }
  }
  return resolved;
}

void CodeResolver::setInputEntityForFunction(std::shared_ptr<FunctionNode> func,
                                             ScopeStack scopeStack,
                                             ASTNode previous) {
  if (previous) {
    func->setCompilerProperty("inputBlock", previous);
  }
}

void CodeResolver::setOutputEntityForFunction(
    std::shared_ptr<FunctionNode> func, ScopeStack scopeStack, ASTNode next) {
  if (next) {
    func->setCompilerProperty("outputBlock", next);
  }
}

void CodeResolver::checkStreamConnections(std::shared_ptr<StreamNode> stream,
                                          ScopeStack scopeStack,
                                          ASTNode previous) {

  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();

  markConnectionForNode(left, scopeStack, previous);

  if (left->getNodeType() == AST::Function) {
    auto func = std::static_pointer_cast<FunctionNode>(left);
    setInputEntityForFunction(func, scopeStack, previous);
  } else if (left->getNodeType() == AST::Array) {
    auto decl = CodeAnalysis::getDeclaration(left);
    if (decl) {
      if (ASTQuery::isConstant(decl, scopeStack, m_tree)) {
        // If constant, then the next stream node is sampling this one.
        auto indexList = std::static_pointer_cast<ArrayNode>(left)->index();

        auto nextDomain =
            CodeAnalysis::getNodeDomain(right, scopeStack, m_tree);
        for (const auto &node : indexList->getChildren()) {
          markConnectionForNode(right, scopeStack, node);
        }
      } else {
        // Otherwise indexed bundle is sampling indeces
        auto indexList = std::static_pointer_cast<ArrayNode>(left)->index();
        for (const auto &node : indexList->getChildren()) {
          markConnectionForNode(left, scopeStack, node);
        }
      }

    } else {
      // Default behavior, will need to be cleaned up later.
      auto indexList = std::static_pointer_cast<ArrayNode>(left)->index();
      for (const auto &node : indexList->getChildren()) {
        markConnectionForNode(right, scopeStack, node);
      }
    }
  } else if (left->getNodeType() == AST::List) {
    if (previous) {
      if (previous->getNodeType() == AST::List &&
          previous->getChildren().size() > 0) {
        auto previousChildren = previous->getChildren();
        std::vector<ASTNode>::iterator connection = previousChildren.begin();
        for (const auto &child : left->getChildren()) {
          if (connection == previousChildren.end()) {
            connection = previousChildren.begin();
          }
          if (child->getNodeType() == AST::Function) {
            auto func = std::static_pointer_cast<FunctionNode>(child);
            setInputEntityForFunction(func, scopeStack, *connection);
          }
          connection++;
        }
      } else {
        for (const auto &child : left->getChildren()) {
          if (child->getNodeType() == AST::Function) {
            auto func = std::static_pointer_cast<FunctionNode>(child);
            setInputEntityForFunction(func, scopeStack, previous);
          }
        }
      }
    }
  } else if (left->getNodeType() == AST::Declaration) {
    left->setCompilerProperty("inputBlock", previous);
    auto decl = std::static_pointer_cast<DeclarationNode>(left);
    auto blocks = decl->getPropertyValue("blocks");
    auto streams = decl->getPropertyValue("streams");

    if (blocks) {
      scopeStack.push_back({decl, blocks->getChildren()});
    }
    if (streams) {
      for (const auto &stream : streams->getChildren()) {
        if (stream->getNodeType() == AST::Stream) {
          checkStreamConnections(std::static_pointer_cast<StreamNode>(stream),
                                 scopeStack, nullptr);
        }
      }
    }
    scopeStack.pop_back();
  }

  if (right->getNodeType() == AST::Function) {
    markConnectionForNode(right, scopeStack, left);
    previous = left;
    auto func = std::static_pointer_cast<FunctionNode>(right);
    setInputEntityForFunction(func, scopeStack, previous);
  } else if (right->getNodeType() == AST::List) {
    previous = left;
    for (const auto &child : right->getChildren()) {
      if (child->getNodeType() == AST::Function) {
        auto func = std::static_pointer_cast<FunctionNode>(child);
        setInputEntityForFunction(func, scopeStack, previous);
      }
    }
  } else if (right->getNodeType() == AST::Declaration) {
    auto decl = std::static_pointer_cast<DeclarationNode>(right);
    auto blocks = decl->getPropertyValue("blocks");
    auto streams = decl->getPropertyValue("streams");
    markConnectionForNode(right, scopeStack, left);
    decl->setCompilerProperty("inputBlock", left);

    if (blocks) {
      scopeStack.push_back({nullptr, blocks->getChildren()});
    }
    if (streams) {
      for (const auto &stream : streams->getChildren()) {
        if (stream->getNodeType() == AST::Stream) {
          checkStreamConnections(std::static_pointer_cast<StreamNode>(stream),
                                 scopeStack, nullptr);
        }
      }
    }
    scopeStack.pop_back();
  } else {
    previous = left;
  }

  if (right->getNodeType() == AST::Stream) {
    checkStreamConnections(std::static_pointer_cast<StreamNode>(right),
                           scopeStack, previous);
    if (left->getNodeType() == AST::Function) {
      auto next = std::static_pointer_cast<StreamNode>(right)->getLeft();
      if (next) {
        if (left->getNodeType() == AST::List) {
          for (const auto &child : left->getChildren()) {
            if (child->getNodeType() == AST::Function) {
              auto func = std::static_pointer_cast<FunctionNode>(child);
              setOutputEntityForFunction(
                  func, scopeStack, next->getCompilerProperty("outputBlock"));
              if (child->getCompilerProperty("outputBlock")) {
                next->setCompilerProperty(
                    "inputBlock", child->getCompilerProperty("outputBlock"));
              }
            }
          }
        } else if (left->getNodeType() == AST::Function) {
          auto func = std::static_pointer_cast<FunctionNode>(left);
          setOutputEntityForFunction(func, scopeStack, next);
          next->setCompilerProperty("inputBlock", func);
        } else {
          auto func = std::static_pointer_cast<FunctionNode>(left);
          setOutputEntityForFunction(func, scopeStack, next);
          next->setCompilerProperty("inputBlock", func);
        }
      }
    } else if (left->getNodeType() == AST::List) {
      for (const auto &child : left->getChildren()) {
        auto next = std::static_pointer_cast<StreamNode>(right)->getLeft();
        //                CodeAnalysis::getNodeNumInputs()
        if (child->getNodeType() == AST::Function) {
          //                    if (right->getNodeType() == AST::Function) {
          //                        right->getCompilerProperty("inputBlock");
          //                    }
          auto func = std::static_pointer_cast<FunctionNode>(child);
          setOutputEntityForFunction(func, scopeStack, next);
          //            if (nextBlock) {
          //                left->setCompilerProperty("outputBlock",
          //                nextBlock);
          //            }
        }
      }
    }
  } else {
    markConnectionForNode(right, scopeStack, previous);
    if (left->getNodeType() == AST::List ||
        left->getNodeType() == AST::Expression) {
      for (const auto &child : left->getChildren()) {
        ASTNode nextBlock = right;
        if (child->getNodeType() == AST::Function) {
          auto func = std::static_pointer_cast<FunctionNode>(child);
          setOutputEntityForFunction(func, scopeStack, nextBlock);
        }
      }
    } else if (left->getNodeType() == AST::Function) {
      ASTNode nextBlock = right;
      auto func = std::static_pointer_cast<FunctionNode>(left);
      setOutputEntityForFunction(func, scopeStack, nextBlock);
    }
  }
}

void CodeResolver::markPreviousReads(ASTNode node, ASTNode previous,
                                     ScopeStack scopeStack) {
  if (previous) {
    std::shared_ptr<ListNode> previousReads;
    ASTNode newReadDomain;
    std::string previousName = ASTQuery::getNodeName(previous);

    std::string name = ASTQuery::getNodeName(node);
    std::shared_ptr<DeclarationNode> decl =
        ASTQuery::findDeclarationByName(name, scopeStack, m_tree);
    if (node->getNodeType() == AST::Declaration) {
      decl = std::static_pointer_cast<DeclarationNode>(node);
    }
    auto nodeTypeName = decl->getEntityType();
    if (nodeTypeName == "module" || nodeTypeName == "reaction" ||
        nodeTypeName == "loop") {
      // Modules, reactions and loops, the domain that matters is the domain
      // of the output block.
      if (node->getNodeType() == AST::Function) {
        auto func = std::static_pointer_cast<FunctionNode>(node);
        // FIXME get this from the functions input port
        newReadDomain = func->getCompilerProperty("domain");
      }
    } else {
      // platformBlock and platformModule can be ignored as they are tied to
      // a domain. However, we do need to figure out how to support buffer
      // access across domains.
      newReadDomain = CodeAnalysis::getNodeDomain(node, scopeStack, m_tree);
      // FIXME we need to get previousReads for functions from the function
      // itsefl, not the module decl.
    }

    auto appendReadDomain = [](std::shared_ptr<ListNode> previousReads,
                               ASTNode newReadDomain) {
      if (previousReads) {
        bool alreadyInReads = false;
        for (const auto &read : previousReads->getChildren()) {
          if (read && read->getNodeType() == AST::MemberAccess &&
              newReadDomain->getNodeType() == AST::MemberAccess) {
            std::shared_ptr<MemberAccessNode> newReadDomainNode =
                std::static_pointer_cast<MemberAccessNode>(newReadDomain);
            std::shared_ptr<MemberAccessNode> existingReadDomain =
                std::static_pointer_cast<MemberAccessNode>(read);
            if ((newReadDomainNode->getEntity() ==
                 existingReadDomain->getEntity()) &&
                (newReadDomainNode->getPropertyName() ==
                 existingReadDomain->getPropertyName())) {
              alreadyInReads = true;
              break;
            }
          } else {
            // FIXME check duplicate string domain names
          }
        }
        if (!alreadyInReads) {
          previousReads->addChild(newReadDomain);
        }
      }
    };
    // TODO we need to support lists here too
    auto previousInstance =
        CodeAnalysis::getInstance(previous, scopeStack, m_tree);
    if (previousInstance) {
      // For nodes that are related to a single instance (Block, Functions,
      // etc.) Mark the writes for the declaration, not the instance
      previousReads = std::static_pointer_cast<ListNode>(
          previousInstance->getCompilerProperty("reads"));
      if (!previousReads) {
        previousInstance->setCompilerProperty(
            "reads", std::make_shared<ListNode>(__FILE__, __LINE__));
        previousReads = std::static_pointer_cast<ListNode>(
            previousInstance->getCompilerProperty("reads"));
      }
    }

    if (previous->getNodeType() == AST::Entity ||
        previous->getNodeType() == AST::Array) {
      if (node->getNodeType() == AST::Array) {
        auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
        //          FIXME process all types of index configurations
        if (arrayNode->index()) {
          for (const auto &indexNode : arrayNode->index()->getChildren()) {
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

      std::function<void(ASTNode, ASTNode)> appendReadDomainForExprList =
          [&](ASTNode exprList, ASTNode newReadDomain) {
            if (exprList->getNodeType() == AST::List ||
                exprList->getNodeType() == AST::Expression) {
              for (const auto &child : exprList->getChildren()) {
                appendReadDomainForExprList(child, newReadDomain);
              }
            } else {
              auto previousInstance =
                  CodeAnalysis::getInstance(exprList, scopeStack, m_tree);
              if (previousInstance) {
                // For nodes that are related to a single instance (Block,
                // Functions, etc.) Mark the writes for the declaration, not
                // the instance
                previousReads = std::static_pointer_cast<ListNode>(
                    previousInstance->getCompilerProperty("reads"));
                appendReadDomain(previousReads, newReadDomain);
              }
            }
          };
      appendReadDomainForExprList(previous, newReadDomain);
    } else if (previous->getNodeType() == AST::MemberAccess) {
      if (newReadDomain) {
        appendReadDomain(previousReads, newReadDomain);
      }
    }
  }
}

void CodeResolver::markConnectionForNode(ASTNode node, ScopeStack scopeStack,
                                         ASTNode previous,
                                         unsigned int listIndex) {
  if (node->getNodeType() == AST::Entity || node->getNodeType() == AST::Array ||
      node->getNodeType() == AST::Function ||
      node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    std::string name = ASTQuery::getNodeName(node);

    if (node->getNodeType() == AST::Array) {
      auto arrayNode = std::static_pointer_cast<ArrayNode>(node);
      auto domain = CodeAnalysis::getNodeDomain(node, scopeStack, m_tree);
      for (const auto &indexChild : arrayNode->index()->getChildren()) {
        markConnectionForNode(indexChild, scopeStack, nullptr);
        if (indexChild->getNodeType() == AST::Entity ||
            indexChild->getNodeType() == AST::Array) {
          auto frameworkNode = indexChild->getCompilerProperty("framework");
          std::string frameworkName;
          if (frameworkNode && frameworkNode->getNodeType() == AST::String) {
            frameworkName = std::static_pointer_cast<ValueNode>(frameworkNode)
                                ->getStringValue();
          }
          auto decl = ASTQuery::findDeclarationByName(
              ASTQuery::getNodeName(indexChild), scopeStack, m_tree,
              indexChild->getNamespaceList(), frameworkName);
          if (decl) {
            if (domain) {
              decl->appendToPropertyValue("reads", domain);
            } else {
            }
          }
        }
      }
    }
    auto nodeInstance = CodeAnalysis::getInstance(node, scopeStack, m_tree);
    if (previous && nodeInstance) {
      std::shared_ptr<ListNode> nodeWritesProperties =
          std::static_pointer_cast<ListNode>(
              nodeInstance->getCompilerProperty("writes"));
      if (!nodeWritesProperties) {
        nodeWritesProperties =
            std::make_shared<ListNode>(nullptr, __FILE__, __LINE__);
        nodeInstance->setCompilerProperty("writes", nodeWritesProperties);
      }

      bool alreadyInWrites = false;
      ASTNode newWriteDomain;

      if (node->getNodeType() == AST::Function) {
        newWriteDomain =
            CodeAnalysis::getNodeDomain(CodeAnalysis::resolveConnectionEntity(
                                            node, scopeStack, m_tree, false),
                                        scopeStack, m_tree);
      } else {
        newWriteDomain = CodeAnalysis::getNodeDomain(node, scopeStack, m_tree);
      }
      for (const auto &write : nodeWritesProperties->getChildren()) {
        if (write && write->getNodeType() == AST::MemberAccess &&
            newWriteDomain &&
            newWriteDomain->getNodeType() == AST::MemberAccess) {
          std::shared_ptr<MemberAccessNode> newReadDomainNode =
              std::static_pointer_cast<MemberAccessNode>(newWriteDomain);
          std::shared_ptr<MemberAccessNode> existingReadDomain =
              std::static_pointer_cast<MemberAccessNode>(write);
          if ((newReadDomainNode->getEntity() ==
               existingReadDomain->getEntity()) &&
              (newReadDomainNode->getPropertyName() ==
               existingReadDomain->getPropertyName())) {
            alreadyInWrites = true;
            break;
          }
        }
      }
      if (!alreadyInWrites && newWriteDomain) {
        nodeWritesProperties->addChild(newWriteDomain);
      }
      if (previous) {
        node->setCompilerProperty("mainInput", previous);
        previous->setCompilerProperty("mainOutput", node);
        // TODO Add compiler property for function ports
        // Check duplication of "mainOutput" and "outputBlock"
        // https://github.com/StrideLang/stridegen/issues/2
      }
    }

    std::shared_ptr<DeclarationNode> decl =
        ASTQuery::findDeclarationByName(name, scopeStack, m_tree);
    if (node->getNodeType() == AST::Declaration) {
      decl = std::static_pointer_cast<DeclarationNode>(node);
    }
    if (decl) {
      // Mark trigger connections
      if (decl->getEntityType() == "trigger") {
        if (!decl->getCompilerProperty("triggerSources")) {
          decl->setCompilerProperty(
              "triggerSources", std::make_shared<ListNode>(__FILE__, __LINE__));
        }
        auto sourcesList = decl->getCompilerProperty("triggerSources");
        if (previous) {
          if (previous->getNodeType() == AST::List) {
            sourcesList->addChild(previous->getChildren()[listIndex]);
          } else {
            sourcesList->addChild(previous);
          }
        }
      }
      if (decl->getEntityType() == "iterator") {
        auto doneTrigger = decl->getPropertyValue("done");
        if (doneTrigger) {
          auto doneTriggerDecl = ASTQuery::findDeclarationByName(
              ASTQuery::getNodeName(doneTrigger), scopeStack, m_tree);
          if (doneTriggerDecl) {
            auto triggerDests =
                doneTriggerDecl->getCompilerProperty("triggerDestinations");
            if (!triggerDests) {
              doneTriggerDecl->setCompilerProperty(
                  "triggerDestinations",
                  std::make_shared<ListNode>(__FILE__, __LINE__));
              triggerDests =
                  doneTriggerDecl->getCompilerProperty("triggerDestinations");
            }
            auto children = triggerDests->getChildren();
            if (std::find(children.begin(), children.end(), decl) ==
                children.end()) {
              triggerDests->addChild(decl);
            }
          }
        }
      }

      // Check if previous is trigger
      std::shared_ptr<DeclarationNode> previousDecl;
      if (previous) {
        previousDecl = ASTQuery::findDeclarationByName(
            ASTQuery::getNodeName(previous), scopeStack, m_tree);
        if (previousDecl && previousDecl->getEntityType() == "trigger") {
          if (!previousDecl->getCompilerProperty("triggerDestinations")) {
            previousDecl->setCompilerProperty(
                "triggerDestinations",
                std::make_shared<ListNode>(__FILE__, __LINE__));
          }
          auto triggerDests =
              previousDecl->getCompilerProperty("triggerDestinations");
          triggerDests->addChild(node);
        }
      }

      // Connect reset property to trigger
      auto resetProp = decl->getPropertyValue("reset");
      if (resetProp && (resetProp->getNodeType() == AST::Entity ||
                        resetProp->getNodeType() == AST::Array)) {
        // TODO should trigger bundles be allowed?
        auto triggerDecl = ASTQuery::findDeclarationByName(
            ASTQuery::getNodeName(resetProp), scopeStack, m_tree);
        if (triggerDecl) {
          if (!triggerDecl->getCompilerProperty("triggerResets")) {
            triggerDecl->setCompilerProperty(
                "triggerResets",
                std::make_shared<ListNode>(__FILE__, __LINE__));
          }
          auto resets = triggerDecl->getCompilerProperty("triggerResets");
          bool notRegistered = true;
          for (const auto &registeredDecl : resets->getChildren()) {
            // FIXME check namespace too
            if (std::static_pointer_cast<DeclarationNode>(registeredDecl)
                    ->getName() == decl->getName()) {
              notRegistered = false;
              break;
            }
          }
          if (notRegistered) {
            resets->addChild(decl);
            auto domain = CodeAnalysis::getNodeDomain(node, scopeStack, m_tree);
            decl->appendToPropertyValue("resetDomains", domain);
          }
        }
      }

      if (node->getNodeType() == AST::Function) {
        auto blocks = decl->getPropertyValue("blocks");
        if (blocks) {
          for (const auto &blockDecl : blocks->getChildren()) {
            if (blockDecl->getNodeType() == AST::Declaration) {
              auto decl = std::static_pointer_cast<DeclarationNode>(blockDecl);
              if (decl->getEntityType() == "reaction" ||
                  decl->getEntityType() == "loop") {
                auto streamsNode = decl->getPropertyValue("streams");
                auto blocksNode = decl->getPropertyValue("blocks");

                if (streamsNode) {
                  if (blocksNode) {
                    scopeStack.push_back({node, blocksNode->getChildren()});
                  }
                  for (const auto &stream : streamsNode->getChildren()) {
                    if (stream->getNodeType() == AST::Stream) {
                      checkStreamConnections(
                          std::static_pointer_cast<StreamNode>(stream),
                          scopeStack);
                    }
                  }
                  scopeStack.pop_back();
                }
              }
            }
          }
        }
      }

      // For the declaration too
      std::shared_ptr<ListNode> readsProperties =
          std::static_pointer_cast<ListNode>(
              decl->getCompilerProperty("reads"));
      if (!readsProperties) {
        readsProperties = std::make_shared<ListNode>(
            nullptr, node->getFilename().c_str(), node->getLine());
        decl->setCompilerProperty("reads", readsProperties);
      }
      std::shared_ptr<ListNode> writesProperties =
          std::static_pointer_cast<ListNode>(
              decl->getCompilerProperty("writes"));
      if (!writesProperties) {
        writesProperties = std::make_shared<ListNode>(
            nullptr, node->getFilename().c_str(), node->getLine());
        decl->setCompilerProperty("writes", writesProperties);
      }
      if (previous) {
        //        std::string previousName;
        std::shared_ptr<ListNode> previousReads;
        //                    std::shared_ptr<DeclarationNode> previousDecl;
        if (previous->getNodeType() == AST::Entity ||
            previous->getNodeType() == AST::Array) {
          markPreviousReads(node, previous, scopeStack);
        } else if (previous->getNodeType() == AST::Expression) {
          auto expr = std::static_pointer_cast<ExpressionNode>(previous);
          for (const auto &child : expr->getChildren()) {
            markConnectionForNode(child, scopeStack, nullptr);
            markPreviousReads(node, child, scopeStack);
            //                        previousReads =
            //                        static_pointer_cast<ListNode>(expr->getCompilerProperty("reads"));
            //                        assert(previousReads);
          }
          //                    previousReads->addChild(decl->getDomain());
        } else if (previous->getNodeType() == AST::List) {
          // How should lists be handled?
          for (const auto &child : previous->getChildren()) {
            markConnectionForNode(child, scopeStack, nullptr);
            markPreviousReads(node, child, scopeStack);
          }
        } else if (previous->getNodeType() == AST::Function) {
          // FIXME assumes the output port of the module takes the output
          // port domain
          writesProperties->addChild(decl->getDomain());
        } else if (previous->getNodeType() == AST::Int ||
                   previous->getNodeType() == AST::Real ||
                   previous->getNodeType() == AST::Switch ||
                   previous->getNodeType() == AST::String) {
          // FIXME constant writes should be moved to the init domain
          // related to the signal's declared domain.
          writesProperties->addChild(decl->getDomain());
        }
      }
    }
  } else if (node->getNodeType() == AST::Int ||
             node->getNodeType() == AST::Real ||
             node->getNodeType() == AST::Switch ||
             node->getNodeType() == AST::String) {
    //        // First create read and write compiler properties for the
    //        node if not there std::shared_ptr<ListNode>
    //        nodeReadsProperties =
    //        static_pointer_cast<ListNode>(node->getCompilerProperty("reads"));
    //        if (!nodeReadsProperties) {
    //            nodeReadsProperties = std::make_shared<ListNode>(nullptr,
    //            node->getFilename().c_str(), node->getLine());
    //            node->setCompilerProperty("reads", nodeReadsProperties);
    //        }
    // Writes property is created but should be empty for ValueNode
    std::shared_ptr<ListNode> nodeWritesProperties =
        std::static_pointer_cast<ListNode>(node->getCompilerProperty("writes"));
    if (!nodeWritesProperties) {
      nodeWritesProperties = std::make_shared<ListNode>(
          nullptr, node->getFilename().c_str(), node->getLine());
      node->setCompilerProperty("writes", nodeWritesProperties);
    }

    if (previous) {
      //      std::string previousName;
      std::shared_ptr<ListNode> previousReads;
      if (previous->getNodeType() == AST::Entity ||
          previous->getNodeType() == AST::Array) {
        markPreviousReads(node, previous, scopeStack);
      } else if (previous->getNodeType() == AST::Expression) {
        auto expr = std::static_pointer_cast<ExpressionNode>(previous);
        for (const auto &child : expr->getChildren()) {
          markConnectionForNode(child, scopeStack, nullptr);
          markPreviousReads(node, child, scopeStack);
          //                        previousReads =
          //                        static_pointer_cast<ListNode>(expr->getCompilerProperty("reads"));
          //                        assert(previousReads);
        }
        //                    previousReads->addChild(decl->getDomain());
      } else if (previous->getNodeType() == AST::List) {
        // How should lists be handled?
      }
    }
  } else if (node->getNodeType() == AST::Expression) {
    for (const auto &child : node->getChildren()) {
      markConnectionForNode(child, scopeStack, previous);
    }
    previous = node;
  } else if (node->getNodeType() == AST::List) {
    int index = 0;
    for (const auto &child : node->getChildren()) {
      int inputSize = CodeAnalysis::getNodeNumInputs(child, scopeStack, m_tree);
      markConnectionForNode(child, scopeStack, previous, index);
      index += inputSize;
    }
    previous = node;
  }
}

void CodeResolver::storeDeclarationsForNode(ASTNode node, ScopeStack scopeStack,
                                            ASTNode tree) {
  if (node->getNodeType() == AST::Function) {
    auto cachedDecl = std::static_pointer_cast<DeclarationNode>(
        node->getCompilerProperty("declaration"));
    std::shared_ptr<DeclarationNode> decl = cachedDecl;
    if (!decl) {
      decl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(node),
                                             scopeStack, tree);
      if (decl && decl->getEntityType() != "platformModule") {
        node->setCompilerProperty("declaration", decl);
      }
    }
    std::vector<ASTNode> scopeBlocks;
    if (decl) {
      auto blocks = decl->getCompilerProperty("blocks");
      if (blocks) {
        for (const auto &b : blocks->getChildren()) {
          scopeBlocks.push_back(b);
        }
      }
    }
    scopeStack.push_back({node, scopeBlocks});
  } else if (node->getNodeType() == AST::Entity ||
             node->getNodeType() == AST::Array) {
    auto cachedDecl = std::static_pointer_cast<DeclarationNode>(
        node->getCompilerProperty("declaration"));
    if (!cachedDecl) {
      auto decl = ASTQuery::findDeclarationByName(ASTQuery::getNodeName(node),
                                                  scopeStack, tree);
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
    auto streamNode = stream->getLeft();
    while (streamNode) {
      storeDeclarationsForNode(streamNode, scopeStack, m_tree);
      if (stream) {
        if (stream->getRight()->getNodeType() == AST::Stream) {
          stream = std::static_pointer_cast<StreamNode>(stream->getRight());
          streamNode = stream->getLeft();
        } else {
          streamNode = stream->getRight();
          stream = nullptr;
        }
      } else {
        streamNode = nullptr;
      }
    }
  } else if (node->getNodeType() == AST::Declaration) {
    // TODO support recursive modules (modules declared in modules)
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() == "module" ||
        decl->getEntityType() == "reaction" ||
        decl->getEntityType() == "loop") {
      auto internalStreams = decl->getPropertyValue("streams");
      auto internalBlocks = decl->getPropertyValue("blocks");
      std::vector<ASTNode> blocksList;
      if (internalBlocks) {
        if (internalBlocks->getNodeType() == AST::List) {
          for (const auto &child : internalBlocks->getChildren()) {
            storeDeclarationsForNode(
                child, {{node, internalBlocks->getChildren()}}, m_tree);
            if (child->getNodeType() == AST::Declaration ||
                child->getNodeType() == AST::ArrayDeclaration) {
              blocksList.push_back(
                  std::static_pointer_cast<DeclarationNode>(child));
            } else {
              std::cerr << "Unexpected node type in internal blocks"
                        << std::endl;
            }
          }
        } else if (internalBlocks->getNodeType() == AST::Declaration ||
                   internalBlocks->getNodeType() == AST::ArrayDeclaration) {
          blocksList.push_back(
              std::static_pointer_cast<DeclarationNode>(internalBlocks));
        }
      }
      ScopeStack stack;
      if (decl->getEntityType() == "reaction" ||
          decl->getEntityType() == "loop") {
        // Loops and reactions have access to their parent scope
        if (scopeStack.size() > 0) {
          stack.push_back(scopeStack.back());
        }
      }
      stack.push_back({decl, blocksList});
      if (internalStreams) {
        if (internalStreams->getNodeType() == AST::List) {
          for (const auto &node : internalStreams->getChildren()) {
            storeDeclarationsForNode(node, stack, m_tree);
          }
        }
      }
    }
  } else if (node->getNodeType() == AST::List ||
             node->getNodeType() == AST::Expression) {
    for (const auto &listNode : node->getChildren()) {
      storeDeclarationsForNode(listNode, scopeStack, m_tree);
    }
  }
  if (node->getNodeType() == AST::Function) {
    scopeStack.pop_back();
  }
}

void CodeResolver::resolveTypeCastForNode(ASTNode node, ScopeStack scopeStack,
                                          ASTNode tree) {

  if (node->getNodeType() == AST::Stream) {
    resolveTypeCastForStream(std::static_pointer_cast<StreamNode>(node),
                             scopeStack, tree);
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {

    resolveTypeCastForDeclaration(
        std::static_pointer_cast<DeclarationNode>(node), scopeStack, tree);
  }
}

void CodeResolver::resolveTypeCastForStream(std::shared_ptr<StreamNode> stream,
                                            ScopeStack scopeStack,
                                            ASTNode tree) {
  auto left = stream->getLeft();
  auto right = stream->getRight();
  auto next = right;

  while (left) {
    if (right && right->getNodeType() == AST::Stream) {
      next = std::static_pointer_cast<StreamNode>(next)->getLeft();
    }
    // TODO more detailed resolution with specific float and int types
    bool isVariadicFunctionCall = false;
    if (left->getNodeType() == AST::List && next &&
        next->getNodeType() == AST::Function) {
      int numIn = CodeAnalysis::getNodeNumInputs(next, scopeStack, tree);
      if (numIn == CodeAnalysis::SIZE_MEMBER_ACCESS) {
        isVariadicFunctionCall = true;
      }
    }
    if ((left->getNodeType() == AST::List && !isVariadicFunctionCall) ||
        left->getNodeType() == AST::Expression) {
      std::vector<std::string> types;
      std::string resolvedType;
      bool needsResolution = false;
      bool validNumericList = true;
      for (const auto &listNode : left->getChildren()) {
        auto nodeType =
            CodeAnalysis::resolveNodeOutDataType(listNode, scopeStack, tree);
        if (nodeType != "_IntType" && nodeType != "_RealType") {
          validNumericList = false;
          break;
        }
        if (resolvedType.size() == 0) {
          resolvedType = nodeType;
        } else if (nodeType != resolvedType) {
          if (nodeType == "_RealType" && resolvedType == "_IntType") {
            resolvedType = "_RealType";
          }
          needsResolution = true;
        }
      }
      if (needsResolution && validNumericList) {
        for (auto &listNode : left->getChildren()) {
          if (CodeAnalysis::resolveNodeOutDataType(listNode, scopeStack,
                                                   tree) != resolvedType) {
            listNode->setCompilerProperty(
                "typecast",
                std::make_shared<ValueNode>(resolvedType, __FILE__, __LINE__));
          }
        }
      }
    }

    if (right) {
      auto leftType =
          CodeAnalysis::resolveNodeOutDataType(left, scopeStack, tree);
      auto nextType =
          CodeAnalysis::resolveNodeOutDataType(next, scopeStack, tree);
      if (!leftType.empty() && !nextType.empty() && leftType != nextType) {
        left->setCompilerProperty(
            "typecast",
            std::make_shared<ValueNode>(nextType, __FILE__, __LINE__));
      }
    }

    if (right) {
      if (right->getNodeType() == AST::Stream) {
        left = std::static_pointer_cast<StreamNode>(right)->getLeft();
        right = std::static_pointer_cast<StreamNode>(right)->getRight();
        next = right;
      } else {
        left = right;
        next = right = nullptr;
      }
    } else {
      left = nullptr;
    }
  }
}

void CodeResolver::resolveTypeCastForDeclaration(
    std::shared_ptr<DeclarationNode> decl, ScopeStack scopeStack,
    ASTNode tree) {
  auto blocks = decl->getPropertyValue("blocks");
  if (blocks) {
    scopeStack.push_back({nullptr, blocks->getChildren()});
    for (const auto &node : blocks->getChildren()) {
      resolveTypeCastForNode(node, scopeStack, tree);
    }
  }

  auto streams = decl->getPropertyValue("streams");
  if (streams) {
    for (const auto &node : streams->getChildren()) {
      resolveTypeCastForNode(node, scopeStack, tree);
    }
  }
}

void CodeResolver::resolveNodeSymbols(ASTNode node, ScopeStack scopeStack,
                                      ASTNode tree) {
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    std::shared_ptr<DeclarationNode> decl =
        std::static_pointer_cast<DeclarationNode>(node);
    auto properties = decl->getProperties();
    // Resolve ports
    for (const auto &prop : properties) {
      auto value = prop->getValue();
      if (value->getNodeType() == AST::MemberAccess) {
        auto memberAccess = std::static_pointer_cast<MemberAccessNode>(value);
        if (memberAccess->getEntity().empty()) {
          // FIXME replacement should be done recursively
          auto replacement =
              decl->getPropertyValue(memberAccess->getPropertyName());
          if (replacement) {
            decl->setPropertyValue(prop->getName(), replacement->deepCopy());
          } else {
            // invalid property
            // TODO report error
          }
        } else { // not a "self" property
        }
      }
    }
  }
}

void CodeResolver::appendParent(std::shared_ptr<DeclarationNode> decl,
                                std::shared_ptr<DeclarationNode> parent) {
  auto parentList = decl->getCompilerProperty("parentDeclarations");
  if (!parentList) {
    parentList = std::make_shared<ListNode>(__FILE__, __LINE__);
    decl->setCompilerProperty("parentDeclarations", parentList);
  }
  if (parent) {
    for (const auto &existing : parentList->getChildren()) {
      if (existing == parent) {
        return; // Already recorded, avoid duplicate entries and redundant
                // traversal
      }
    }
    parentList->addChild(parent);
  }
  if (decl->getEntityType() == "module" ||
      decl->getEntityType() == "reaction" ||
      decl->getEntityType() ==
          "loop") { // declaration types that can have sub scopes
    auto internalBlocks = decl->getPropertyValue("blocks");
    if (internalBlocks) {
      for (const auto &node : internalBlocks->getChildren()) {
        if (node->getNodeType() == AST::Declaration ||
            node->getNodeType() == AST::ArrayDeclaration) {
          auto childDecl = std::static_pointer_cast<DeclarationNode>(node);
          for (const auto &grandParent : parentList->getChildren()) {
            if (grandParent->getNodeType() == AST::Declaration ||
                grandParent->getNodeType() == AST::ArrayDeclaration) {
              appendParent(childDecl, std::static_pointer_cast<DeclarationNode>(
                                          grandParent));
            }
          }
          appendParent(childDecl, decl);
        }
        assert(node->getNodeType() == AST::Declaration ||
               node->getNodeType() == AST::ArrayDeclaration);
      }
    }
  }
}

/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#include "platforminitializer.hpp"

#include "stride/parser/declarationnode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/importnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/platformnode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/featureresolver.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <set>

using namespace strd;

void PlatformInitializer::initializeSystemAndPolicies() {
  std::vector<std::shared_ptr<ImportNode>> allImports =
      ASTQuery::getImportNodes(m_ctx.tree);
  std::vector<std::shared_ptr<ImportNode>> importList;
  for (const auto &imp : allImports) {
    auto cond = imp->getCompilerProperty("_feature_condition");
    if (!cond || FeatureResolver::isConditionMet(cond, m_ctx.featureConfig)) {
      importList.push_back(imp);
    }
  }
  std::vector<std::shared_ptr<SystemNode>> systems =
      ASTQuery::getSystemNodes(m_ctx.tree);

  std::vector<std::string> includePaths;
  if (m_ctx.tree) {
    std::error_code ec;
    std::filesystem::path programPath = m_ctx.tree->getFilename();
    if (!programPath.empty()) {
      auto absolute = std::filesystem::absolute(programPath, ec);
      if (!ec && absolute.has_parent_path()) {
        includePaths.push_back(absolute.parent_path().generic_string());
      }
    }
  }

  if (!systems.empty()) {
    auto platformDir = std::filesystem::path(m_ctx.striderootDir) /
                       "platforms" / systems.front()->platformName();
    for (const char *sub : {"boards", "devices", "cores"}) {
      if (std::filesystem::is_directory(platformDir / sub)) {
        includePaths.push_back((platformDir / sub).generic_string());
      }
    }
    auto cores = std::filesystem::path(m_ctx.striderootDir) / "cores";
    if (std::filesystem::is_directory(cores)) {
      includePaths.push_back(cores.generic_string());
    }
  }

  if (!systems.empty()) {
    const auto &platformNode = systems.front();
    m_ctx.system = std::make_shared<StrideSystem>(
        m_ctx.striderootDir, platformNode->platformName(),
        platformNode->majorVersion(), platformNode->minorVersion(), importList,
        m_ctx.featureConfig, includePaths);
    m_ctx.system->setSystemConfiguration(m_ctx.systemConfig);

    std::string targetDev;
    if (!platformNode->hwPlatforms().empty()) {
      targetDev = platformNode->hwPlatforms().front();
    }
    m_ctx.policyManager.loadPlatform(m_ctx.striderootDir,
                                     platformNode->platformName(), targetDev);
  } else {
    m_ctx.system = std::make_shared<StrideSystem>(
        m_ctx.striderootDir, "", -1, -1, importList, m_ctx.featureConfig,
        includePaths);
    m_ctx.policyManager.loadPlatform(m_ctx.striderootDir, "JIT", "");
  }
}

void PlatformInitializer::processSystem() {
  std::vector<std::shared_ptr<ImportNode>> importList;
  for (const ASTNode &node : m_ctx.tree->getChildren()) {
    if (!node || node->getNodeType() != AST::Import) {
      continue;
    }
    auto import = std::static_pointer_cast<ImportNode>(node);
    auto cond = import->getCompilerProperty("_feature_condition");
    if (cond && !FeatureResolver::isConditionMet(cond, m_ctx.featureConfig)) {
      continue;
    }
    bool imported = false;
    for (const auto &existing : importList) {
      if (ASTQuery::getImportPath(existing) == ASTQuery::getImportPath(import) &&
          existing->importAlias() == import->importAlias()) {
        imported = true;
        break;
      }
    }
    if (!imported) {
      importList.push_back(import);
    }
  }

  if (m_ctx.system && !m_ctx.system->systemName().empty()) {
    for (const auto &import : importList) {
      m_ctx.system->loadImportTree(ASTQuery::getImportPath(import),
                                   import->importAlias());
    }

    auto platformDomain = m_ctx.system->getPlatformDomain();
    if (platformDomain) {
      std::string domainName = ASTQuery::getNodeName(platformDomain);
      auto existingDomain =
          ASTQuery::findDeclarationByName(domainName, {}, m_ctx.tree,
                                          platformDomain->getNamespaceList());
      if (!existingDomain) {
        for (const auto &objects : m_ctx.system->getImportTrees()) {
          auto domainDecl = ASTQuery::findDeclarationByName(
              domainName, {{nullptr, objects.second}}, nullptr,
              platformDomain->getNamespaceList());
          if (domainDecl) {
            m_ctx.tree->addChild(domainDecl);
            break;
          }
        }
      }
    }
  }
}

const std::optional<ResolvedHardware> &
PlatformInitializer::ensureHardwareResolved() {
  if (m_ctx.hardwareResolved) {
    return m_ctx.resolvedHardware;
  }
  m_ctx.hardwareResolved = true;
  auto systems = ASTQuery::getSystemNodes(m_ctx.tree);
  if (systems.empty() || systems.front()->hwPlatforms().empty()) {
    return m_ctx.resolvedHardware;
  }
  auto systemNode = systems.front();
  HardwareResolver hardwareResolver(m_ctx.striderootDir);
  if (m_ctx.tree) {
    std::error_code ec;
    std::filesystem::path programPath = m_ctx.tree->getFilename();
    auto absolute = std::filesystem::absolute(programPath, ec);
    if (!ec && absolute.has_parent_path()) {
      hardwareResolver.addSearchDirectory(
          absolute.parent_path().generic_string());
    }
  }
  m_ctx.resolvedHardware = hardwareResolver.resolve(
      systemNode->platformName(), systemNode->hwPlatforms().front(),
      &m_ctx.hardwareResolveError);
  return m_ctx.resolvedHardware;
}

void PlatformInitializer::declarePlatformConstants() {
  auto systems = ASTQuery::getSystemNodes(m_ctx.tree);
  if (systems.empty() || systems.front()->hwPlatforms().empty()) {
    return;
  }
  const std::string name = "PlatformClockFrequency";
  for (const auto &child : m_ctx.tree->getChildren()) {
    if (child && child->getNodeType() == AST::Declaration &&
        std::static_pointer_cast<DeclarationNode>(child)->getName() == name) {
      return;
    }
  }
  const auto &hardware = ensureHardwareResolved();
  if (!hardware) {
    return;
  }
  auto clock = hardware->intProperty("clock");
  if (!clock) {
    return;
  }
  auto systemNode = systems.front();
  const std::string fileName = systemNode->getFilename();
  const char *file = fileName.c_str();
  const int line = systemNode->getLine();
  auto properties = std::make_shared<ListNode>(file, line);
  properties->addChild(std::make_shared<PropertyNode>(
      "value", std::make_shared<ValueNode>(*clock, file, line), file, line));
  properties->addChild(std::make_shared<PropertyNode>(
      "type", std::make_shared<EntityNode>("UInt32", file, line), file, line));
  properties->addChild(std::make_shared<PropertyNode>(
      "meta",
      std::make_shared<ValueNode>(
          std::string("The core clock in Hz, from the board (or its device)"),
          file, line),
      file, line));
  m_ctx.tree->addChild(std::make_shared<DeclarationNode>(name, "constant",
                                                         properties, file, line));
}

void PlatformInitializer::overlayFrameworkDataTypes() {
  auto isDataType = [](const std::shared_ptr<DeclarationNode> &decl) {
    return decl->getEntityType() == "frameworkDataType" ||
           decl->getEntityType() == "alias";
  };
  auto hasFramework = [](const std::shared_ptr<DeclarationNode> &decl) {
    return decl->getCompilerProperty("framework") != nullptr;
  };

  std::map<std::string, std::shared_ptr<DeclarationNode>> frameworkTypes;
  for (const auto &objectsPair : m_ctx.system->getImportTrees()) {
    for (const auto &node : objectsPair.second) {
      if (!node || node->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (isDataType(decl) && hasFramework(decl)) {
        frameworkTypes.emplace(decl->getName(), decl);
      }
    }
  }
  if (frameworkTypes.empty()) {
    return;
  }

  auto children = m_ctx.tree->getChildren();
  bool changed = false;
  std::set<std::string> present;
  for (auto &child : children) {
    if (!child || child->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(child);
    if (!isDataType(decl)) {
      continue;
    }
    if (!hasFramework(decl)) {
      auto it = frameworkTypes.find(decl->getName());
      if (it != frameworkTypes.end() && !present.count(decl->getName())) {
        child = it->second;
        changed = true;
      }
    }
    present.insert(decl->getName());
  }
  if (!changed) {
    return;
  }
  m_ctx.tree->setChildren(children);
  ASTFunctions::insertRequiredObjects(m_ctx.tree,
                                      m_ctx.system->getImportTrees());
}

void PlatformInitializer::applyTestingOverrides() {
  auto treeChildren = m_ctx.tree->getChildren();
  for (const auto &platform : m_ctx.system->getFrameworks()) {
    const std::vector<ASTNode> &testingObjs =
        platform->getPlatformTestingObjects();
    for (auto &treeChild : treeChildren) {
      if (!treeChild || (treeChild->getNodeType() != AST::Declaration &&
                         treeChild->getNodeType() != AST::ArrayDeclaration)) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(treeChild);
      for (const ASTNode &testingObj : testingObjs) {
        if (!testingObj ||
            (testingObj->getNodeType() != AST::Declaration &&
             testingObj->getNodeType() != AST::ArrayDeclaration)) {
          std::cerr << "Unexpected node in testing file." << std::endl;
          continue;
        }
        auto testDecl = std::static_pointer_cast<DeclarationNode>(testingObj);
        if (decl->getName() == testDecl->getName()) {
          treeChild = testDecl;
          break;
        }
      }
    }
  }
  m_ctx.tree->setChildren(treeChildren);
}

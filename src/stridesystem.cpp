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

#include "stride/stridegen/stridesystem.hpp"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/stridegen/toolmanager.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

#include "stride/parser/declarationnode.h"
#include "stride/parser/propertynode.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <sstream>

using namespace strd;

namespace {

const char *hostPlatformName() {
#if defined(_WIN32) || defined(Q_OS_WINDOWS)
  return "Windows";
#elif defined(__APPLE__) || defined(Q_OS_MACOS)
  return "macOS";
#elif defined(__linux__) || defined(Q_OS_LINUX)
  return "Linux";
#else
  return "";
#endif
}

std::pair<int, int> parseVersionDir(const std::string &name) {
  auto dot = name.find('.');
  if (dot == std::string::npos) {
    return {-1, -1};
  }
  try {
    int maj = std::stoi(name.substr(0, dot));
    int min = std::stoi(name.substr(dot + 1));
    return {maj, min};
  } catch (...) {
    return {-1, -1};
  }
}

std::string findLatestVersionDir(const std::filesystem::path &baseDir) {
  std::error_code ec;
  if (!std::filesystem::exists(baseDir, ec) ||
      !std::filesystem::is_directory(baseDir, ec)) {
    return {};
  }
  std::string bestName;
  std::pair<int, int> bestVer{-1, -1};
  for (const auto &entry : std::filesystem::directory_iterator(baseDir, ec)) {
    if (ec || !entry.is_directory(ec)) {
      continue;
    }
    std::string dirName = entry.path().filename().generic_string();
    auto ver = parseVersionDir(dirName);
    if (bestName.empty() || ver > bestVer ||
        (ver == bestVer && dirName > bestName)) {
      bestVer = ver;
      bestName = std::move(dirName);
    }
  }
  return bestName;
}

void mergeSystemNodes(std::map<std::string, std::vector<ASTNode>> &dest,
                      const std::map<std::string, std::vector<ASTNode>> &src) {
  for (const auto &[ns, nodes] : src) {
    auto &target = dest[ns];
    target.insert(target.end(), nodes.begin(), nodes.end());
  }
}

} // namespace

StrideSystem::StrideSystem(std::string strideRoot, std::string systemName,
                           int majorVersion, int minorVersion,
                           std::vector<std::shared_ptr<ImportNode>> importList,
                           FeatureConfig featureConfig,
                           std::vector<std::string> includePaths)
    : m_strideRoot(std::move(strideRoot)),
      m_systemName(std::move(systemName)), m_majorVersion(majorVersion),
      m_minorVersion(minorVersion), m_featureConfig(std::move(featureConfig)),
      m_includePaths(std::move(includePaths)) {
  (void)importList;
  if (m_systemName.empty()) {
    LOG_WARNING() << "No System specified." << std::endl;
  } else {
    std::string versionString;
    if (m_majorVersion >= 0 && m_minorVersion >= 0) {
      versionString =
          std::to_string(m_majorVersion) + "." + std::to_string(m_minorVersion);
    } else {
      versionString = findLatestVersionDir(
          std::filesystem::path(m_strideRoot) / "systems" / m_systemName);
      if (versionString.empty()) {
        versionString = findLatestVersionDir(
            std::filesystem::path(m_strideRoot) / "frameworks" / m_systemName);
      }
      if (versionString.empty()) {
        versionString = "1.0";
      }
    }

    m_systemPath = (std::filesystem::path(m_strideRoot) / "systems" /
                    m_systemName / versionString)
                       .generic_string();
    std::string systemFile = m_systemPath + "/System.stride";

    if (std::filesystem::exists(systemFile)) {
      ASTNode systemTree = AST::parseFile(systemFile.c_str(), nullptr);
      if (systemTree) {
        initializeFromSystemTree(systemTree);

        for (const auto &framework : m_frameworks) {
          std::filesystem::path testPath(framework->buildTestingLibPath());
          std::error_code ec;
          if (!std::filesystem::exists(testPath, ec) ||
              !std::filesystem::is_directory(testPath, ec)) {
            continue;
          }
          for (const auto &file :
               std::filesystem::directory_iterator(testPath, ec)) {
            if (ec || !file.is_regular_file(ec) ||
                file.path().extension() != ".stride") {
              continue;
            }
            ASTNode tree =
                AST::parseFile(file.path().generic_string().c_str(), nullptr);
            if (tree) {
              std::string fwAlias =
                  getFrameworkAlias(framework->getFramework());
              for (const auto &node : tree->getChildren()) {
                if (node) {
                  node->setCompilerProperty(
                      "framework",
                      std::make_shared<ValueNode>(fwAlias, __FILE__, __LINE__));
                }
              }
              framework->addTestingTree(file.path().stem().generic_string(),
                                        tree);
            } else {
              for (const auto &error : AST::getParseErrors()) {
                LOG_ERROR() << error.getErrorText() << std::endl;
              }
            }
          }
        }
      } else {
        LOG_ERROR() << "Error parsing system tree in: " << systemFile
                    << std::endl;
      }
    } else {
      std::filesystem::path fwPath = std::filesystem::path(m_strideRoot) /
                                     "frameworks" / m_systemName /
                                     versionString;
      if (std::filesystem::exists(fwPath)) {
        m_frameworks.push_back(std::make_shared<StrideFramework>(
            m_strideRoot, m_systemName, versionString, "", "", m_systemName));
      } else {
        m_errors.push_back("System not found");
        LOG_ERROR() << "System file not found: " << systemFile << std::endl;
      }
    }
  }

  m_library.initializeLibrary(m_strideRoot, m_includePaths);
  mergeSystemNodes(m_systemNodes, m_library.getLibraryMembers());
  for (const auto &fw : m_frameworks) {
    mergeSystemNodes(m_systemNodes, fw->getFrameworkMembers());
  }
}

StrideSystem::~StrideSystem() = default;

std::vector<std::string>
StrideSystem::listAvailableSystems(const std::string &strideroot) {
  std::vector<std::string> outEntries;
  std::filesystem::path dir = std::filesystem::path(strideroot) / "systems";
  std::error_code ec;
  if (std::filesystem::exists(dir, ec) &&
      std::filesystem::is_directory(dir, ec)) {
    for (const auto &dirEntry : std::filesystem::directory_iterator(dir, ec)) {
      if (!ec && dirEntry.is_directory(ec)) {
        outEntries.push_back(dirEntry.path().generic_string());
      }
    }
  }
  return outEntries;
}

void StrideSystem::initializeFromSystemTree(const ASTNode &systemTree) {
  if (!systemTree) {
    return;
  }
  std::vector<std::pair<std::string, std::string>> usedPlatformNames;
  std::vector<std::map<std::string, std::string>> platformDefinitions;

  for (const ASTNode &systemNode : systemTree->getChildren()) {
    if (!systemNode || systemNode->getNodeType() != AST::Declaration) {
      continue;
    }
    auto declaration = std::static_pointer_cast<DeclarationNode>(systemNode);
    const auto &entityType = declaration->getEntityType();
    if (entityType == "platform") {
      std::map<std::string, std::string> definition;
      for (const auto &prop : declaration->getProperties()) {
        if (prop && prop->getValue() &&
            prop->getValue()->getNodeType() == AST::String) {
          definition[prop->getName()] =
              std::static_pointer_cast<ValueNode>(prop->getValue())
                  ->getStringValue();
        }
      }
      platformDefinitions.push_back(std::move(definition));
      m_platformDefinitions.push_back(declaration);
    } else if (entityType == "connection") {
      m_connectionDefinitions.push_back(declaration);
    } else if (entityType == "system") {
      ASTNode platforms = declaration->getPropertyValue("platforms");
      if (!platforms) {
        LOG_ERROR() << "platforms property not found in system definition"
                    << std::endl;
      } else if (platforms->getNodeType() == AST::List) {
        for (const ASTNode &platformName : platforms->getChildren()) {
          if (!platformName || platformName->getNodeType() != AST::Entity) {
            continue;
          }
          auto platformSpecBlock =
              std::static_pointer_cast<EntityNode>(platformName);
          auto platformSpec = ASTQuery::findDeclarationForNode(
              platformSpecBlock, {}, systemTree);
          if (!platformSpec) {
            LOG_ERROR() << "Could not find platform spec "
                        << platformSpecBlock->getName() << std::endl;
            continue;
          }
          auto platformBlock = platformSpec->getPropertyValue("framework");
          auto rootNamespaceBlock =
              platformSpec->getPropertyValue("rootNamespace");
          if (!platformBlock || platformBlock->getNodeType() != AST::String) {
            LOG_ERROR() << "Unexpected framework property in platform spec "
                        << platformSpecBlock->getName() << std::endl;
            continue;
          }
          std::string fw = std::static_pointer_cast<ValueNode>(platformBlock)
                               ->getStringValue();
          std::string rootNs;
          if (rootNamespaceBlock) {
            if (rootNamespaceBlock->getNodeType() == AST::String) {
              rootNs =
                  std::static_pointer_cast<ValueNode>(rootNamespaceBlock)
                      ->getStringValue();
            } else {
              LOG_ERROR() << "Unexpected rootNamespace type in platform spec "
                          << platformSpecBlock->getName() << std::endl;
              continue;
            }
          }
          usedPlatformNames.emplace_back(std::move(fw), std::move(rootNs));
        }
      } else {
        LOG_ERROR() << "Expected list for platforms in system definition"
                    << std::endl;
      }
    }
  }

  for (auto &connection : m_connectionDefinitions) {
    auto sourcePlatform = connection->getPropertyValue("sourceFramework");
    auto destPlatform = connection->getPropertyValue("destinationFramework");
    for (const auto &systemNode : systemTree->getChildren()) {
      if (!systemNode || systemNode->getNodeType() != AST::Declaration) {
        continue;
      }
      auto declaration = std::static_pointer_cast<DeclarationNode>(systemNode);
      if (declaration->getEntityType() != "platform") {
        continue;
      }
      auto fwProp = declaration->getPropertyValue("framework");
      if (!fwProp || fwProp->getNodeType() != AST::String) {
        continue;
      }
      if (sourcePlatform && sourcePlatform->getNodeType() == AST::Entity &&
          declaration->getName() ==
              std::static_pointer_cast<EntityNode>(sourcePlatform)->getName()) {
        connection->replacePropertyValue("sourceFramework", fwProp);
      }
      if (destPlatform && destPlatform->getNodeType() == AST::Entity &&
          declaration->getName() ==
              std::static_pointer_cast<EntityNode>(destPlatform)->getName()) {
        connection->replacePropertyValue("destinationFramework", fwProp);
      }
    }
  }

  for (const auto &usedPlatformName : usedPlatformNames) {
    for (auto &definition : platformDefinitions) {
      if (definition["framework"] == usedPlatformName.first) {
        m_frameworks.push_back(std::make_shared<StrideFramework>(
            m_strideRoot, definition["framework"],
            definition["frameworkVersion"], definition["hardware"],
            definition["hardwareVersion"], definition["rootNamespace"]));
      }
    }
  }
}

std::vector<std::string> StrideSystem::listAvailableImports() const {
  std::vector<std::string> outEntries;
  std::error_code ec;
  std::filesystem::path dir =
      std::filesystem::path(m_strideRoot) / "library" / "1.0";
  if (std::filesystem::exists(dir, ec) &&
      std::filesystem::is_directory(dir, ec)) {
    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
      if (!ec && entry.is_directory(ec)) {
        outEntries.push_back(entry.path().generic_string());
      }
    }
  }
  for (const auto &fw : m_frameworks) {
    std::filesystem::path fwDir(fw->buildPlatformLibPath());
    if (std::filesystem::exists(fwDir, ec) &&
        std::filesystem::is_directory(fwDir, ec)) {
      for (const auto &entry : std::filesystem::directory_iterator(fwDir, ec)) {
        if (ec || !entry.is_directory(ec)) {
          continue;
        }
        std::string fname = entry.path().filename().string();
        if (!fname.empty() &&
            std::isupper(static_cast<unsigned char>(fname[0]))) {
          std::string gs = entry.path().generic_string();
          if (std::find(outEntries.begin(), outEntries.end(), gs) ==
              outEntries.end()) {
            outEntries.push_back(std::move(gs));
          }
        }
      }
    }
  }
  return outEntries;
}

std::vector<std::string> StrideSystem::getPlatformTypeNames() const {
  std::vector<std::string> typeNames;
  for (const auto &[ns, nodes] : m_systemNodes) {
    (void)ns;
    for (const auto &node : nodes) {
      if (!node || node->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      const auto &et = decl->getEntityType();
      if (et == "type" || et == "platformModule" || et == "platformEntity") {
        std::string tn = ASTQuery::stringProperty(decl, "typeName");
        if (!tn.empty()) {
          typeNames.push_back(std::move(tn));
        }
      }
    }
  }
  return typeNames;
}

std::vector<std::string> StrideSystem::getFunctionNames() const {
  std::vector<std::string> funcNames;
  for (const auto &[ns, nodes] : m_systemNodes) {
    for (const auto &node : nodes) {
      if (!node || node->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "module") {
        funcNames.push_back(ns.empty() ? decl->getName()
                                       : (ns + ":" + decl->getName()));
      }
    }
  }
  return funcNames;
}

ASTNode
StrideSystem::getPlatformDomain(const std::string &namespaceName) const {
  auto it = m_systemNodes.find(namespaceName);
  if (it == m_systemNodes.end()) {
    return nullptr;
  }
  for (const ASTNode &object : it->second) {
    if (object && object->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(object);
      if (decl->getName() == "PlatformDomain" &&
          decl->getEntityType() == "alias") {
        return decl->getPropertyValue("entity");
      }
    }
  }
  return nullptr;
}

std::map<std::string, std::string>
StrideSystem::getFrameworkTools(const std::string &namespaceName) const {
  std::map<std::string, std::string> tools;
  auto it = m_systemNodes.find(namespaceName);
  if (it == m_systemNodes.end()) {
    LOG_ERROR() << "Invalid framework namespace: " << namespaceName
                << std::endl;
    return tools;
  }
  ToolManager toolManager(m_strideRoot);
  for (const ASTNode &object : it->second) {
    if (!object || object->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(object);
    if (decl->getEntityType() != "toolRequirement") {
      continue;
    }
    std::string toolInstance = ASTQuery::stringProperty(decl, "toolInstance");
    if (toolInstance.empty()) {
      LOG_ERROR() << "Expected toolInstance in toolRequirement "
                  << decl->getName() << std::endl;
      continue;
    }
    std::string resolved = toolManager.findTool(toolInstance);
    if (!resolved.empty()) {
      tools[toolInstance] = std::move(resolved);
    } else {
      LOG_ERROR() << "Tool instance '" << toolInstance
                  << "' not found in local config" << std::endl;
    }
  }
  return tools;
}

std::map<std::string, std::string>
StrideSystem::getFrameworkPaths(const std::string &namespaceName) const {
  std::map<std::string, std::string> paths;
  auto it = m_systemNodes.find(namespaceName);
  if (it == m_systemNodes.end()) {
    LOG_ERROR() << "Invalid framework namespace: " << namespaceName
                << std::endl;
    return paths;
  }
  ToolManager toolManager(m_strideRoot);
  for (const ASTNode &object : it->second) {
    if (!object || object->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(object);
    if (decl->getEntityType() != "pathRequirement") {
      continue;
    }
    std::string pathInstance = ASTQuery::stringProperty(decl, "pathInstance");
    if (pathInstance.empty()) {
      LOG_ERROR() << "Expected pathInstance in pathRequirement "
                  << decl->getName() << std::endl;
      continue;
    }
    auto pathIt = toolManager.localPaths.find(pathInstance);
    if (pathIt != toolManager.localPaths.end()) {
      paths[pathInstance] = pathIt->second;
    } else {
      LOG_ERROR() << "Path instance '" << pathInstance
                  << "' not found in local config" << std::endl;
    }
  }
  return paths;
}

std::string StrideSystem::substituteTokens(const std::string &namespaceName,
                                           std::string text) const {
  auto paths = getFrameworkPaths(namespaceName);
  for (const auto &[key, value] : paths) {
    const std::string token = "%" + key + "%";
    size_t index = 0;
    while ((index = text.find(token, index)) != std::string::npos) {
      text.replace(index, token.size(), value);
      index += value.size();
    }
  }

  size_t index = 0;
  while (index < text.size()) {
    size_t start = text.find('%', index);
    if (start == std::string::npos) {
      break;
    }
    size_t end = text.find('%', start + 1);
    if (end == std::string::npos) {
      break;
    }
    std::string tokenName = text.substr(start + 1, end - start - 1);
    bool replaced = false;
    for (const auto &[fw, configs] : m_systemConfig.resourceConfigurations) {
      (void)fw;
      for (const auto &resourceConfig : configs) {
        if (resourceConfig && resourceConfig->getName() == tokenName) {
          if (auto val = resourceConfig->getPropertyValue("value")) {
            std::string rep = AST::toText(val);
            text.replace(start, end - start + 1, rep);
            index = start + rep.size();
            replaced = true;
          }
          break;
        }
      }
      if (replaced) {
        break;
      }
    }
    if (!replaced) {
      index = end;
    }
  }
  return text;
}

std::vector<std::string> StrideSystem::getFrameworkNames() const {
  std::vector<std::string> names;
  for (const auto &platform : m_frameworks) {
    names.push_back(platform->getFramework());
  }
  return names;
}

std::vector<ASTNode>
StrideSystem::getOptionTrees(const std::string &systemPath) {
  std::vector<ASTNode> optionTrees;
  std::filesystem::path optionsDir = std::filesystem::path(systemPath) / "options";
  std::error_code ec;
  if (!std::filesystem::exists(optionsDir, ec) ||
      !std::filesystem::is_directory(optionsDir, ec)) {
    return optionTrees;
  }
  const std::string host = hostPlatformName();
  for (const auto &file :
       std::filesystem::directory_iterator(optionsDir, ec)) {
    if (ec || !file.is_regular_file(ec) ||
        file.path().extension() != ".stride") {
      continue;
    }
    ASTNode optionTree =
        AST::parseFile(file.path().generic_string().c_str(), nullptr);
    if (!optionTree) {
      LOG_ERROR() << "Error parsing option file: "
                  << file.path().generic_string() << std::endl;
      continue;
    }
    ASTNode finalTree = std::make_shared<AST>();
    for (const auto &child : optionTree->getChildren()) {
      bool osMatch = true;
      if (child && child->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(child);
        auto osNode = decl->getPropertyValue("buildPlatforms");
        if (osNode && osNode->getNodeType() == AST::List) {
          osMatch = false;
          for (const auto &validOSNode : osNode->getChildren()) {
            if (validOSNode && validOSNode->getNodeType() == AST::String &&
                std::static_pointer_cast<ValueNode>(validOSNode)
                        ->getStringValue() == host) {
              osMatch = true;
              break;
            }
          }
        }
      }
      if (osMatch) {
        finalTree->addChild(child);
      }
    }
    optionTrees.push_back(std::move(finalTree));
  }
  return optionTrees;
}

std::vector<ASTNode>
StrideSystem::loadImportTree(const std::string &importName,
                             const std::string &importAs,
                             const std::string &frameworkName) {
  auto importedNodes = m_library.loadImport(importName, importAs);
  for (const auto &fw : m_frameworks) {
    if (frameworkName.empty() || frameworkName == fw->getFramework()) {
      auto fwnodes = fw->loadImport(importName, importAs);
      importedNodes.insert(importedNodes.begin(), fwnodes.begin(),
                           fwnodes.end());
    }
  }

  std::vector<ASTNode> filteredNodes;
  for (const auto &node : importedNodes) {
    if (!node) {
      continue;
    }
    if (auto cond = node->getCompilerProperty("_feature_condition")) {
      if (FeatureResolver::isConditionMet(cond, m_featureConfig)) {
        node->removeCompilerProperty("_feature_condition");
        filteredNodes.push_back(node);
      }
    } else {
      filteredNodes.push_back(node);
    }
  }

  auto &dest = m_systemNodes[importAs];
  for (const auto &node : filteredNodes) {
    if (std::find(dest.begin(), dest.end(), node) == dest.end()) {
      dest.push_back(node);
    }
  }
  return filteredNodes;
}

std::vector<std::shared_ptr<DeclarationNode>>
StrideSystem::getFrameworkSynchronization(
    const std::string &frameworkName) const {
  std::vector<std::shared_ptr<DeclarationNode>> syncNodes;
  for (const auto &platform : m_frameworks) {
    if (frameworkName != platform->getRootNamespace() &&
        frameworkName != platform->getFramework()) {
      continue;
    }
    for (const auto &node : platform->getPlatformObjects()) {
      if (node && node->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(node);
        if (decl->getEntityType() == "synchronization") {
          syncNodes.push_back(decl);
        }
      }
    }
  }
  return syncNodes;
}

std::shared_ptr<DeclarationNode>
StrideSystem::getFrameworkDataType(const std::string &frameworkName,
                                   const std::string &strideDataType) const {
  for (const auto &platform : m_frameworks) {
    if (frameworkName != platform->getRootNamespace() &&
        frameworkName != platform->getFramework()) {
      continue;
    }
    auto dataTypeDecl = findDataTypeInFramework(platform, strideDataType);
    std::set<std::string> visited;
    while (dataTypeDecl && dataTypeDecl->getEntityType() == "alias" &&
           visited.insert(dataTypeDecl->getName()).second) {
      auto entity = dataTypeDecl->getPropertyValue("entity");
      if (!entity || entity->getNodeType() != AST::Entity) {
        dataTypeDecl = nullptr;
        break;
      }
      dataTypeDecl =
          findDataTypeInFramework(platform, ASTQuery::getNodeName(entity));
    }
    if (dataTypeDecl && dataTypeDecl->getEntityType() == "alias") {
      dataTypeDecl = nullptr;
    }
    if (dataTypeDecl) {
      return dataTypeDecl;
    }
  }
  return nullptr;
}

std::string StrideSystem::getFrameworkDefaultDataType(
    const std::string &frameworkName) const {
  for (const auto &platform : m_frameworks) {
    if (frameworkName != platform->getRootNamespace() &&
        frameworkName != platform->getFramework()) {
      continue;
    }
    for (const auto &node : platform->getPlatformObjects()) {
      if (!node || node->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if (decl->getEntityType() == "_frameworkDescription") {
        if (auto defaultNode = decl->getPropertyValue("defaultDataType")) {
          std::string name = ASTQuery::valueText(defaultNode);
          if (!name.empty()) {
            return name;
          }
        }
      }
    }
    std::set<std::string> visited;
    return findDefaultDataTypeInPath(platform->buildPlatformLibPath(), visited);
  }
  return {};
}

std::vector<std::shared_ptr<DeclarationNode>>
StrideSystem::getFrameworkOperators(const std::string &frameworkName) const {
  std::vector<std::shared_ptr<DeclarationNode>> operators;
  auto it = m_systemNodes.find(frameworkName);
  if (it != m_systemNodes.end()) {
    for (const auto &node : it->second) {
      if (node && node->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(node);
        if (decl->getEntityType() == "platformOperator") {
          operators.push_back(decl);
        }
      }
    }
  }
  return operators;
}

std::vector<std::pair<std::string, std::string>>
StrideSystem::getFrameworkAliasInherits(
    const std::string &frameworkAlias) const {
  for (const auto &fw : m_frameworks) {
    if (fw->getRootNamespace() == frameworkAlias) {
      return fw->getInheritedList();
    }
  }
  return {};
}

std::string StrideSystem::getDataType(const ASTNode &node,
                                      const ScopeStack &scope,
                                      const ASTNode &tree) const {
  if (!node) {
    return {};
  }
  if (node->getNodeType() == AST::Declaration ||
      node->getNodeType() == AST::ArrayDeclaration) {
    return CodeAnalysis::getDataTypeForDeclaration(
        std::static_pointer_cast<DeclarationNode>(node), scope, tree);
  }
  if (auto decl = ASTQuery::findDeclarationForNode(node, scope, tree)) {
    return CodeAnalysis::getDataTypeForDeclaration(decl, scope, tree);
  }
  return {};
}

std::string
StrideSystem::getFrameworkAlias(const std::string &frameworkName) const {
  for (const auto &platform : m_frameworks) {
    if (frameworkName == platform->getFramework()) {
      return platform->getRootNamespace();
    }
  }
  return frameworkName;
}

std::string
StrideSystem::getFrameworkFromAlias(const std::string &frameworkAlias) const {
  for (const auto &platform : m_frameworks) {
    if (frameworkAlias == platform->getRootNamespace()) {
      return platform->getFramework();
    }
  }
  return frameworkAlias;
}

void StrideSystem::injectResourceConfiguration(const ASTNode &tree) {
  if (!tree) {
    return;
  }
  std::vector<ASTNode> newChildren = tree->getChildren();

  for (const auto &node : tree->getChildren()) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() != "resourceConfiguration") {
      continue;
    }
    std::string framework;
    if (auto fwNode = decl->getCompilerProperty("framework");
        fwNode && fwNode->getNodeType() == AST::String) {
      framework =
          std::static_pointer_cast<ValueNode>(fwNode)->getStringValue();
    }
    auto &fwConfigs = m_systemConfig.resourceConfigurations[framework];
    bool configFound = std::any_of(
        fwConfigs.begin(), fwConfigs.end(),
        [&](const std::shared_ptr<DeclarationNode> &cfg) {
          return cfg && cfg->getName() == decl->getName();
        });
    if (!configFound) {
      auto defaultVal = decl->getPropertyValue("default");
      if (!defaultVal) {
        continue;
      }
      ASTNode propertiesList = std::make_shared<ListNode>(
          decl->getFilename().c_str(), decl->getLine(), decl->getColumn());
      decl->copyLocationTo(propertiesList);
      auto valProp = std::make_shared<PropertyNode>(
          "value", defaultVal->deepCopy(), decl->getFilename().c_str(),
          decl->getLine(), decl->getColumn());
      decl->copyLocationTo(valProp);
      propertiesList->addChild(valProp);
      auto constDecl = std::make_shared<DeclarationNode>(
          decl->getName(), "constant", propertiesList,
          decl->getFilename().c_str(), decl->getLine(), decl->getColumn());
      decl->copyLocationTo(constDecl);
      if (auto fwProp = decl->getCompilerProperty("framework")) {
        constDecl->setCompilerProperty("framework", fwProp);
      }
      fwConfigs.push_back(constDecl);
    }
  }

  for (const auto &[fwName, configs] : m_systemConfig.resourceConfigurations) {
    for (const auto &configEntry : configs) {
      if (!configEntry) {
        continue;
      }
      for (size_t i = 0; i < newChildren.size(); ++i) {
        auto &child = newChildren[i];
        if (!child || (child->getNodeType() != AST::Declaration &&
                       child->getNodeType() != AST::ArrayDeclaration)) {
          continue;
        }
        auto childDecl = std::static_pointer_cast<DeclarationNode>(child);
        if (childDecl->getName() != configEntry->getName()) {
          continue;
        }
        std::string childFw;
        if (auto fwNode = childDecl->getCompilerProperty("framework");
            fwNode && fwNode->getNodeType() == AST::String) {
          childFw =
              std::static_pointer_cast<ValueNode>(fwNode)->getStringValue();
        }
        if (childFw == fwName) {
          newChildren[i] = configEntry;
          break;
        }
      }
    }
  }
  tree->setChildren(newChildren);
}

std::shared_ptr<DeclarationNode>
StrideSystem::getFrameworkOperator(const std::string &platform,
                                   const std::string &leftType,
                                   const std::string &rightType,
                                   const std::string &type) {
  (void)platform;
  (void)leftType;
  (void)rightType;
  (void)type;
  return nullptr;
}

void StrideSystem::installFramework(const std::string &frameworkName) {
  for (const auto &framework : m_frameworks) {
    if (framework->getFramework() == frameworkName) {
      framework->installFramework();
    }
  }
}

std::shared_ptr<DeclarationNode> StrideSystem::findDataTypeInFramework(
    const std::shared_ptr<StrideFramework> &fw,
    const std::string &strideDataType) const {
  if (!fw) {
    return nullptr;
  }
  for (const auto &node : fw->getPlatformObjects()) {
    if (node && node->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      if ((decl->getEntityType() == "frameworkDataType" ||
           decl->getEntityType() == "alias") &&
          decl->getName() == strideDataType) {
        return decl;
      }
    }
  }
  std::set<std::string> visited;
  return findDataTypeInPath(fw->buildPlatformLibPath(), strideDataType,
                            visited);
}

std::shared_ptr<DeclarationNode>
StrideSystem::findDataTypeInPath(const std::string &path,
                                 const std::string &strideDataType,
                                 std::set<std::string> &visited) const {
  if (!visited.insert(path).second) {
    return nullptr;
  }
  auto nodes = ASTFunctions::loadAllInDirectory(path);
  for (const ASTNode &node : nodes) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if ((decl->getEntityType() == "frameworkDataType" ||
         decl->getEntityType() == "alias") &&
        decl->getName() == strideDataType) {
      return decl;
    }
    if (decl->getEntityType() == "_frameworkDescription") {
      std::string importName = ASTQuery::stringProperty(decl, "inherits");
      std::string importVersion =
          ASTQuery::stringProperty(decl, "inheritsVersion");
      if (!importName.empty() && !importVersion.empty()) {
        std::string inheritedPath = m_strideRoot + "/frameworks/" + importName +
                                    "/" + importVersion + "/platformlib";
        if (auto found =
                findDataTypeInPath(inheritedPath, strideDataType, visited)) {
          return found;
        }
      }
    }
  }
  return nullptr;
}

std::string
StrideSystem::findDefaultDataTypeInPath(const std::string &path,
                                        std::set<std::string> &visited) const {
  if (!visited.insert(path).second) {
    return {};
  }
  auto nodes = ASTFunctions::loadAllInDirectory(path);
  for (const ASTNode &node : nodes) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() != "_frameworkDescription") {
      continue;
    }
    if (auto defaultNode = decl->getPropertyValue("defaultDataType")) {
      std::string name = ASTQuery::valueText(defaultNode);
      if (!name.empty()) {
        return name;
      }
    }
    std::string importName = ASTQuery::stringProperty(decl, "inherits");
    std::string importVersion =
        ASTQuery::stringProperty(decl, "inheritsVersion");
    if (!importName.empty() && !importVersion.empty()) {
      std::string inheritedPath = m_strideRoot + "/frameworks/" + importName +
                                  "/" + importVersion + "/platformlib";
      std::string found = findDefaultDataTypeInPath(inheritedPath, visited);
      if (!found.empty()) {
        return found;
      }
    }
  }
  return {};
}

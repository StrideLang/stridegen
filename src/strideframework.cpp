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

#include "stride/stridegen/strideframework.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/cachemanager.h"
#include "stride/utils/logger.h"
#include "stride/utils/stridelibrary.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/wait.h>
#endif

using namespace strd;

namespace {

struct CurrentPathGuard {
  std::filesystem::path previous;
  std::error_code ec;
  explicit CurrentPathGuard(const std::filesystem::path &target) {
    previous = std::filesystem::current_path(ec);
    if (!ec) {
      std::filesystem::current_path(target, ec);
    }
  }
  ~CurrentPathGuard() {
    if (!previous.empty()) {
      std::error_code restoreEc;
      std::filesystem::current_path(previous, restoreEc);
    }
  }
};

void mergeChildOverInherited(std::vector<ASTNode> &baseNodes,
                             const std::vector<ASTNode> &inheritedNodes) {
  for (const auto &inhNode : inheritedNodes) {
    if (!inhNode) {
      continue;
    }
    const std::string inhName = ASTQuery::getNodeName(inhNode);
    bool overridden = std::any_of(
        baseNodes.begin(), baseNodes.end(), [&](const ASTNode &existing) {
          return existing && ASTQuery::getNodeName(existing) == inhName;
        });
    if (!overridden) {
      baseNodes.push_back(inhNode);
    }
  }
}

} // namespace

StrideFramework::StrideFramework(std::string strideRoot, std::string framework,
                                 std::string fwVersion, std::string hardware,
                                 std::string hardwareVersion,
                                 std::string rootNamespace)
    : m_strideRoot(std::move(strideRoot)), m_framework(std::move(framework)),
      m_frameworkVersion(std::move(fwVersion)),
      m_hardware(std::move(hardware)),
      m_hardwareVersion(std::move(hardwareVersion)),
      m_rootNamespace(std::move(rootNamespace)) {
  std::set<std::string> visitedPaths;
  m_inheritedPaths =
      getInheritedFrameworkPaths(buildPlatformLibPath(), visitedPaths);
  m_inheritedList = loadInheritedList(buildPlatformLibPath());
  auto nodes = loadFrameworkRoot(buildPlatformLibPath());
  m_trees.push_back(FrameworkTree{"", "", std::move(nodes), {}});
}

StrideFramework::~StrideFramework() = default;

std::string StrideFramework::getFramework() const { return m_framework; }

std::string StrideFramework::getFrameworkVersion() const {
  return m_frameworkVersion;
}

std::string StrideFramework::getHardware() const { return m_hardware; }

std::string StrideFramework::getHardwareVersion() const {
  return m_hardwareVersion;
}

std::string StrideFramework::getRootNamespace() const {
  return m_rootNamespace;
}

bool StrideFramework::getRequired() const { return m_required; }

StrideFramework::PlatformAPI StrideFramework::getAPI() const { return m_api; }

std::string StrideFramework::buildPlatformPath() const {
  return m_strideRoot + "/frameworks/" + m_framework + "/" + m_frameworkVersion;
}

std::string StrideFramework::buildPlatformLibPath() const {
  return buildPlatformPath() + "/platformlib";
}

std::string StrideFramework::buildTestingLibPath() const {
  return buildPlatformLibPath() + "/testing";
}

std::string StrideFramework::getPlatformDetails() const {
  return buildPlatformLibPath();
}

void StrideFramework::installFramework() {
  std::vector<std::string> frameworkRoots;
  frameworkRoots.push_back(buildPlatformLibPath());

  for (const auto &inh : m_inheritedList) {
    frameworkRoots.push_back(m_strideRoot + "/frameworks/" + inh.first + "/" +
                             inh.second + "/platformlib");
  }

  for (const auto &frameworkRoot : frameworkRoots) {
    std::string fileName = frameworkRoot + "/Configuration.stride";
    if (!std::filesystem::exists(fileName)) {
      continue;
    }
    auto configuration = AST::parseFile(fileName.c_str());
    if (!configuration) {
      continue;
    }
    for (const auto &node : configuration->getChildren()) {
      if (!node || node->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(node);
      auto installationNode = decl->getPropertyValue("installation");
      if (!installationNode) {
        continue;
      }
      for (const auto &installDirectiveNode : installationNode->getChildren()) {
        if (!installDirectiveNode ||
            installDirectiveNode->getNodeType() != AST::Declaration) {
          continue;
        }
        auto installDirective =
            std::static_pointer_cast<DeclarationNode>(installDirectiveNode);
        if (installDirective->getEntityType() != "installAction") {
          continue;
        }
        auto commandNode = installDirective->getPropertyValue("command");
        auto dirNode = installDirective->getPropertyValue("workingDirectory");
        if (!commandNode || commandNode->getNodeType() != AST::String) {
          continue;
        }
        std::string workingDirectory = buildPlatformPath();
        if (dirNode && dirNode->getNodeType() == AST::String) {
          workingDirectory +=
              "/" +
              std::static_pointer_cast<ValueNode>(dirNode)->getStringValue();
        }
        std::string command =
            std::static_pointer_cast<ValueNode>(commandNode)->getStringValue();
        LOG_DEBUG() << "Running install command: " << command << " in "
                    << workingDirectory << std::endl;

        std::error_code dirEc;
        if (!std::filesystem::exists(workingDirectory, dirEc)) {
          LOG_ERROR() << "Working directory does not exist: "
                      << workingDirectory << std::endl;
          continue;
        }
        CurrentPathGuard cwdGuard(workingDirectory);
        if (cwdGuard.ec) {
          LOG_ERROR() << "Failed to switch to directory: " << workingDirectory
                      << std::endl;
          continue;
        }

        std::array<char, 256> buffer{};
        std::string output;
        int exitCode = 0;
#if defined(_WIN32)
        FILE *pipe = _popen(command.c_str(), "r");
        if (!pipe) {
          LOG_ERROR() << "_popen failed for: " << command << std::endl;
          continue;
        }
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
          output += buffer.data();
        }
        exitCode = _pclose(pipe);
#else
        FILE *pipe = popen(command.c_str(), "r");
        if (!pipe) {
          LOG_ERROR() << "popen failed for: " << command << std::endl;
          continue;
        }
        while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
          output += buffer.data();
        }
        int status = pclose(pipe);
        exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : status;
#endif
        if (exitCode != 0) {
          LOG_ERROR() << "Command failed (exit " << exitCode
                      << "): " << command << "\n"
                      << output << std::endl;
        }
      }
    }
  }
}

std::vector<std::string> StrideFramework::getDomainIds() const {
  std::vector<std::string> domainIds;
  for (const auto &treeEntry : m_trees) {
    for (const auto &node : treeEntry.nodes) {
      if (node && node->getNodeType() == AST::Declaration) {
        auto decl = std::static_pointer_cast<DeclarationNode>(node);
        if (decl->getEntityType() == "_domainDefinition") {
          domainIds.push_back(decl->getName());
        }
      }
    }
  }
  return domainIds;
}

void StrideFramework::addTestingTree(const std::string &treeName,
                                     ASTNode treeRoot) {
  m_platformTestTrees[treeName] = std::move(treeRoot);
}

std::map<std::string, std::vector<ASTNode>>
StrideFramework::getFrameworkMembers() const {
  std::map<std::string, std::vector<ASTNode>> libNamespace;
  for (const auto &tree : m_trees) {
    auto &vec = libNamespace[tree.importAs];
    vec.insert(vec.end(), tree.nodes.begin(), tree.nodes.end());
  }
  return libNamespace;
}

std::vector<ASTNode> StrideFramework::getPlatformObjects() const {
  std::vector<ASTNode> objects;
  for (const auto &tree : m_trees) {
    objects.insert(objects.end(), tree.nodes.begin(), tree.nodes.end());
  }
  return objects;
}

std::vector<ASTNode> StrideFramework::getPlatformTestingObjects() const {
  std::vector<ASTNode> objects;
  for (const auto &[name, root] : m_platformTestTrees) {
    (void)name;
    if (root) {
      const auto &children = root->getChildren();
      objects.insert(objects.end(), children.begin(), children.end());
    }
  }
  return objects;
}

bool StrideFramework::getPluginDetails(std::string &pluginName,
                                       int &majorVersion,
                                       int &minorVersion) const {
  for (const auto &tree : m_trees) {
    for (const auto &declNode : tree.nodes) {
      if (!declNode || declNode->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(declNode);
      if (decl->getEntityType() != "generatorDirectives") {
        continue;
      }
      if (auto nameNode = decl->getPropertyValue("codeGenerator");
          nameNode && nameNode->getNodeType() == AST::String) {
        pluginName =
            std::static_pointer_cast<ValueNode>(nameNode)->getStringValue();
      }
      if (auto majorNode =
              decl->getPropertyValue("codeGeneratorMajorVersion");
          majorNode && majorNode->getNodeType() == AST::Int) {
        majorVersion = static_cast<int>(
            std::static_pointer_cast<ValueNode>(majorNode)->getIntValue());
      }
      if (auto minorNode =
              decl->getPropertyValue("codeGeneratorMinorVersion");
          minorNode && minorNode->getNodeType() == AST::Int) {
        minorVersion = static_cast<int>(
            std::static_pointer_cast<ValueNode>(minorNode)->getIntValue());
      }
      return true;
    }
  }
  return false;
}

std::vector<std::pair<std::string, std::string>>
StrideFramework::getInheritedList() const {
  return m_inheritedList;
}

void StrideFramework::tagFrameworkNodes(std::vector<ASTNode> &nodes) const {
  // Framework members always carry a "framework" property holding the
  // framework's root namespace (often ""), distinguishing them from library
  // declarations that have none.
  for (auto &node : nodes) {
    if (!node) {
      continue;
    }
    if (!m_rootNamespace.empty() && !node->getCompilerProperty("namespaceTree")) {
      node->appendToPropertyValue(
          "namespaceTree",
          std::make_shared<ValueNode>(m_rootNamespace, __FILE__, __LINE__));
    }
    if (!node->getCompilerProperty("framework")) {
      node->setCompilerProperty(
          "framework",
          std::make_shared<ValueNode>(m_rootNamespace, __FILE__, __LINE__));
    }
  }
}

std::vector<ASTNode>
StrideFramework::loadFrameworkRoot(const std::string &frameworkRoot) {
  auto nodes = ASTFunctions::loadAllInDirectory(frameworkRoot);
  for (const auto &inhPath : m_inheritedPaths) {
    mergeChildOverInherited(nodes, ASTFunctions::loadAllInDirectory(inhPath));
  }
  tagFrameworkNodes(nodes);
  return nodes;
}

std::vector<std::string>
StrideFramework::getInheritedFrameworkPaths(const std::string &frameworkLibPath,
                                            std::set<std::string> &visited) {
  std::vector<std::string> inhPaths;
  if (!visited.insert(frameworkLibPath).second) {
    return inhPaths;
  }
  auto nodes = ASTFunctions::loadAllInDirectory(frameworkLibPath);
  for (const auto &newNode : nodes) {
    if (!newNode || newNode->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(newNode);
    if (decl->getEntityType() != "_frameworkDescription") {
      continue;
    }
    std::string importName = ASTQuery::stringProperty(decl, "inherits");
    std::string importVersion =
        ASTQuery::stringProperty(decl, "inheritsVersion");
    if (!importName.empty() && !importVersion.empty()) {
      std::string fwRoot = m_strideRoot + "/frameworks/" + importName + "/" +
                           importVersion + "/platformlib";
      inhPaths = getInheritedFrameworkPaths(fwRoot, visited);
      inhPaths.push_back(fwRoot);
    }
  }
  return inhPaths;
}

std::vector<std::pair<std::string, std::string>>
StrideFramework::loadInheritedList(const std::string &frameworkLibPath) {
  std::vector<std::pair<std::string, std::string>> inh;
  std::string frameworkPath = frameworkLibPath + "/Framework.stride";
  ASTNode root = CacheManager::instance().loadCachedAST(frameworkPath);
  if (!root && std::filesystem::exists(frameworkPath)) {
    root = AST::parseFile(frameworkPath.c_str());
    if (root) {
      CacheManager::instance().saveCachedAST(frameworkPath, root);
    }
  }
  if (!root) {
    return inh;
  }
  for (const auto &node : root->getChildren()) {
    if (!node || node->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(node);
    if (decl->getEntityType() != "_frameworkDescription") {
      continue;
    }
    std::string importName = ASTQuery::stringProperty(decl, "inherits");
    std::string importVersion =
        ASTQuery::stringProperty(decl, "inheritsVersion");
    if (!importName.empty() && !importVersion.empty()) {
      inh.emplace_back(importName, importVersion);
    }
  }
  return inh;
}

std::vector<ASTNode>
StrideFramework::loadImport(const std::string &importName,
                            const std::string &importAs) {
  for (const auto &existing : m_trees) {
    if (!importName.empty() && existing.importName == importName &&
        existing.importAs == importAs) {
      return existing.nodes;
    }
  }

  auto subImportCb = [this](const std::string &subImport,
                            const std::string &subAlias) {
    loadImport(subImport, subAlias);
  };

  auto newNodes = StrideLibrary::loadNodesFromPaths(
      importName, {buildPlatformLibPath()}, m_loading, subImportCb);
  for (const auto &inhPath : m_inheritedPaths) {
    auto inheritedNodes = StrideLibrary::loadNodesFromPaths(
        importName, {inhPath}, m_loading, subImportCb);
    mergeChildOverInherited(newNodes, inheritedNodes);
  }

  tagFrameworkNodes(newNodes);

  for (auto &importTree : m_trees) {
    if (importTree.importAs == importAs) {
      for (const auto &n : newNodes) {
        if (std::find(importTree.nodes.begin(), importTree.nodes.end(), n) ==
            importTree.nodes.end()) {
          importTree.nodes.push_back(n);
        }
      }
      return newNodes;
    }
  }

  std::vector<std::string> namespaces;
  if (!importAs.empty()) {
    namespaces.push_back(importAs);
  }
  m_trees.push_back(
      FrameworkTree{importName, importAs, newNodes, std::move(namespaces)});
  return newNodes;
}

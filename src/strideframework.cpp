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

#include <array>
#include <cassert>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "stride/stridegen/strideframework.hpp"
#include "stride/parser/declarationnode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"
#include "stride/utils/cachemanager.h"


using namespace std;
using namespace strd;

StrideFramework::StrideFramework(std::string strideRoot, std::string framework,
                                 std::string fwVersion, std::string hardware,
                                 std::string hardwareVersion,
                                 std::string rootNamespace)
    : m_strideRoot(strideRoot), m_framework(framework),
      m_frameworkVersion(fwVersion), m_hardware(hardware),
      m_hardwareVersion(hardwareVersion), m_rootNamespace(rootNamespace) {
  m_inheritedPaths = getInheritedFrameworkPaths(buildPlatformLibPath());
  m_inheritedList = loadInheritedList(buildPlatformLibPath());
  auto nodes = loadFrameworkRoot(buildPlatformLibPath());
  m_trees.push_back(FrameworkTree{"", "", nodes, {}});
}

StrideFramework::~StrideFramework() {}

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

std::string StrideFramework::buildPlatformPath() {
  std::string path = m_strideRoot + "/";
  path += "frameworks/" + m_framework + "/" + m_frameworkVersion;
  return path;
}

std::string StrideFramework::buildPlatformLibPath() {
  std::string path = buildPlatformPath() + "/";
  path += "platformlib";
  return path;
}

std::string StrideFramework::buildTestingLibPath() {
  std::string path = buildPlatformLibPath();
  path += "/testing";
  return path;
}

std::string StrideFramework::getPlatformDetails() {
  auto frameworkRoot = buildPlatformLibPath();
  return frameworkRoot;
}

void StrideFramework::installFramework() {
  std::vector<std::string> frameworkRoots;
  frameworkRoots.push_back(buildPlatformLibPath());

  for (const auto &inh : m_inheritedList) {
    std::string path = m_strideRoot + "/";
    path += "frameworks/" + inh.first + "/" + inh.second + "/";
    path += "platformlib";
    frameworkRoots.push_back(path);
  }

  for (const auto frameworkRoot : frameworkRoots) {
    auto fileName = frameworkRoot + "/Configuration.stride";
    auto configuration = AST::parseFile(fileName.c_str());
    if (configuration) {
      for (const auto &node : configuration->getChildren()) {
        if (node->getNodeType() == AST::Declaration) {
          auto decl = std::static_pointer_cast<DeclarationNode>(node);
          auto installationNode = decl->getPropertyValue("installation");
          if (installationNode) {
            for (const auto &installDirectiveNode :
                 installationNode->getChildren()) {
              if (installDirectiveNode->getNodeType() == AST::Declaration) {
                auto installDirective =
                    std::static_pointer_cast<DeclarationNode>(
                        installDirectiveNode);
                if (installDirective->getEntityType() == "installAction") {
                  // FIXME check for platform and language
                  auto commandNode =
                      installDirective->getPropertyValue("command");
                  auto dirNode =
                      installDirective->getPropertyValue("workingDirectory");
                  if (commandNode &&
                      commandNode->getNodeType() == AST::String && dirNode) {
                    std::string workingDirectory = buildPlatformPath();
                    if (dirNode->getNodeType() == AST::String) {
                      workingDirectory +=
                          "/" + std::static_pointer_cast<ValueNode>(dirNode)
                                    ->getStringValue();
                    }
                    auto command =
                        std::static_pointer_cast<ValueNode>(commandNode)
                            ->getStringValue();
                    std::cerr << "Running command: " << command << " IN "
                              << workingDirectory << std::endl;
                    //                  QObject::connect(
                    //                      &installProcess,
                    //                      &QProcess::readyReadStandardOutput,
                    //                      [&]() {
                    //                        qDebug() <<
                    //                        installProcess.readAllStandardOutput();
                    //                      });
                    //                  QObject::connect(
                    //                      &installProcess,
                    //                      &QProcess::readyReadStandardError,
                    //                      [&]() {
                    //                        qDebug() <<
                    //                        installProcess.readAllStandardError();
                    //                      });
                    //                  installProcess.start(QString::fromStdString(command),
                    //                                       QStringList());
                    auto previousPath = std::filesystem::current_path();
                    std::filesystem::current_path(workingDirectory);
#ifdef Q_OS_LINUX
                    FILE *pipe = popen(command.c_str(), "r");
                    if (pipe) {
                      char buffer[128];
                      std::string result = "";
                      // read till end of process:
                      while (!feof(pipe)) {
                        // use buffer to read and add to result
                        if (fgets(buffer, 128, pipe) != NULL)
                          result += buffer;
                      }
                      auto ret = pclose(pipe);
                      if (WIFEXITED(ret)) {
                        if (WEXITSTATUS(ret) != 0) {
                          std::cerr << "ERROR executing : " << command
                                    << std::endl;
                          std::cerr << result << std::endl;
                        }
                      }
                    } else {
                      std::cerr << "popen failed!";
                    }

#elif defined(Q_OS_WINDOWS)
                    std::array<char, 128> buffer;
                    std::string result;
                    std::unique_ptr<FILE, decltype(&_pclose)> pipe(
                        _popen(command.c_str(), "r"), _pclose);
                    if (!pipe) {
                      throw std::runtime_error("popen() failed!");
                    }
                    while (fgets(buffer.data(), buffer.size(), pipe.get()) !=
                           nullptr) {
                      result += buffer.data();
                    }
#else
                    std::array<char, 128> buffer;
                    std::string result;
                    std::unique_ptr<FILE, decltype(&_pclose)> pipe(
                        _popen(command.c_str(), "r"), _pclose);
                    if (!pipe) {
                      throw std::runtime_error("popen() failed!");
                    }
                    while (fgets(buffer.data(), buffer.size(), pipe.get()) !=
                           nullptr) {
                      result += buffer.data();
                    }
                    std::cerr << __FUNCTION__ << " Not implemented!"
                              << std::endl;
#endif
                    std::filesystem::current_path(previousPath);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

std::vector<string> StrideFramework::getDomainIds() {
  std::vector<string> domainIds;
  for (const auto &treeEntry : m_trees) {
    for (const auto &node : treeEntry.nodes) {
      if (node->getNodeType() == AST::Declaration) {
        auto decl = static_pointer_cast<DeclarationNode>(node);
        if (decl->getEntityType() == "_domainDefinition") {
          domainIds.push_back(decl->getName());
        }
      }
    }
  }
  return domainIds;
}

void StrideFramework::addTestingTree(string treeName, ASTNode treeRoot) {
  m_platformTestTrees[treeName] = treeRoot;
}

std::map<string, std::vector<ASTNode>> StrideFramework::getFrameworkMembers() {
  std::map<std::string, std::vector<ASTNode>> libNamespace;
  for (const auto &tree : m_trees) {
    if (libNamespace.find(tree.importAs) == libNamespace.end()) {
      libNamespace[tree.importAs] = std::vector<ASTNode>();
    }
    for (const ASTNode &node : tree.nodes) {
      libNamespace[tree.importAs].push_back(node);
    }
  }
  return libNamespace;
}

vector<ASTNode> StrideFramework::getPlatformObjectsReference() {
  vector<ASTNode> objects;
  auto blockGroup = m_trees.begin();
  while (blockGroup != m_trees.end()) {
    for (const ASTNode &element : blockGroup->nodes) {
      objects.push_back(element);
    }
    blockGroup++;
  }
  return objects;
}

vector<ASTNode> StrideFramework::getPlatformTestingObjectsRef() {
  vector<ASTNode> objects;
  auto blockGroup = m_platformTestTrees.begin();
  while (blockGroup != m_platformTestTrees.end()) {
    for (const ASTNode &element : blockGroup->second->getChildren()) {
      objects.push_back(element);
    }
    blockGroup++;
  }
  return objects;
}

bool StrideFramework::getPluginDetails(string &pluginName, int &majorVersion,
                                       int &minorVersion) {
  for (const auto &tree : m_trees) {
    for (const auto &declNode : tree.nodes) {
      if (declNode->getNodeType() == AST::Declaration) {
        auto decl = static_pointer_cast<DeclarationNode>(declNode);
        if (decl->getEntityType() == "generatorDirectives") {
          auto nameNode = decl->getPropertyValue("codeGenerator");
          if (nameNode && nameNode->getNodeType() == AST::String) {
            pluginName =
                static_pointer_cast<ValueNode>(nameNode)->getStringValue();
          }
          auto majorVersionNode =
              decl->getPropertyValue("codeGeneratorMajorVersion");
          if (majorVersionNode && majorVersionNode->getNodeType() == AST::Int) {
            majorVersion =
                static_pointer_cast<ValueNode>(majorVersionNode)->getIntValue();
          }
          auto minorVersionNode =
              decl->getPropertyValue("codeGeneratorMinorVersion");
          if (minorVersionNode && minorVersionNode->getNodeType() == AST::Int) {
            minorVersion =
                static_pointer_cast<ValueNode>(minorVersionNode)->getIntValue();
          }
          return true;
        }
      }
    }
  }
  return false;
}

std::vector<std::pair<std::string, std::string>>
StrideFramework::getInheritedList() const {
  return m_inheritedList;
}

std::vector<ASTNode>
StrideFramework::loadFrameworkRoot(std::string frameworkRoot) {
  // determine inherited framework paths

  auto nodes = ASTFunctions::loadAllInDirectory(frameworkRoot);

  std::vector<ASTNode> inheritedNodes;
  for (const auto &inhPath : m_inheritedPaths) {
    inheritedNodes = ASTFunctions::loadAllInDirectory(inhPath);
    // Remove nodes from inherited if present in current.
    for (const auto &inhNode : inheritedNodes) {
      bool found = false;
      for (auto node = nodes.begin(); node != nodes.end(); node++) {
        if (ASTQuery::getNodeName(*node) == ASTQuery::getNodeName(inhNode)) {
          //          nodes.erase(node);
          found = true;
          break;
        }
      }
      // TODO this might cause issues if there are multiply defined names within
      // the inherited framework. This would be an error within the framework
      // itself that needs to be reported.
      if (!found) {
        nodes.push_back(inhNode);
      }
    }
  }
  for (auto newNode : nodes) {
    if (getRootNamespace().size() > 0) {
      assert(!newNode->getCompilerProperty("namespaceTree"));
      newNode->appendToPropertyValue(
          "namespaceTree",
          std::make_shared<ValueNode>(getRootNamespace(), __FILE__, __LINE__));
    }
    newNode->setCompilerProperty(
        "framework",
        std::make_shared<ValueNode>(getRootNamespace(), __FILE__, __LINE__));
  }
  return nodes;
}

std::vector<std::string>
StrideFramework::getInheritedFrameworkPaths(std::string frameworkLibPath) {
  std::vector<std::string> inhPaths;
  auto nodes = ASTFunctions::loadAllInDirectory(frameworkLibPath);
  for (const auto &newNode : nodes) {
    if (newNode->getNodeType() == AST::Declaration) {
      auto decl = static_pointer_cast<DeclarationNode>(newNode);
      if (decl->getEntityType() == "_frameworkDescription") {
        auto inheritsNode = decl->getPropertyValue("inherits");
        auto inheritsVersionNode = decl->getPropertyValue("inheritsVersion");
        if (inheritsNode && inheritsVersionNode &&
            inheritsNode->getNodeType() == AST::String &&
            inheritsVersionNode->getNodeType() == AST::String) {
          auto importName =
              static_pointer_cast<ValueNode>(inheritsNode)->getStringValue();
          auto importVersion =
              static_pointer_cast<ValueNode>(inheritsVersionNode)
                  ->getStringValue();
          auto fwRoot = m_strideRoot + "/frameworks/" + importName + "/" +
                        importVersion + "/platformlib";
          inhPaths = getInheritedFrameworkPaths(fwRoot);
          inhPaths.push_back(fwRoot);
        }
      }
    }
  }
  return inhPaths;
}

std::vector<std::pair<std::string, std::string>>
StrideFramework::loadInheritedList(string frameworkLibPath) {
  std::vector<std::pair<std::string, std::string>> inh;

  std::string frameworkPath = frameworkLibPath + "/Framework.stride";
  ASTNode root = CacheManager::instance().loadCachedAST(frameworkPath);
  if (!root && std::filesystem::exists(frameworkPath)) {
    root = AST::parseFile(frameworkPath.c_str());
    if (root) {
      CacheManager::instance().saveCachedAST(frameworkPath, root);
    }
  }
  if (root) {
    for (const auto &node : root->getChildren()) {
      if (node->getNodeType() == AST::Declaration) {
        std::shared_ptr<DeclarationNode> nodeDeclaration =
            std::static_pointer_cast<DeclarationNode>(node);
        if (nodeDeclaration->getEntityType() == "_frameworkDescription") {
          auto inheritsNode = nodeDeclaration->getPropertyValue("inherits");
          auto inheritsVersionNode =
              nodeDeclaration->getPropertyValue("inheritsVersion");
          if (inheritsNode && inheritsVersionNode) {
            if (inheritsNode->getNodeType() == AST::String &&
                inheritsVersionNode->getNodeType() == AST::String) {
              // FIXME look for inheritance recursively
              inh.push_back(
                  {std::static_pointer_cast<ValueNode>(inheritsNode)
                       ->getStringValue(),
                   std::static_pointer_cast<ValueNode>(inheritsVersionNode)
                       ->getStringValue()});
            }
          }
        }
      }
    }
  }
  return inh;
}

std::vector<ASTNode> StrideFramework::loadImport(string importName,
                                                 string importAs) {
  std::vector<ASTNode> newNodes;

  std::vector<std::string> segments;
  size_t start = 0;
  while (start < importName.size()) {
    size_t pos = importName.find("::", start);
    if (pos == std::string::npos) {
      segments.push_back(importName.substr(start));
      break;
    }
    segments.push_back(importName.substr(start, pos - start));
    start = pos + 2;
  }

  std::string relPath;
  for (size_t i = 0; i < segments.size(); ++i) {
    if (i > 0) {
      relPath += "/";
    }
    relPath += segments[i];
  }

  auto loadFromLibDir = [&](const std::string &baseDir) {
    std::vector<ASTNode> loaded;
    if (relPath.empty()) {
      return ASTFunctions::loadAllInDirectory(baseDir);
    }
    std::string filePath = baseDir + "/" + relPath + ".stride";
    if (std::filesystem::exists(filePath) &&
        std::filesystem::is_regular_file(filePath)) {
      auto fileTree = CacheManager::instance().loadCachedAST(filePath);
      if (!fileTree) {
        fileTree = AST::parseFile(filePath.c_str(), nullptr);
        if (fileTree) {
          CacheManager::instance().saveCachedAST(filePath, fileTree);
        }
      }
      if (fileTree) {
        for (const auto &child : fileTree->getChildren()) {
          if (child->getNodeType() == AST::Declaration ||
              child->getNodeType() == AST::ArrayDeclaration) {
            loaded.push_back(child);
          }
        }
      }
    }
    std::string dirPath = baseDir + "/" + relPath;
    if (std::filesystem::exists(dirPath) &&
        std::filesystem::is_directory(dirPath)) {
      auto dirNodes = ASTFunctions::loadAllInDirectory(dirPath);
      for (const auto &dNode : dirNodes) {
        loaded.push_back(dNode);
      }
    }
    return loaded;
  };

  auto nodes = loadFromLibDir(buildPlatformLibPath());
  for (const auto &node : nodes) {
    newNodes.push_back(node);
  }

  for (const auto &inhPath : m_inheritedPaths) {
    auto inheritedNodes = loadFromLibDir(inhPath);
    for (auto inhNode : inheritedNodes) {
      for (auto node = nodes.begin(); node != nodes.end(); node++) {
        if (ASTQuery::getNodeName(*node) == ASTQuery::getNodeName(inhNode)) {
          nodes.erase(node);
          std::cerr << "Replacing inherited node: "
                    << ASTQuery::getNodeName(*node) << " from "
                    << (*node)->getFilename() << " with "
                    << ASTQuery::getNodeName(inhNode) << " from "
                    << inhNode->getFilename() << std::endl;
          break;
        }
      }
      newNodes.push_back(inhNode);
    }
  }

  for (auto newNode : newNodes) {
    if (getRootNamespace().size() > 0) {
      if (!newNode->getCompilerProperty("namespaceTree")) {
        newNode->appendToPropertyValue(
            "namespaceTree",
            std::make_shared<ValueNode>(getRootNamespace(), __FILE__, __LINE__));
      }
    }
    if (!newNode->getCompilerProperty("framework")) {
      newNode->setCompilerProperty(
          "framework",
          std::make_shared<ValueNode>(getRootNamespace(), __FILE__, __LINE__));
    }
  }

  for (auto &importTree : m_trees) {
    if (importTree.importAs == importAs) {
      importTree.nodes.insert(importTree.nodes.end(), newNodes.begin(),
                              newNodes.end());
      return newNodes;
    }
  }

  std::vector<std::string> namespaces;
  if (!importAs.empty()) {
    namespaces.push_back(importAs);
  }
  m_trees.push_back(FrameworkTree{importName, importAs, newNodes, namespaces});
  return newNodes;
}

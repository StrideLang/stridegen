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

#include "stride/stridegen/toolmanager.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "stride/parser/declarationnode.h"
#include "stride/parser/entitynode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

using namespace strd;

namespace {

const char *currentPlatformName() {
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

bool matchesCurrentPlatform(const std::shared_ptr<DeclarationNode> &decl) {
  auto platformsNode = decl ? decl->getPropertyValue("platforms") : nullptr;
  if (!platformsNode || platformsNode->getNodeType() != AST::List) {
    return false;
  }
  const std::string host = currentPlatformName();
  if (host.empty()) {
    return false;
  }
  for (const auto &osNode : platformsNode->getChildren()) {
    if (osNode && osNode->getNodeType() == AST::String &&
        std::static_pointer_cast<ValueNode>(osNode)->getStringValue() == host) {
      return true;
    }
  }
  return false;
}

std::string expandUserHome(std::string path) {
  auto pos = path.find('~');
  if (pos == std::string::npos) {
    return path;
  }
#if defined(_WIN32)
  const char *home = std::getenv("USERPROFILE");
#else
  const char *home = std::getenv("HOME");
#endif
  if (!home) {
    return path;
  }
  path.replace(pos, 1, home);
  return path;
}

template <typename TemplateT>
std::shared_ptr<TemplateT>
parseTemplateDecl(const std::shared_ptr<DeclarationNode> &decl) {
  auto tpl = std::make_shared<TemplateT>();
  tpl->strideName = decl->getName();
  tpl->displayName = ASTQuery::stringProperty(decl, "displayName");
  tpl->output = ASTQuery::stringProperty(decl, "output");
  tpl->meta = ASTQuery::stringProperty(decl, "meta");
  return tpl;
}

std::string findExecutable(const ToolSearch &search) {
  if (search.nameRegex.empty()) {
    return {};
  }
  std::error_code ec;
  std::filesystem::path fi(search.nameRegex);
  if (fi.is_absolute() && std::filesystem::exists(fi, ec)) {
    return search.nameRegex;
  }
  if (!search.rootPathRegex.empty()) {
    std::filesystem::path root = expandUserHome(search.rootPathRegex);
    std::filesystem::path candidate = root / search.nameRegex;
    if (std::filesystem::exists(candidate, ec)) {
      return candidate.string();
    }
  }
  const char *pathEnv = std::getenv("PATH");
  if (!pathEnv) {
    return {};
  }
#if defined(_WIN32)
  const char delim = ';';
#else
  const char delim = ':';
#endif
  std::stringstream ss(pathEnv);
  std::string token;
  while (std::getline(ss, token, delim)) {
    if (token.empty()) {
      continue;
    }
    std::filesystem::path candidate =
        std::filesystem::path(token) / search.nameRegex;
    if (std::filesystem::exists(candidate, ec)) {
      return candidate.string();
    }
  }
  return {};
}

} // namespace

ToolManager::ToolManager(std::string strideRoot)
    : m_strideRoot(std::move(strideRoot)) {
  readTemplates();
  readLocalConfigs();
}

std::string ToolManager::findTool(const std::string &key) const {
  auto it = localTools.find(key);
  return it != localTools.end() ? it->second : std::string();
}

void ToolManager::readTemplates() {
  toolTemplates.clear();
  toolSearches.clear();
  pathTemplates.clear();
  pathSearches.clear();

  if (m_strideRoot.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::path rootDir = std::filesystem::path(m_strideRoot) / "tools";
  if (!std::filesystem::exists(rootDir, ec) ||
      !std::filesystem::is_directory(rootDir, ec)) {
    return;
  }

  for (const auto &file : std::filesystem::directory_iterator(rootDir, ec)) {
    if (ec || !file.is_regular_file(ec) ||
        file.path().extension() != ".stride") {
      continue;
    }
    auto tree = AST::parseFile(file.path().generic_string().c_str());
    if (!tree) {
      LOG_ERROR() << "Error parsing tool file: " << file.path() << std::endl;
      continue;
    }
    for (const auto &child : tree->getChildren()) {
      if (!child || child->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      const auto &et = decl->getEntityType();
      if (et == "toolTemplate") {
        if (matchesCurrentPlatform(decl)) {
          toolTemplates.push_back(parseTemplateDecl<ToolTemplate>(decl));
        }
      } else if (et == "pathTemplate") {
        if (matchesCurrentPlatform(decl)) {
          pathTemplates.push_back(parseTemplateDecl<PathTemplate>(decl));
        }
      } else if (et == "toolSearch") {
        if (matchesCurrentPlatform(decl)) {
          std::string tplName = ASTQuery::stringProperty(decl, "toolTemplate");
          if (tplName.empty()) {
            LOG_ERROR() << "Missing toolTemplate in toolSearch" << std::endl;
            continue;
          }
          auto search = std::make_shared<ToolSearch>();
          search->rootPathRegex =
              ASTQuery::stringProperty(decl, "rootPathRegex");
          search->nameRegex = ASTQuery::stringProperty(decl, "nameRegex");
          toolSearches[tplName] = std::move(search);
        }
      } else if (et == "pathSearch") {
        if (matchesCurrentPlatform(decl)) {
          std::string tplName = ASTQuery::stringProperty(decl, "pathTemplate");
          if (tplName.empty()) {
            LOG_ERROR() << "Missing pathTemplate in pathSearch" << std::endl;
            continue;
          }
          if (pathSearches.find(tplName) != pathSearches.end()) {
            LOG_WARNING() << "Path searches already contain template: "
                          << tplName << ". Overwriting." << std::endl;
          }
          auto search = std::make_shared<PathSearch>();
          search->rootPathRegex =
              ASTQuery::stringProperty(decl, "rootPathRegex");
          pathSearches[tplName] = std::move(search);
        }
      }
    }
  }
}

void ToolManager::readLocalConfigs() {
  localTools.clear();
  localPaths.clear();

  if (m_strideRoot.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::path rootDir =
      std::filesystem::path(m_strideRoot) / "local" / "tools";
  if (!std::filesystem::exists(rootDir, ec) ||
      !std::filesystem::is_directory(rootDir, ec)) {
    return;
  }

  for (const auto &file : std::filesystem::directory_iterator(rootDir, ec)) {
    if (ec || !file.is_regular_file(ec) ||
        file.path().extension() != ".stride") {
      continue;
    }
    auto tree = AST::parseFile(file.path().generic_string().c_str());
    if (!tree) {
      LOG_ERROR() << "Error parsing tool instance: " << file.path()
                  << std::endl;
      continue;
    }
    for (const auto &child : tree->getChildren()) {
      if (!child || child->getNodeType() != AST::Declaration) {
        continue;
      }
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getEntityType() == "toolInstance") {
        auto executableNode = decl->getPropertyValue("executable");
        if (executableNode && executableNode->getNodeType() == AST::String) {
          localTools[decl->getName()] =
              std::static_pointer_cast<ValueNode>(executableNode)
                  ->getStringValue();
        } else {
          LOG_ERROR() << "Invalid executable in tool instance " << file.path()
                      << std::endl;
        }
      } else if (decl->getEntityType() == "pathInstance") {
        auto pathNode = decl->getPropertyValue("path");
        if (pathNode && pathNode->getNodeType() == AST::String) {
          localPaths[decl->getName()] = expandUserHome(
              std::static_pointer_cast<ValueNode>(pathNode)->getStringValue());
        } else {
          LOG_ERROR() << "Invalid path in path instance " << file.path()
                      << std::endl;
        }
      }
    }
  }
}

void ToolManager::updateAllLocalConfigs() {
  if (m_strideRoot.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::path localToolsDir =
      std::filesystem::path(m_strideRoot) / "local" / "tools";
  if (std::filesystem::exists(localToolsDir, ec)) {
    std::filesystem::remove_all(localToolsDir, ec);
  }
  std::filesystem::create_directories(localToolsDir, ec);

  for (const auto &toolTemplate : toolTemplates) {
    auto it = toolSearches.find(toolTemplate->strideName);
    if (it == toolSearches.end() || !it->second) {
      LOG_WARNING() << "No search directive for tool: "
                    << toolTemplate->strideName << std::endl;
      continue;
    }
    std::string foundExecutable = findExecutable(*it->second);
    if (foundExecutable.empty()) {
      continue;
    }
    std::filesystem::path outPath =
        localToolsDir / (toolTemplate->output + ".stride");
    std::ofstream f(outPath);
    if (!f.good()) {
      LOG_ERROR() << "Error creating tool config file: "
                  << toolTemplate->strideName << std::endl;
      continue;
    }
    f << "toolInstance " << toolTemplate->output << " {\n"
      << "  toolTemplate: " << toolTemplate->strideName << "\n"
      << "  executable: '" << foundExecutable << "'\n"
      << "}\n";
  }

  for (const auto &pathTemplate : pathTemplates) {
    auto it = pathSearches.find(pathTemplate->strideName);
    if (it == pathSearches.end() || !it->second) {
      continue;
    }
    std::string foundPath = expandUserHome(it->second->rootPathRegex);
    if (foundPath.empty()) {
      continue;
    }
    std::filesystem::path outPath =
        localToolsDir / (pathTemplate->output + ".stride");
    std::ofstream f(outPath);
    if (!f.good()) {
      LOG_ERROR() << "Error creating path instance file: "
                  << pathTemplate->strideName << std::endl;
      continue;
    }
    f << "pathInstance " << pathTemplate->output << " {\n"
      << "  pathTemplate: " << pathTemplate->strideName << "\n"
      << "  path: '" << foundPath << "'\n"
      << "}\n";
  }

  readLocalConfigs();
}

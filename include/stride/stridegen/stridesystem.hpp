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

#ifndef STRIDESYSTEM_HPP
#define STRIDESYSTEM_HPP

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "stride/parser/strideparser.h"

#include "stride/stridegen/strideframework.hpp"
#include "stride/stridegen/systemconfiguration.hpp"
#include "stride/utils/featureresolver.h"
#include "stride/utils/stridelibrary.h"

namespace strd {
typedef std::vector<std::pair<ASTNode, std::vector<ASTNode>>> ScopeStack;

class StrideSystem {
public:
  StrideSystem(std::string strideRoot, std::string systemName, int majorVersion,
               int minorVersion,
               std::vector<std::shared_ptr<ImportNode>> importList,
               FeatureConfig featureConfig = FeatureConfig(),
               std::vector<std::string> includePaths = {});
  ~StrideSystem();

  static std::vector<std::string>
  listAvailableSystems(const std::string &strideroot);

  const std::vector<std::string> &getErrors() const { return m_errors; }
  const std::vector<std::string> &getWarnings() const { return m_warnings; }
  std::vector<std::string> getPlatformTypeNames() const;
  std::vector<std::string> getFunctionNames() const;

  ASTNode getPlatformDomain(const std::string &namespaceName = "") const;

  std::map<std::string, std::string>
  getFrameworkTools(const std::string &namespaceName) const;
  std::map<std::string, std::string>
  getFrameworkPaths(const std::string &namespaceName) const;

  std::string substituteTokens(const std::string &namespaceName,
                               std::string text) const;
  std::vector<std::string> getFrameworkNames() const;

  static std::vector<ASTNode> getOptionTrees(const std::string &systemPath);

  // if frameworkName != "" then import is limited to the specified framework
  std::vector<ASTNode> loadImportTree(const std::string &importName,
                                      const std::string &importAs,
                                      const std::string &frameworkName = "");

  const std::map<std::string, std::vector<ASTNode>> &getImportTrees() const {
    return m_systemNodes;
  }

  std::string getFrameworkAlias(const std::string &frameworkName) const;
  std::string getFrameworkFromAlias(const std::string &frameworkAlias) const;

  void injectResourceConfiguration(const ASTNode &tree);

  const std::vector<std::shared_ptr<DeclarationNode>> &
  getConnectionDefinitions() const {
    return m_connectionDefinitions;
  }

  std::shared_ptr<DeclarationNode>
  getFrameworkOperator(const std::string &platform, const std::string &leftType,
                       const std::string &rightType, const std::string &type);

  void installFramework(const std::string &frameworkName);

  const std::vector<std::shared_ptr<StrideFramework>> &getFrameworks() const {
    return m_frameworks;
  }

  std::vector<std::shared_ptr<DeclarationNode>>
  getFrameworkSynchronization(const std::string &frameworkName) const;

  std::shared_ptr<DeclarationNode>
  getFrameworkDataType(const std::string &frameworkName,
                       const std::string &strideDataType) const;

  std::string
  getFrameworkDefaultDataType(const std::string &frameworkName) const;

  std::vector<std::shared_ptr<DeclarationNode>>
  getFrameworkOperators(const std::string &frameworkName) const;

  std::vector<std::pair<std::string, std::string>>
  getFrameworkAliasInherits(const std::string &frameworkAlias) const;

  std::string getDataType(const ASTNode &node, const ScopeStack &scope,
                          const ASTNode &tree) const;

  const std::vector<std::shared_ptr<DeclarationNode>> &
  getPlatformDefinitions() const {
    return m_platformDefinitions;
  }

  const SystemConfiguration &getSystemConfiguration() const {
    return m_systemConfig;
  }
  SystemConfiguration &getSystemConfiguration() { return m_systemConfig; }
  void setSystemConfiguration(const SystemConfiguration &config) {
    m_systemConfig = config;
  }

  const std::string &getStrideRoot() const { return m_strideRoot; }
  void setStrideRoot(const std::string &strideRoot) {
    m_strideRoot = strideRoot;
  }

  std::vector<std::string> listAvailableImports() const;

  const std::string &systemName() const { return m_systemName; }

  const FeatureConfig &getFeatureConfig() const { return m_featureConfig; }
  void setFeatureConfig(const FeatureConfig &config) {
    m_featureConfig = config;
  }

private:
  std::shared_ptr<DeclarationNode>
  findDataTypeInFramework(const std::shared_ptr<StrideFramework> &fw,
                          const std::string &strideDataType) const;
  std::shared_ptr<DeclarationNode>
  findDataTypeInPath(const std::string &path, const std::string &strideDataType,
                     std::set<std::string> &visited) const;
  std::string findDefaultDataTypeInPath(const std::string &path,
                                        std::set<std::string> &visited) const;

  void initializeFromSystemTree(const ASTNode &systemTree);

  std::string m_strideRoot;
  std::string m_systemName;
  int m_majorVersion;
  int m_minorVersion;
  std::string m_systemPath;

  std::vector<std::string> m_errors;
  std::vector<std::string> m_warnings;

  std::vector<std::shared_ptr<StrideFramework>> m_frameworks;
  std::vector<std::shared_ptr<DeclarationNode>> m_platformDefinitions;
  std::vector<std::shared_ptr<DeclarationNode>> m_connectionDefinitions;
  SystemConfiguration m_systemConfig;

  std::map<std::string, std::vector<ASTNode>> m_systemNodes;

  StrideLibrary m_library;
  FeatureConfig m_featureConfig;
  std::vector<std::string> m_includePaths;
};
} // namespace strd

#endif // STRIDESYSTEM_HPP

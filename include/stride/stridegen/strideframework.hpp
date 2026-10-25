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

#ifndef STREAMPLATFORM_H
#define STREAMPLATFORM_H

#include <map>
#include <string>
#include <utility>

#include "stride/stridegen/codevalidator.hpp"

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/valuenode.h"

namespace strd {
struct FrameworkTree {
  std::string importName;
  std::string importAs;
  std::vector<ASTNode> nodes;
  std::vector<std::string> namespaces;
};

class StrideFramework {
public:
  StrideFramework(std::string strideRoot, std::string framework,
                  std::string fwVersion, std::string hardware = "",
                  std::string hardwareVersion = "",
                  std::string rootNamespace = "");

  ~StrideFramework();

  typedef enum { PythonTools, PluginPlatform, NullPlatform } PlatformAPI;

  std::string getFramework() const;
  std::string getFrameworkVersion() const;
  std::string getHardware() const;
  std::string getHardwareVersion() const;
  std::string getRootNamespace() const;
  bool getRequired() const;
  PlatformAPI getAPI() const;
  std::string buildPlatformPath();
  std::string buildPlatformLibPath();
  std::string buildTestingLibPath();

  std::string getPlatformDetails();

  void installFramework();

  std::vector<std::string> getDomainIds();

  std::vector<ASTNode> loadImport(std::string importName, std::string importAs);
  //  void addTree(std::string treeName, ASTNode treeRoot);
  void addTestingTree(std::string treeName, ASTNode treeRoot);

  std::map<std::string, std::vector<ASTNode>> getFrameworkMembers();

  std::vector<ASTNode> getPlatformObjectsReference();
  std::vector<ASTNode> getPlatformTestingObjectsRef();

  bool getPluginDetails(std::string &pluginName, int &majorVersion,
                        int &minorVersion);

  std::vector<std::pair<std::string, std::string>> getInheritedList() const;

protected:
  std::vector<ASTNode> loadFrameworkRoot(std::string frameworkRoot);

  std::vector<std::string>
  getInheritedFrameworkPaths(std::string frameworkLibPath);

  std::vector<std::pair<std::string, std::string>>
  loadInheritedList(std::string frameworkLibPath);

private:
  std::string m_strideRoot;
  std::string m_framework;
  std::string m_frameworkVersion;
  std::string m_hardware;
  std::string m_hardwareVersion;
  std::string m_rootNamespace;
  std::string m_inherits;
  std::string m_inheritsVersion;

  std::vector<std::string> m_inheritedPaths;
  std::vector<std::pair<std::string, std::string>> m_inheritedList;

  bool m_required;
  PlatformAPI m_api{
      PluginPlatform}; // TODO Put back support for plugin platforms
                       //  std::map<std::string, ASTNode> m_platformTrees;
  std::map<std::string, ASTNode> m_platformTestTrees;

  std::vector<FrameworkTree> m_trees;
};
} // namespace strd

#endif // STREAMPLATFORM_H

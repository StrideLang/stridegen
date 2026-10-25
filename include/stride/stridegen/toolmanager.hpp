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

#ifndef TOOLMANAGER_HPP
#define TOOLMANAGER_HPP

#include <iostream>
#include <map>
#include <string>

#include "stride/parser/ast.h"

namespace strd {
struct ToolTemplate {
  std::string strideName;
  std::string displayName;
  std::string output;
  std::string meta;
};

struct ToolSearch {
  std::string rootPathRegex;
  std::string nameRegex;
};

struct PathTemplate {
  std::string strideName;
  std::string displayName;
  std::string output;
  std::string meta;
};

struct PathSearch {
  std::string rootPathRegex;
};

class ToolManager {
public:
  ToolManager() {}
  ToolManager(std::string strideRoot);

  void setStrideRoot(std::string strideRoot) { m_strideRoot = strideRoot; }

  void readTemplates();

  void readLocalConfigs();

  void updateAllLocalConfigs();

  std::map<std::string, std::string> localTools;

  std::vector<std::shared_ptr<ToolTemplate>> toolTemplates;
  std::map<std::string, std::shared_ptr<ToolSearch>> toolSearches;

  std::map<std::string, std::string> localPaths;

  std::vector<std::shared_ptr<PathTemplate>> pathTemplates;
  std::map<std::string, std::shared_ptr<PathSearch>> pathSearches;

private:
  std::string m_strideRoot;
};
} // namespace strd

#endif // TOOLMANAGER_HPP

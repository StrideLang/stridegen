/*
    Stride is licensed under the terms of the GNU Affero General Public License
    version 3.

    Copyright (C) 2017-2026 Andres Cabrera
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

    Author: Andres Cabrera
*/

#ifndef FEATURERESOLVER_HPP
#define FEATURERESOLVER_HPP

#include <map>
#include <set>
#include <string>
#include <vector>

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include "stride/parser/langerror.h"

namespace strd {

struct FeatureConfig {
  std::set<std::string> enabledFeatures;
  std::set<std::string> disabledFeatures;
  std::map<std::string, std::string> exclusiveChoices; // group -> variant
  bool useDefaultFeatures{true};
};

class FeatureResolver {
public:
  FeatureResolver(ASTNode tree, const FeatureConfig &config = FeatureConfig());
  ~FeatureResolver();

  bool process();

  static bool isConditionMet(ASTNode cond, const FeatureConfig &config);

  const std::vector<LangError> &getErrors() const { return m_errors; }
  const std::set<std::string> &getActiveFeatures() const {
    return m_activeFeatures;
  }
  const std::map<std::string, std::string> &getActiveChoices() const {
    return m_exclusiveChoices;
  }

private:
  struct FeatureInfo {
    std::string name;
    bool isGroup{false};
    std::vector<std::string> variants;
    std::string defaultVariant;
  };

  void harvestDeclarations();
  void processForceDirectives();
  bool reconcileAndValidate();
  bool evaluateCondition(ASTNode cond, const std::string &currentFile, int line);
  bool isFeatureActive(const std::string &name) const;

  void pruneAST(ASTNode node);
  void pruneConditionalProperties(ASTNode node);
  void prunePortLists(ASTNode node);

  ASTNode m_tree;
  FeatureConfig m_initialConfig;

  std::map<std::string, FeatureInfo> m_declaredFeatures;
  std::set<std::string> m_activeFeatures;
  std::set<std::string> m_disabledFeatures;
  std::map<std::string, std::string> m_exclusiveChoices; // Group -> Variant
  bool m_useDefaultFeatures{true};

  std::vector<LangError> m_errors;
};

} // namespace strd

#endif // FEATURERESOLVER_HPP

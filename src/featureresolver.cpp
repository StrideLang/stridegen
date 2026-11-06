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

#include "stride/stridegen/featureresolver.hpp"

#include <algorithm>
#include <iostream>

#include "stride/parser/entitynode.h"
#include "stride/parser/expressionnode.h"
#include "stride/parser/listnode.h"
#include "stride/parser/propertynode.h"
#include "stride/parser/scopenode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

namespace strd {

static LangError makeError(LangError::ErrorType type,
                           const std::vector<std::string> &tokens,
                           const std::string &filename, int line) {
  LangError err;
  err.type = type;
  err.errorTokens = tokens;
  err.filename = filename;
  err.lineNumber = line;
  return err;
}

FeatureResolver::FeatureResolver(ASTNode tree, const FeatureConfig &config)
    : m_tree(tree), m_initialConfig(config) {}

FeatureResolver::~FeatureResolver() {}

bool FeatureResolver::process() {
  if (!m_tree) {
    return true;
  }
  m_errors.clear();
  harvestDeclarations();
  processForceDirectives();
  if (!reconcileAndValidate()) {
    return false;
  }
  pruneAST(m_tree);
  return m_errors.empty();
}

void FeatureResolver::harvestDeclarations() {
  m_declaredFeatures.clear();
  std::vector<ASTNode> remainingChildren;

  for (const auto &child : m_tree->getChildren()) {
    if (!child)
      continue;
    if (child->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getEntityType() == "feature") {
        FeatureInfo info;
        info.name = decl->getName();

        auto variantsVal = decl->getPropertyValue("variants");
        if (variantsVal) {
          info.isGroup = true;
          if (variantsVal->getNodeType() == AST::List) {
            for (const auto &varChild : variantsVal->getChildren()) {
              if (varChild) {
                std::string vName = ASTQuery::getNodeName(varChild);
                if (vName.empty() && varChild->getNodeType() == AST::String) {
                  vName = std::static_pointer_cast<ValueNode>(varChild)
                              ->getStringValue();
                }
                if (!vName.empty()) {
                  info.variants.push_back(vName);
                }
              }
            }
          }
        }

        auto defaultVal = decl->getPropertyValue("default");
        if (defaultVal) {
          info.defaultVariant = ASTQuery::getNodeName(defaultVal);
          if (info.defaultVariant.empty() &&
              defaultVal->getNodeType() == AST::String) {
            info.defaultVariant =
                std::static_pointer_cast<ValueNode>(defaultVal)
                    ->getStringValue();
          }
        }

        m_declaredFeatures[info.name] = info;
        continue; // Strip feature declaration from AST
      }
    }
    remainingChildren.push_back(child);
  }

  m_tree->setChildren(remainingChildren);
}

void FeatureResolver::processForceDirectives() {
  m_activeFeatures = m_initialConfig.enabledFeatures;
  m_disabledFeatures = m_initialConfig.disabledFeatures;
  m_exclusiveChoices = m_initialConfig.exclusiveChoices;
  m_useDefaultFeatures = m_initialConfig.useDefaultFeatures;

  std::vector<ASTNode> remainingChildren;

  for (const auto &child : m_tree->getChildren()) {
    if (!child)
      continue;
    if (child->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getEntityType() == "_force") {
        auto cond = decl->getCompilerProperty("_feature_condition");
        std::string filename = decl->getFilename();
        int line = decl->getLine();

        LOG_WARNING() << filename << ":" << line
                      << ": warning: [force] feature forced locally in file.\n";

        if (cond) {
          if (cond->getNodeType() == AST::Entity) {
            std::string name = cond->getName();
            auto scope = std::static_pointer_cast<EntityNode>(cond)->getScope();
            if (scope && scope->getChildren().size() > 0) {
              std::string s;
              for (const auto &sc : scope->getChildren()) {
                if (sc->getNodeType() == AST::Scope) {
                  s += std::static_pointer_cast<ScopeNode>(sc)->getName() + "::";
                }
              }
              name = s + name;
            }

            auto pos = name.find("::");
            if (pos != std::string::npos) {
              std::string group = name.substr(0, pos);
              std::string variant = name.substr(pos + 2);
              if (m_initialConfig.exclusiveChoices.count(group) &&
                  m_initialConfig.exclusiveChoices.at(group) != variant) {
                LOG_WARNING()
                    << filename << ":" << line
                    << ": warning: in-source '@force(" << name
                    << ")' ignored; overridden by command-line flag.\n";
              } else {
                m_exclusiveChoices[group] = variant;
                m_activeFeatures.insert(name);
                m_activeFeatures.insert(group);
                m_disabledFeatures.erase(name);
                m_disabledFeatures.erase(group);
              }
            } else {
              if (m_initialConfig.disabledFeatures.count(name)) {
                LOG_WARNING()
                    << filename << ":" << line
                    << ": warning: in-source '@force(" << name
                    << ")' ignored; overridden by command-line flag.\n";
              } else {
                m_activeFeatures.insert(name);
                m_disabledFeatures.erase(name);
              }
            }
          } else if (cond->getNodeType() == AST::Expression) {
            auto expr = std::static_pointer_cast<ExpressionNode>(cond);
            if (expr->getExpressionType() == ExpressionNode::LogicalNot &&
                expr->getLeft() &&
                expr->getLeft()->getNodeType() == AST::Entity) {
              std::string name = expr->getLeft()->getName();
              m_disabledFeatures.insert(name);
              m_activeFeatures.erase(name);
            }
          }
        }
        continue; // Strip _force directive from AST
      }
    }
    remainingChildren.push_back(child);
  }

  m_tree->setChildren(remainingChildren);
}

bool FeatureResolver::reconcileAndValidate() {
  // Apply defaults for exclusive groups if enabled and not explicitly specified or disabled
  if (m_useDefaultFeatures) {
    for (const auto &[name, info] : m_declaredFeatures) {
      if (info.isGroup && !info.defaultVariant.empty()) {
        if (m_exclusiveChoices.find(name) == m_exclusiveChoices.end() &&
            m_disabledFeatures.count(name) == 0) {
          m_exclusiveChoices[name] = info.defaultVariant;
          m_activeFeatures.insert(name + "::" + info.defaultVariant);
          m_activeFeatures.insert(name);
        }
      }
    }
  }

  // Validate all active features against declared features
  for (const auto &feat : m_activeFeatures) {
    auto pos = feat.find("::");
    if (pos != std::string::npos) {
      std::string group = feat.substr(0, pos);
      std::string variant = feat.substr(pos + 2);
      auto it = m_declaredFeatures.find(group);
      if (it == m_declaredFeatures.end()) {
        m_errors.push_back(makeError(LangError::UndeclaredFeature,
                                     {group},
                                     m_tree->getFilename(), m_tree->getLine()));
        return false;
      }
      const auto &variants = it->second.variants;
      if (std::find(variants.begin(), variants.end(), variant) ==
          variants.end()) {
        m_errors.push_back(makeError(LangError::InvalidFeatureVariant,
                                     {group, variant},
                                     m_tree->getFilename(), m_tree->getLine()));
        return false;
      }
    } else {
      if (m_declaredFeatures.find(feat) == m_declaredFeatures.end()) {
        m_errors.push_back(makeError(LangError::UndeclaredFeature,
                                     {feat},
                                     m_tree->getFilename(), m_tree->getLine()));
        return false;
      }
    }
  }

  return true;
}

bool FeatureResolver::isFeatureActive(const std::string &name) const {
  if (m_disabledFeatures.count(name)) {
    return false;
  }
  auto pos = name.find("::");
  if (pos != std::string::npos) {
    std::string group = name.substr(0, pos);
    std::string variant = name.substr(pos + 2);
    auto it = m_exclusiveChoices.find(group);
    if (it != m_exclusiveChoices.end()) {
      return it->second == variant;
    }
    return false;
  } else {
    auto it = m_exclusiveChoices.find(name);
    if (it != m_exclusiveChoices.end()) {
      return true; // Umbrella group check
    }
    return m_activeFeatures.count(name) > 0;
  }
}

bool FeatureResolver::evaluateCondition(ASTNode cond,
                                        const std::string &currentFile,
                                        int line) {
  if (!cond) {
    return true;
  }

  if (cond->getNodeType() == AST::Entity) {
    std::string name = cond->getName();
    auto scope = std::static_pointer_cast<EntityNode>(cond)->getScope();
    if (scope && scope->getChildren().size() > 0) {
      std::string s;
      for (const auto &sc : scope->getChildren()) {
        if (sc->getNodeType() == AST::Scope) {
          s += std::static_pointer_cast<ScopeNode>(sc)->getName() + "::";
        }
      }
      name = s + name;
    }

    auto pos = name.find("::");
    if (pos != std::string::npos) {
      std::string group = name.substr(0, pos);
      std::string variant = name.substr(pos + 2);
      auto it = m_declaredFeatures.find(group);
      if (it == m_declaredFeatures.end()) {
        m_errors.push_back(makeError(LangError::UndeclaredFeature,
                                     {group},
                                     currentFile, line));
        return false;
      }
      const auto &variants = it->second.variants;
      if (std::find(variants.begin(), variants.end(), variant) ==
          variants.end()) {
        m_errors.push_back(makeError(LangError::InvalidFeatureVariant,
                                     {group, variant},
                                     currentFile, line));
        return false;
      }
    } else {
      if (m_declaredFeatures.find(name) == m_declaredFeatures.end()) {
        m_errors.push_back(makeError(LangError::UndeclaredFeature,
                                     {name},
                                     currentFile, line));
        return false;
      }
    }

    return isFeatureActive(name);
  }

  if (cond->getNodeType() == AST::Expression) {
    auto expr = std::static_pointer_cast<ExpressionNode>(cond);
    switch (expr->getExpressionType()) {
    case ExpressionNode::LogicalNot:
      return !evaluateCondition(expr->getLeft(), currentFile, line);
    case ExpressionNode::And:
      return evaluateCondition(expr->getLeft(), currentFile, line) &&
             evaluateCondition(expr->getRight(), currentFile, line);
    case ExpressionNode::Or:
      return evaluateCondition(expr->getLeft(), currentFile, line) ||
             evaluateCondition(expr->getRight(), currentFile, line);
    default:
      return false;
    }
  }

  return false;
}

void FeatureResolver::pruneAST(ASTNode node) {
  if (!node)
    return;

  // 1. Prune child nodes based on _feature_condition
  std::vector<ASTNode> survivingChildren;
  for (const auto &child : node->getChildren()) {
    if (!child)
      continue;
    auto cond = child->getCompilerProperty("_feature_condition");
    if (cond) {
      bool active = evaluateCondition(cond, child->getFilename(), child->getLine());
      if (!active) {
        continue; // Pruned!
      }
      child->removeCompilerProperty("_feature_condition");
    }
    survivingChildren.push_back(child);
  }
  node->setChildren(survivingChildren);

  // 2. Resolve inline conditional properties
  pruneConditionalProperties(node);

  // 3. Prune conditional items in port lists (inputs, outputs)
  prunePortLists(node);

  // 4. Recurse into children
  for (const auto &child : node->getChildren()) {
    pruneAST(child);
  }
}

void FeatureResolver::pruneConditionalProperties(ASTNode node) {
  if (!node)
    return;

  for (const auto &child : node->getChildren()) {
    if (child && child->getNodeType() == AST::Property) {
      auto prop = std::static_pointer_cast<PropertyNode>(child);
      auto val = prop->getValue();
      if (val && val->getCompilerProperty("_is_conditional_property")) {
        auto list = std::static_pointer_cast<ListNode>(val);
        int matchCount = 0;
        ASTNode winningValue = nullptr;

        for (const auto &cand : list->getChildren()) {
          auto cond = cand->getCompilerProperty("_feature_condition");
          if (evaluateCondition(cond, cand->getFilename(), cand->getLine())) {
            matchCount++;
            winningValue = cand;
          }
        }

        if (matchCount == 0) {
          m_errors.push_back(makeError(LangError::NonExhaustiveProperty,
                                       {prop->getName()},
                                       prop->getFilename(), prop->getLine()));
        } else if (matchCount > 1) {
          m_errors.push_back(makeError(LangError::AmbiguousProperty,
                                       {prop->getName()},
                                       prop->getFilename(), prop->getLine()));
        } else {
          winningValue->removeCompilerProperty("_feature_condition");
          prop->replaceValue(winningValue);
        }
      }
    }
  }
}

void FeatureResolver::prunePortLists(ASTNode node) {
  if (!node)
    return;

  for (const auto &child : node->getChildren()) {
    if (child && child->getNodeType() == AST::Property) {
      auto prop = std::static_pointer_cast<PropertyNode>(child);
      if (prop->getName() == "inputs" || prop->getName() == "outputs") {
        auto val = prop->getValue();
        if (val && val->getNodeType() == AST::List) {
          auto list = std::static_pointer_cast<ListNode>(val);
          std::vector<ASTNode> survivingPorts;
          for (const auto &port : list->getChildren()) {
            if (!port)
              continue;
            auto cond = port->getCompilerProperty("_feature_condition");
            if (cond) {
              bool active = evaluateCondition(cond, port->getFilename(), port->getLine());
              if (!active) {
                continue;
              }
              port->removeCompilerProperty("_feature_condition");
            }
            survivingPorts.push_back(port);
          }
          list->setChildren(survivingPorts);
        }
      }
    }
  }
}

} // namespace strd

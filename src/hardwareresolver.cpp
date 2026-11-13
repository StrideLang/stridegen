/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2026 Andres Cabrera
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
*/

#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/parser/propertynode.h"
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"

#include <algorithm>
#include <filesystem>
#include <set>

namespace strd {

const char *hardwareKindName(HardwareKind kind) {
  switch (kind) {
  case HardwareKind::Core:
    return "core";
  case HardwareKind::Device:
    return "device";
  case HardwareKind::Board:
    // Not "board": the game library already has a `board` type.
    return "hardwareBoard";
  }
  return "";
}

namespace {

std::shared_ptr<DeclarationNode> asBlock(const ASTNode &node) {
  if (!node || node->getNodeType() != AST::Declaration) {
    return nullptr;
  }
  auto decl = std::static_pointer_cast<DeclarationNode>(node);
  if (!decl->getName().empty() || !decl->getEntityType().empty()) {
    return nullptr; // a named declaration is a value, not a block
  }
  return decl;
}

ASTNode mergeValues(const ASTNode &base, const ASTNode &over);

// Items of the layer above are added after the base's. An item whose name or
// text is already there is not added twice, except a named entry
// (`pin D13 { ... }`), which replaces the base's entry of the same name.
ASTNode mergeLists(const ASTNode &base, const ASTNode &over) {
  auto merged = base->deepCopy();
  auto items = merged->getChildren();
  for (const auto &item : over->getChildren()) {
    auto text = HardwareResolver::valueText(item);
    auto existing = text.empty()
                        ? items.end()
                        : std::find_if(items.begin(), items.end(),
                                       [&](const ASTNode &candidate) {
                                         return HardwareResolver::valueText(
                                                    candidate) == text;
                                       });
    if (existing == items.end()) {
      items.push_back(item->deepCopy());
    } else if (item->getNodeType() == AST::Declaration) {
      *existing = item->deepCopy();
    }
  }
  merged->setChildren(items);
  return merged;
}

ASTNode mergeBlocks(const std::shared_ptr<DeclarationNode> &base,
                    const std::shared_ptr<DeclarationNode> &over) {
  auto merged = std::static_pointer_cast<DeclarationNode>(base->deepCopy());
  for (const auto &prop : over->getProperties()) {
    auto existing = merged->getPropertyValue(prop->getName());
    if (existing) {
      merged->replacePropertyValue(prop->getName(),
                                   mergeValues(existing, prop->getValue()));
    } else {
      merged->addProperty(
          std::static_pointer_cast<PropertyNode>(prop->deepCopy()));
    }
  }
  return merged;
}

ASTNode mergeValues(const ASTNode &base, const ASTNode &over) {
  if (!base) {
    return over ? over->deepCopy() : nullptr;
  }
  if (!over) {
    return base;
  }
  if (base->getNodeType() == AST::List && over->getNodeType() == AST::List) {
    return mergeLists(base, over);
  }
  auto baseBlock = asBlock(base);
  auto overBlock = asBlock(over);
  if (baseBlock && overBlock) {
    return mergeBlocks(baseBlock, overBlock);
  }
  return over->deepCopy();
}

std::shared_ptr<DeclarationNode> findDeclaration(const std::string &path,
                                                 const std::string &entityType,
                                                 const std::string &name) {
  if (!std::filesystem::exists(path)) {
    return nullptr;
  }
  auto tree = AST::parseFile(path.c_str(), nullptr);
  if (!tree) {
    return nullptr;
  }
  for (const auto &child : tree->getChildren()) {
    if (child->getNodeType() != AST::Declaration) {
      continue;
    }
    auto decl = std::static_pointer_cast<DeclarationNode>(child);
    if (decl->getEntityType() == entityType && decl->getName() == name) {
      return decl;
    }
  }
  return nullptr;
}

} // namespace

// ResolvedHardware -----------------------------------------------------------

const std::string &ResolvedHardware::name() const {
  static const std::string empty;
  return m_chain.empty() ? empty : m_chain.back().name;
}

std::string ResolvedHardware::layerName(HardwareKind kind) const {
  for (auto it = m_chain.rbegin(); it != m_chain.rend(); ++it) {
    if (it->kind == kind) {
      return it->name;
    }
  }
  return {};
}

ASTNode ResolvedHardware::property(const std::string &name) const {
  auto it = m_properties.find(name);
  return it == m_properties.end() ? nullptr : it->second;
}

bool ResolvedHardware::hasProperty(const std::string &name) const {
  auto value = property(name);
  return value && value->getNodeType() != AST::None;
}

std::string
ResolvedHardware::stringProperty(const std::string &name,
                                 const std::string &defaultValue) const {
  auto text = HardwareResolver::valueText(property(name));
  return text.empty() ? defaultValue : text;
}

static std::optional<int64_t> intOf(const ASTNode &node) {
  if (node && node->getNodeType() == AST::Int) {
    return std::static_pointer_cast<ValueNode>(node)->getIntValue();
  }
  return std::nullopt;
}

std::optional<int64_t>
ResolvedHardware::intProperty(const std::string &name) const {
  return intOf(property(name));
}

std::vector<std::string>
ResolvedHardware::nameListProperty(const std::string &name) const {
  std::vector<std::string> names;
  auto value = property(name);
  if (!value || value->getNodeType() == AST::None) {
    return names;
  }
  if (value->getNodeType() == AST::List) {
    for (const auto &item : value->getChildren()) {
      auto text = HardwareResolver::valueText(item);
      if (!text.empty()) {
        names.push_back(text);
      }
    }
  } else {
    auto text = HardwareResolver::valueText(value);
    if (!text.empty()) {
      names.push_back(text);
    }
  }
  return names;
}

ASTNode ResolvedHardware::blockValue(const std::string &block,
                                     const std::string &key) const {
  auto decl = asBlock(property(block));
  return decl ? decl->getPropertyValue(key) : nullptr;
}

std::string
ResolvedHardware::blockString(const std::string &block, const std::string &key,
                              const std::string &defaultValue) const {
  auto text = HardwareResolver::valueText(blockValue(block, key));
  return text.empty() ? defaultValue : text;
}

std::optional<int64_t> ResolvedHardware::blockInt(const std::string &block,
                                                  const std::string &key) const {
  return intOf(blockValue(block, key));
}

std::vector<std::string>
ResolvedHardware::blockKeys(const std::string &block) const {
  std::vector<std::string> keys;
  if (auto decl = asBlock(property(block))) {
    for (const auto &prop : decl->getProperties()) {
      keys.push_back(prop->getName());
    }
  }
  return keys;
}

std::shared_ptr<DeclarationNode>
ResolvedHardware::entry(const std::string &list,
                        const std::string &name) const {
  auto value = property(list);
  if (!value || value->getNodeType() != AST::List) {
    return nullptr;
  }
  for (const auto &item : value->getChildren()) {
    if (item->getNodeType() == AST::Declaration &&
        std::static_pointer_cast<DeclarationNode>(item)->getName() == name) {
      return std::static_pointer_cast<DeclarationNode>(item);
    }
  }
  return nullptr;
}

std::string ResolvedHardware::entryString(const std::string &list,
                                          const std::string &name,
                                          const std::string &key) const {
  auto decl = entry(list, name);
  return decl ? HardwareResolver::valueText(decl->getPropertyValue(key))
              : std::string();
}

std::optional<int64_t> ResolvedHardware::entryInt(const std::string &list,
                                                  const std::string &name,
                                                  const std::string &key) const {
  auto decl = entry(list, name);
  return decl ? intOf(decl->getPropertyValue(key)) : std::nullopt;
}

// HardwareResolver -----------------------------------------------------------

HardwareResolver::HardwareResolver(std::string strideroot)
    : m_strideroot(std::move(strideroot)) {}

std::string HardwareResolver::valueText(const ASTNode &node) {
  if (!node) {
    return {};
  }
  switch (node->getNodeType()) {
  case AST::String:
    return std::static_pointer_cast<ValueNode>(node)->getStringValue();
  case AST::Int:
    return std::to_string(
        std::static_pointer_cast<ValueNode>(node)->getIntValue());
  case AST::Real:
  case AST::Switch:
    return std::static_pointer_cast<ValueNode>(node)->toString();
  case AST::Declaration:
    return std::static_pointer_cast<DeclarationNode>(node)->getName();
  case AST::Entity:
  case AST::Array:
  case AST::Function:
    return ASTQuery::getNodeName(node);
  default:
    return {};
  }
}

std::optional<HardwareLayer>
HardwareResolver::findLayer(const std::string &platformName,
                            const std::string &name) const {
  struct Candidate {
    HardwareKind kind;
    std::string path;
  };
  std::vector<Candidate> candidates;
  if (!platformName.empty()) {
    auto platformDir = m_strideroot + "/platforms/" + platformName;
    candidates.push_back(
        {HardwareKind::Board, platformDir + "/boards/" + name + ".stride"});
    candidates.push_back(
        {HardwareKind::Device, platformDir + "/devices/" + name + ".stride"});
  }
  candidates.push_back(
      {HardwareKind::Core, m_strideroot + "/cores/" + name + ".stride"});

  for (const auto &candidate : candidates) {
    auto decl = findDeclaration(candidate.path,
                                hardwareKindName(candidate.kind), name);
    if (decl) {
      return HardwareLayer{candidate.kind, name, candidate.path, decl};
    }
  }
  return std::nullopt;
}

std::optional<ResolvedHardware>
HardwareResolver::resolve(const std::string &platformName,
                          const std::string &name, std::string *error) const {
  auto fail = [&](const std::string &message) -> std::optional<ResolvedHardware> {
    if (error) {
      *error = message;
    }
    return std::nullopt;
  };

  ResolvedHardware resolved;
  std::set<std::string> visited;
  std::string current = name;
  while (!current.empty()) {
    if (visited.count(current)) {
      return fail("hardware overlay cycle at '" + current + "'");
    }
    visited.insert(current);

    auto layer = findLayer(platformName, current);
    if (!layer) {
      std::string message = "unknown board, device or core '" + current + "'";
      if (!resolved.m_chain.empty()) {
        message += " (overlaid by " + std::string(hardwareKindName(
                                          resolved.m_chain.back().kind)) +
                   " '" + resolved.m_chain.back().name + "')";
      }
      return fail(message);
    }
    if (!resolved.m_chain.empty() &&
        static_cast<int>(layer->kind) >
            static_cast<int>(resolved.m_chain.back().kind)) {
      return fail(std::string(hardwareKindName(resolved.m_chain.back().kind)) +
                  " '" + resolved.m_chain.back().name + "' cannot overlay " +
                  hardwareKindName(layer->kind) + " '" + current + "'");
    }
    resolved.m_chain.push_back(*layer);
    current = valueText(layer->declaration->getPropertyValue("overlays"));
  }

  // Leaf first while walking; store and merge base first.
  std::reverse(resolved.m_chain.begin(), resolved.m_chain.end());
  for (const auto &layer : resolved.m_chain) {
    for (const auto &prop : layer.declaration->getProperties()) {
      if (prop->getName() == "overlays") {
        continue;
      }
      auto &slot = resolved.m_properties[prop->getName()];
      slot = mergeValues(slot, prop->getValue());
    }
  }
  return resolved;
}

} // namespace strd

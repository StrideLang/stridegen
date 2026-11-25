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

#include "stride/stridegen/platformpolicymanager.hpp"
#include "stride/stridegen/hardwareresolver.hpp"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

#include <algorithm>
#include <filesystem>

namespace strd {

namespace {

std::string stripNamespace(const std::string &name) {
  size_t pos = name.rfind("::");
  return pos == std::string::npos ? name : name.substr(pos + 2);
}

std::vector<std::string> collectQualifiedNames(const ASTNode &node) {
  std::vector<std::string> out;
  if (!node) {
    return out;
  }
  if (node->getNodeType() == AST::List) {
    for (const auto &child : node->getChildren()) {
      std::string q = ASTQuery::getQualifiedName(child);
      if (q.empty()) {
        q = ASTQuery::valueText(child);
      }
      out.push_back(std::move(q));
    }
  } else {
    std::string q = ASTQuery::getQualifiedName(node);
    if (q.empty()) {
      q = ASTQuery::valueText(node);
    }
    out.push_back(std::move(q));
  }
  return out;
}

} // namespace

PlatformPolicyManager::PlatformPolicyManager() = default;
PlatformPolicyManager::~PlatformPolicyManager() = default;

void PlatformPolicyManager::clear() {
  m_platformName.clear();
  m_targetDevice.clear();
  m_compilerFlags.clear();
  m_resources.clear();
  m_supportedDevices.clear();
  m_bindings.clear();
}

void PlatformPolicyManager::parsePolicyBindings(
    const std::shared_ptr<DeclarationNode> &decl,
    const std::string &propertyName) {
  if (!decl) {
    return;
  }
  auto prop = decl->getPropertyValue(propertyName);
  if (!prop || prop->getNodeType() != AST::List) {
    return;
  }
  for (const auto &bindingNode : prop->getChildren()) {
    if (!bindingNode || (bindingNode->getNodeType() != AST::Declaration &&
                         bindingNode->getNodeType() != AST::ArrayDeclaration)) {
      continue;
    }
    auto bDecl = std::static_pointer_cast<DeclarationNode>(bindingNode);
    if (bDecl->getEntityType() != "policyBinding") {
      continue;
    }

    auto policyProp = bDecl->getPropertyValue("policy");
    auto funcProp = bDecl->getPropertyValue("function");
    auto targetProp = bDecl->getPropertyValue("target");
    if (!policyProp || !funcProp || !targetProp) {
      continue;
    }

    std::string policyFull = ASTQuery::getQualifiedName(policyProp);
    if (policyFull.empty()) {
      policyFull = ASTQuery::valueText(policyProp);
    }
    if (policyFull.empty()) {
      continue;
    }

    auto funcs = collectQualifiedNames(funcProp);
    auto targets = collectQualifiedNames(targetProp);
    if (funcs.size() != targets.size()) {
      LOG_WARNING() << "policyBinding in " << decl->getName()
                    << " has mismatched function (" << funcs.size()
                    << ") and target (" << targets.size() << ") lists."
                    << std::endl;
    }

    const std::string policyShort = stripNamespace(policyFull);
    const size_t count = std::min(funcs.size(), targets.size());
    for (size_t i = 0; i < count; ++i) {
      if (!funcs[i].empty() && !targets[i].empty()) {
        const std::string fnShort = stripNamespace(funcs[i]);
        m_bindings[{policyShort, fnShort}] = targets[i];
        m_bindings[{policyFull, fnShort}] = targets[i];
        m_bindings[{policyShort, funcs[i]}] = targets[i];
        m_bindings[{policyFull, funcs[i]}] = targets[i];
      }
    }
  }
}

bool PlatformPolicyManager::loadPlatform(const std::string &strideroot,
                                         const std::string &platformName,
                                         const std::string &targetDevice) {
  clear();
  m_platformName = platformName;

  std::string platformPath =
      strideroot + "/platforms/" + platformName + "/Platform.stride";
  if (!std::filesystem::exists(platformPath)) {
    return false;
  }
  auto platformTree = AST::parseFile(platformPath.c_str(), nullptr);
  if (!platformTree) {
    return false;
  }

  std::shared_ptr<DeclarationNode> platformDecl;
  for (const auto &child : platformTree->getChildren()) {
    if (child && child->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getEntityType() == "platform" &&
          decl->getName() == platformName) {
        platformDecl = decl;
        break;
      }
    }
  }
  if (!platformDecl) {
    return false;
  }

  m_supportedDevices = ASTQuery::nameListProperty(platformDecl, "supportedDevices");

  std::string activeDevice = targetDevice;
  if (activeDevice.empty()) {
    activeDevice = ASTQuery::stringProperty(platformDecl, "defaultDevice");
  }
  m_targetDevice = activeDevice;

  parsePolicyBindings(platformDecl, "platformPolicies");

  if (activeDevice.empty()) {
    return true;
  }

  HardwareResolver resolver(strideroot);
  auto resolved = resolver.resolve(platformName, activeDevice);
  if (!resolved) {
    return true;
  }
  m_resources = resolved->nameListProperty("resources");
  m_compilerFlags = resolved->stringProperty("compilerFlags");

  for (const auto &layer : resolved->chain()) {
    parsePolicyBindings(layer.declaration, "devicePolicies");
  }
  return true;
}

std::string PlatformPolicyManager::resolveVariant(
    const std::string &policyName, const std::string &functionName,
    std::string *outVariantTag) const {
  const std::string pShort = stripNamespace(policyName);
  const std::string fnShort = stripNamespace(functionName);

  // Exact qualified match wins first; progressively relax to short names.
  const std::pair<std::string, std::string> candidates[] = {
      {policyName, functionName},
      {pShort, functionName},
      {policyName, fnShort},
      {pShort, fnShort}};

  auto tryLookup = [&](const std::pair<std::string, std::string> &key,
                       std::string &outTarget) -> bool {
    if (auto it = m_bindings.find(key); it != m_bindings.end()) {
      outTarget = it->second;
      return true;
    }
    return false;
  };

  std::string targetVariant;
  bool found = false;
  for (const auto &cand : candidates) {
    if (tryLookup(cand, targetVariant)) {
      found = true;
      break;
    }
  }
  if (!found) {
    // Also match qualified rules stored under a namespace when caller passes a
    // short policy or function name.
    for (const auto &[key, target] : m_bindings) {
      if ((key.first == policyName || stripNamespace(key.first) == pShort) &&
          (key.second == functionName ||
           stripNamespace(key.second) == fnShort)) {
        targetVariant = target;
        found = true;
        break;
      }
    }
  }

  if (found) {
    if (outVariantTag) {
      size_t atPos = targetVariant.find('@');
      *outVariantTag = (atPos != std::string::npos)
                           ? targetVariant.substr(atPos + 1)
                           : std::string();
    }
    return targetVariant;
  }

  if (outVariantTag) {
    outVariantTag->clear();
  }
  return {};
}

bool PlatformPolicyManager::hasAnyPolicyBinding(
    const std::string &functionName) const {
  const std::string fnShort = stripNamespace(functionName);
  for (const auto &[key, target] : m_bindings) {
    (void)target;
    if (key.second == functionName || stripNamespace(key.second) == fnShort) {
      return true;
    }
  }
  return false;
}

} // namespace strd

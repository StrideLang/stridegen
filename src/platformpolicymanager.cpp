/*
    Stride is licensed under the terms of the GNU Affero General Public License version 3.

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
#include "stride/parser/valuenode.h"
#include "stride/utils/astquery.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <set>

namespace strd {

PlatformPolicyManager::PlatformPolicyManager() {}

PlatformPolicyManager::~PlatformPolicyManager() {}

void PlatformPolicyManager::clear() {
  m_platformName.clear();
  m_targetDevice.clear();
  m_compilerFlags.clear();
  m_resources.clear();
  m_supportedDevices.clear();
  m_bindings.clear();
}

std::shared_ptr<DeclarationNode> PlatformPolicyManager::loadDeviceDeclaration(
    const std::string &strideroot, const std::string &platformName,
    const std::string &deviceName) {
  std::string devPath = strideroot + "/platforms/" + platformName + "/devices/" +
                        deviceName + ".stride";
  if (!std::filesystem::exists(devPath)) {
    return nullptr;
  }
  auto devTree = AST::parseFile(devPath.c_str(), nullptr);
  if (!devTree) {
    return nullptr;
  }
  for (const auto &child : devTree->getChildren()) {
    if (child->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getEntityType() == "device" && decl->getName() == deviceName) {
        return decl;
      }
    }
  }
  return nullptr;
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
    if (bindingNode->getNodeType() != AST::Declaration &&
        bindingNode->getNodeType() != AST::ArrayDeclaration) {
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

    std::string policyFull = ASTQuery::getNodeName(policyProp);
    std::string policyShort = policyFull;
    size_t colonPos = policyShort.rfind("::");
    if (colonPos != std::string::npos) {
      policyShort = policyShort.substr(colonPos + 2);
    }

    std::vector<std::string> funcs;
    if (funcProp->getNodeType() == AST::List) {
      for (const auto &child : funcProp->getChildren()) {
        funcs.push_back(ASTQuery::getNodeName(child));
      }
    } else {
      funcs.push_back(ASTQuery::getNodeName(funcProp));
    }

    std::vector<std::string> targets;
    if (targetProp->getNodeType() == AST::List) {
      for (const auto &child : targetProp->getChildren()) {
        targets.push_back(ASTQuery::getNodeName(child));
      }
    } else {
      targets.push_back(ASTQuery::getNodeName(targetProp));
    }

    size_t count = std::min(funcs.size(), targets.size());
    for (size_t i = 0; i < count; ++i) {
      const std::string &fn = funcs[i];
      const std::string &tgt = targets[i];
      if (fn.empty() || tgt.empty()) {
        continue;
      }

      std::string fnShort = fn;
      size_t fnColon = fnShort.rfind("::");
      if (fnColon != std::string::npos) {
        fnShort = fnShort.substr(fnColon + 2);
      }

      m_bindings[{policyShort, fnShort}] = tgt;
      m_bindings[{policyFull, fnShort}] = tgt;
      m_bindings[{policyShort, fn}] = tgt;
      m_bindings[{policyFull, fn}] = tgt;
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
    if (child->getNodeType() == AST::Declaration) {
      auto decl = std::static_pointer_cast<DeclarationNode>(child);
      if (decl->getEntityType() == "platform" && decl->getName() == platformName) {
        platformDecl = decl;
        break;
      }
    }
  }

  if (!platformDecl) {
    return false;
  }

  // Supported devices
  auto suppProp = platformDecl->getPropertyValue("supportedDevices");
  if (suppProp && suppProp->getNodeType() == AST::List) {
    for (const auto &child : suppProp->getChildren()) {
      m_supportedDevices.push_back(ASTQuery::getNodeName(child));
    }
  }

  // Default device
  std::string activeDevice = targetDevice;
  if (activeDevice.empty()) {
    auto defDevProp = platformDecl->getPropertyValue("defaultDevice");
    if (defDevProp) {
      activeDevice = ASTQuery::getNodeName(defDevProp);
    }
  }
  m_targetDevice = activeDevice;

  // Platform baseline policies
  parsePolicyBindings(platformDecl, "platformPolicies");

  if (activeDevice.empty()) {
    return true;
  }

  // Follow overlay chain up to base device
  std::vector<std::string> overlayChain;
  std::set<std::string> visited;
  std::string curr = activeDevice;
  while (!curr.empty() && !visited.count(curr)) {
    visited.insert(curr);
    overlayChain.push_back(curr);
    auto devDecl = loadDeviceDeclaration(strideroot, platformName, curr);
    if (!devDecl) {
      break;
    }
    auto overlaysProp = devDecl->getPropertyValue("overlays");
    if (overlaysProp && overlaysProp->getNodeType() != AST::None) {
      curr = ASTQuery::getNodeName(overlaysProp);
    } else {
      break;
    }
  }

  // Reverse so base device is applied first, leaf overlay last
  std::reverse(overlayChain.begin(), overlayChain.end());

  for (const auto &devName : overlayChain) {
    auto devDecl = loadDeviceDeclaration(strideroot, platformName, devName);
    if (!devDecl) {
      continue;
    }

    // Cumulative resources
    auto resProp = devDecl->getPropertyValue("resources");
    if (resProp) {
      if (resProp->getNodeType() == AST::List) {
        for (const auto &child : resProp->getChildren()) {
          std::string rName = ASTQuery::getNodeName(child);
          if (std::find(m_resources.begin(), m_resources.end(), rName) ==
              m_resources.end()) {
            m_resources.push_back(rName);
          }
        }
      } else {
        std::string rName = ASTQuery::getNodeName(resProp);
        if (std::find(m_resources.begin(), m_resources.end(), rName) ==
            m_resources.end()) {
          m_resources.push_back(rName);
        }
      }
    }

    // Compiler flags (overridden by child)
    auto flagsProp = devDecl->getPropertyValue("compilerFlags");
    if (flagsProp && flagsProp->getNodeType() == AST::String) {
      m_compilerFlags =
          std::static_pointer_cast<ValueNode>(flagsProp)->getStringValue();
    }

    // Device policies (overridden by child)
    parsePolicyBindings(devDecl, "devicePolicies");
  }

  return true;
}

std::string PlatformPolicyManager::resolveVariant(
    const std::string &policyName, const std::string &functionName,
    std::string *outVariantTag) const {
  std::string pShort = policyName;
  size_t pColon = pShort.rfind("::");
  if (pColon != std::string::npos) {
    pShort = pShort.substr(pColon + 2);
  }

  std::string fnShort = functionName;
  size_t fnColon = fnShort.rfind("::");
  if (fnColon != std::string::npos) {
    fnShort = fnShort.substr(fnColon + 2);
  }

  std::vector<std::pair<std::string, std::string>> candidates = {
      {pShort, fnShort},
      {policyName, fnShort},
      {pShort, functionName},
      {policyName, functionName}};

  for (const auto &cand : candidates) {
    auto it = m_bindings.find(cand);
    if (it != m_bindings.end()) {
      std::string targetVariant = it->second;
      if (outVariantTag) {
        size_t atPos = targetVariant.find('@');
        if (atPos != std::string::npos) {
          *outVariantTag = targetVariant.substr(atPos + 1);
        } else {
          outVariantTag->clear();
        }
      }
      return targetVariant;
    }
  }

  if (outVariantTag) {
    outVariantTag->clear();
  }
  return "";
}

bool PlatformPolicyManager::hasBindingForFunction(
    const std::string &functionName) const {
  std::string fnShort = functionName;
  size_t fnColon = fnShort.rfind("::");
  if (fnColon != std::string::npos) {
    fnShort = fnShort.substr(fnColon + 2);
  }
  for (const auto &pair : m_bindings) {
    if (pair.first.second == fnShort || pair.first.second == functionName) {
      return true;
    }
  }
  return false;
}

} // namespace strd

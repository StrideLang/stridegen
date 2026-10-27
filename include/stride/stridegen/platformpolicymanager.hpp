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

#ifndef STRIDE_PLATFORMPOLICYMANAGER_HPP
#define STRIDE_PLATFORMPOLICYMANAGER_HPP

#include "stride/parser/ast.h"
#include "stride/parser/declarationnode.h"
#include <map>
#include <string>
#include <vector>

namespace strd {

struct PolicyBindingRule {
  std::string policy;        // e.g. "Optimal", "Fast", "Accurate", "Compact"
  std::string functionName;  // e.g. "Sin" or "Math::Sin"
  std::string targetVariant; // e.g. "Sin@Direct", "Sin@Poly5"
  std::string targetTag;     // e.g. "Direct", "Poly5"
};

class PlatformPolicyManager {
public:
  PlatformPolicyManager();
  ~PlatformPolicyManager();

  // Loads platform definition from <strideroot>/platforms/<platformName>/Platform.stride
  // If targetDevice is empty, falls back to defaultDevice from Platform.stride.
  // Resolves the device overlay chain and builds the aggregated policy table and resource list.
  bool loadPlatform(const std::string &strideroot,
                    const std::string &platformName,
                    const std::string &targetDevice = "");

  // Resolves variant for an unannotated function/entity within a policy context.
  // Returns targetVariant name (e.g. "Sin@Direct"), and optionally outputs variant tag (e.g. "Direct").
  // Returns empty string if no rule found.
  std::string resolveVariant(const std::string &policyName,
                             const std::string &functionName,
                             std::string *outVariantTag = nullptr) const;

  bool hasBindingForFunction(const std::string &functionName) const;

  const std::string &getPlatformName() const { return m_platformName; }
  const std::string &getTargetDevice() const { return m_targetDevice; }
  const std::string &getCompilerFlags() const { return m_compilerFlags; }
  const std::vector<std::string> &getHardwareResources() const { return m_resources; }
  const std::vector<std::string> &getSupportedDevices() const { return m_supportedDevices; }

  const std::map<std::pair<std::string, std::string>, std::string> &getBindings() const {
    return m_bindings;
  }

  void clear();

private:
  std::string m_platformName;
  std::string m_targetDevice;
  std::string m_compilerFlags;
  std::vector<std::string> m_resources;
  std::vector<std::string> m_supportedDevices;

  // Key: (policyName, functionName) -> targetVariant
  std::map<std::pair<std::string, std::string>, std::string> m_bindings;

  void parsePolicyBindings(const std::shared_ptr<DeclarationNode> &decl,
                           const std::string &propertyName);
  std::shared_ptr<DeclarationNode> loadDeviceDeclaration(const std::string &strideroot,
                                                         const std::string &platformName,
                                                         const std::string &deviceName);
};

} // namespace strd

#endif // STRIDE_PLATFORMPOLICYMANAGER_HPP

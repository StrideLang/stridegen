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

#include "stride/stridegen/systemconfiguration.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/logger.h"

#include <cctype>
#include <fstream>

using namespace strd;

namespace {

std::string capitalizeConfigKey(std::string key) {
  if (key.empty()) {
    return key;
  }
  if (key[0] == '_') {
    if (key.size() > 1) {
      key[1] = static_cast<char>(
          std::toupper(static_cast<unsigned char>(key[1])));
    }
  } else {
    key[0] =
        static_cast<char>(std::toupper(static_cast<unsigned char>(key[0])));
  }
  return key;
}

std::string uncapitalizeConfigKey(std::string key) {
  if (key.empty()) {
    return key;
  }
  if (key[0] == '_') {
    if (key.size() > 1) {
      key[1] = static_cast<char>(
          std::tolower(static_cast<unsigned char>(key[1])));
    }
  } else {
    key[0] =
        static_cast<char>(std::tolower(static_cast<unsigned char>(key[0])));
  }
  return key;
}

} // namespace

SystemConfiguration::SystemConfiguration() = default;

void SystemConfiguration::readConfiguration(std::string filename) {
  std::string configFilename = filename + ".config";
  ASTNode configFile = AST::parseFile(configFilename.c_str());
  if (!configFile) {
    return;
  }

  for (const ASTNode &configNode : configFile->getChildren()) {
    if (!configNode || configNode->getNodeType() != AST::Declaration) {
      continue;
    }
    auto configDecl = std::static_pointer_cast<DeclarationNode>(configNode);
    if (configDecl->getEntityType() == "config") {
      std::string framework = "all";
      if (auto fwNode = configDecl->getPropertyValue("framework");
          fwNode && fwNode->getNodeType() == AST::String) {
        std::string fw =
            std::static_pointer_cast<ValueNode>(fwNode)->getStringValue();
        if (!fw.empty()) {
          framework = fw;
        }
      }
      if (auto resValues = configDecl->getPropertyValue("resourceConfigValues")) {
        for (const auto &configOption : resValues->getChildren()) {
          if (configOption && configOption->getNodeType() == AST::Declaration) {
            resourceConfigurations[framework].push_back(
                std::static_pointer_cast<DeclarationNode>(configOption));
          }
        }
      }
    } else if (configDecl->getEntityType() == "override") {
      for (const auto &configOption : configDecl->getProperties()) {
        if (!configOption || !configOption->getValue()) {
          continue;
        }
        std::string optionName = capitalizeConfigKey(configOption->getName());
        ASTNode configValue = configOption->getValue();
        if (configValue->getNodeType() == AST::String) {
          overrides["all"][optionName] =
              std::static_pointer_cast<ValueNode>(configValue)
                  ->getStringValue();
        } else if (configValue->getNodeType() == AST::Int) {
          overrides["all"][optionName] =
              std::static_pointer_cast<ValueNode>(configValue)->getIntValue();
        } else if (configValue->getNodeType() == AST::Real) {
          overrides["all"][optionName] =
              std::static_pointer_cast<ValueNode>(configValue)->getRealValue();
        }
      }
    }
  }
}

void SystemConfiguration::writeConfiguration(std::string filename) {
  std::ofstream configFile(filename);
  if (!configFile.good()) {
    LOG_ERROR() << "Error opening configuration file '" << filename
                << "' for writing!" << std::endl;
    return;
  }

  const std::string presetName = "Preset";
  for (const auto &[frameworkName, entries] : resourceConfigurations) {
    configFile << "config " << presetName << " {\n";
    configFile << "    framework: \"" << frameworkName << "\"\n";
    configFile << "    resourceConfigValues: [\n";
    for (const auto &option : entries) {
      configFile << AST::toText(option, 4);
    }
    configFile << "    ]\n}\n\n";
  }

  if (auto ovIt = overrides.find("all");
      ovIt != overrides.end() && !ovIt->second.empty()) {
    configFile << "override Test {\n";
    for (const auto &[key, val] : ovIt->second) {
      configFile << "    " << uncapitalizeConfigKey(key) << ": ";
      if (std::holds_alternative<std::string>(val)) {
        configFile << "\"" << std::get<std::string>(val) << "\"";
      } else if (std::holds_alternative<int64_t>(val)) {
        configFile << std::to_string(std::get<int64_t>(val));
      } else if (std::holds_alternative<double>(val)) {
        configFile << std::to_string(std::get<double>(val));
      }
      configFile << "\n";
    }
    configFile << "}\n";
  }
}

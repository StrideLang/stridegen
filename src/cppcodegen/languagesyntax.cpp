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

#include "stride/stridegen/cppcodegen/languagesyntax.hpp"
#include "stride/utils/astquery.h"
#include "stride/utils/logger.h"

#include <limits>
#include <map>

using namespace strd;

std::string LanguageSyntax::getDeclarationType(const std::string &type,
                                               const std::string &name,
                                               const SignalAccess &access,
                                               uint64_t size) {
  (void)name;
  const std::string base = (size > 1) ? "Bundle" : "Signal";
  const std::string helperType = "stride::" + base + "Helper<double>";
  const bool hasReset =
      (access.access & ACCESS_SDRst) || (access.access & ACCESS_MDRst);
  const std::string suffix = hasReset ? "_SDRWRst" : "_SDRW";
  return "stride::" + base + suffix + "<" + helperType + ", " + type + ">";
}

std::string LanguageSyntax::getDeclarationForType(
    const std::string &type, const std::string &name,
    const SignalAccess &access, uint64_t size,
    const std::vector<std::string> &defaultValue,
    const std::vector<std::string> &constructorArgs) {
  (void)access;
  std::string out;
  if (size == std::numeric_limits<uint64_t>::max()) {
    out += "// FIXME Size unresolved\n";
    size = defaultValue.size();
  }

  std::string arraySize;
  if (size > 1) {
    arraySize = "[" + std::to_string(size) + "]";
  }

  std::string constructorInitList;
  if (!constructorArgs.empty()) {
    constructorInitList = "{";
    for (size_t i = 0; i < constructorArgs.size(); ++i) {
      if (i > 0) {
        constructorInitList += ",";
      }
      constructorInitList += constructorArgs[i];
    }
    constructorInitList += "}";
  }

  out += type + " " + name + arraySize + constructorInitList;
  if (size > 0 && !defaultValue.empty()) {
    out += " = ";
    if (size > 1 || defaultValue.size() > 1) {
      out += "{";
    }
    for (uint64_t i = 0; i < size; ++i) {
      if (i > 0) {
        out += ",";
      }
      out += defaultValue[i % defaultValue.size()];
    }
    if (size > 1 || defaultValue.size() > 1) {
      out += "}";
    }
  }
  out += ";\n";
  return out;
}

std::string LanguageSyntax::getDeclarationForType(Instance &instance) {
  return getDeclarationForType(instance.type, instance.fullName(),
                               instance.access, instance.size,
                               instance.defaultValue, instance.constructorArgs);
}

std::string LanguageSyntax::include(const std::string &includeName) {
  if (includeName.empty()) {
    return {};
  }
  if (includeName.front() == '<') {
    return "#include " + includeName + "\n";
  }
  return "#include \"" + includeName + "\"\n";
}

std::string LanguageSyntax::assignment(const std::string &name,
                                       const std::string &value, bool close) {
  std::string out = name + " = " + value;
  if (close) {
    out += endStatement();
  }
  return out;
}

std::string
LanguageSyntax::generateExpression(ExpressionNode::ExpressionType type,
                                   const std::string &left,
                                   const std::string &right) {
  switch (type) {
  case ExpressionNode::Multiply:
    return left + " * " + right;
  case ExpressionNode::Divide:
    return left + " / " + right;
  case ExpressionNode::Add:
    return left + " + " + right;
  case ExpressionNode::Subtract:
    return left + " - " + right;
  case ExpressionNode::And:
    return left + " && " + right;
  case ExpressionNode::Or:
    return left + " || " + right;
  case ExpressionNode::BitAnd:
    return left + " & " + right;
  case ExpressionNode::BitOr:
    return left + " | " + right;
  case ExpressionNode::UnaryMinus:
    return " - " + left;
  case ExpressionNode::LogicalNot:
    return "!" + left;
  case ExpressionNode::BitNot:
    return "~" + left;
  default:
    return {};
  }
}

std::string LanguageSyntax::functionCall(const std::string &name,
                                         const std::string &arguments,
                                         const std::string &templateParams,
                                         bool close) {
  std::string text = name;
  if (!templateParams.empty()) {
    text += "<" + templateParams + ">";
  }
  text += "(" + arguments + ")";
  if (close) {
    text += ";\n";
  }
  return text;
}

std::string LanguageSyntax::functionDeclaration(const std::string &returnType,
                                                const std::string &name,
                                                const std::string &arguments,
                                                const std::string &code,
                                                const std::string &templateArgs) {
  std::string text;
  if (!templateArgs.empty()) {
    text += "template<" + templateArgs + ">\n";
  }
  text += returnType + " " + name + "(" + arguments + ") {\n";
  text += code;
  text += "}\n";
  return text;
}

std::string LanguageSyntax::generateModuleClass(
    const std::string &functionName, const std::string &templateArgs,
    const std::string &declarations, const std::string &arguments,
    const std::string &constructorCode, const std::string &functionCode) {
  std::string moduleCode;
  if (!templateArgs.empty()) {
    moduleCode += "template<" + templateArgs + ">\n";
  }
  moduleCode += "class " + functionName + " {\npublic:\n";
  moduleCode += declarations;
  if (!arguments.empty() && !constructorCode.empty()) {
    moduleCode += functionName + "(" + arguments + ")";
    moduleCode += "{" + constructorCode + "}\n";
  }
  moduleCode += functionCode;
  moduleCode += "};\n\n";
  return moduleCode;
}

std::string
LanguageSyntax::getDataType(const std::shared_ptr<DeclarationNode> &decl,
                            StrideSystem *system) {
  if (!decl || !system) {
    return {};
  }

  std::string frameworkName;
  if (auto frameworkNode = decl->getCompilerProperty("framework");
      frameworkNode && frameworkNode->getNodeType() == AST::String) {
    frameworkName =
        std::static_pointer_cast<ValueNode>(frameworkNode)->getStringValue();
  }
  if (decl->getEntityType() == "switch" ||
      decl->getEntityType() == "reaction") {
    return "bool";
  }

  auto declaredType = decl->getPropertyValue("type");
  if (declaredType && declaredType->getNodeType() == AST::Entity) {
    auto dataTypeDecl = system->getFrameworkDataType(
        frameworkName,
        std::static_pointer_cast<EntityNode>(declaredType)->getName());
    if (dataTypeDecl) {
      auto frameworkTypeNode = dataTypeDecl->getPropertyValue("frameworkName");
      if (frameworkTypeNode &&
          frameworkTypeNode->getNodeType() == AST::String) {
        return std::static_pointer_cast<ValueNode>(frameworkTypeNode)
            ->getStringValue();
      }
    }
  }

  static const std::map<std::string, std::string> builtinDataTypes = {
      {"string", DataTypes::String},
      {"trigger", DataTypes::Switch},
      {"iterator", DataTypes::Int},
      {"sequence", DataTypes::Int}};
  std::string defaultDataType;
  auto builtinDataType = builtinDataTypes.find(decl->getEntityType());
  if (builtinDataType != builtinDataTypes.end()) {
    defaultDataType = builtinDataType->second;
  }

  auto dataTypeNode =
      system->getFrameworkDataType(frameworkName, defaultDataType);
  if (!dataTypeNode || dataTypeNode->getNodeType() == AST::None) {
    defaultDataType = system->getFrameworkDefaultDataType(frameworkName);
    dataTypeNode = system->getFrameworkDataType(frameworkName, defaultDataType);
  }

  if (dataTypeNode) {
    auto dataTypeNameNode = dataTypeNode->getPropertyValue("frameworkName");
    if (dataTypeNameNode && dataTypeNameNode->getNodeType() == AST::String) {
      return std::static_pointer_cast<ValueNode>(dataTypeNameNode)
          ->getStringValue();
    }
  } else {
    LOG_ERROR() << "No datatype provided for declaration " << decl->getName()
                << std::endl;
  }
  return {};
}

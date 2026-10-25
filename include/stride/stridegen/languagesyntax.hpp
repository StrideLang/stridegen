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

#ifndef LANGUAGESYNTAX_HPP
#define LANGUAGESYNTAX_HPP

#include <string>

#include "codeentities.hpp"
#include "stride/parser/expressionnode.h"
#include "stride/parser/valuenode.h"
#include "stridesystem.hpp"

namespace strd {
class LanguageSyntax {
public:
  static std::string endStatement() { return ";\n"; }

  static std::string getDeclarationType(std::string type, std::string name,
                                        SignalAccess access, uint64_t size);
  static std::string
  getDeclarationForType(std::string type, std::string name, SignalAccess access,
                        uint64_t size, std::vector<std::string> defaultValue,
                        std::vector<std::string> constructorArgs = {});
  static std::string getDeclarationForType(Instance &instance);

  //  static std::string instance(Instance &inst, bool close = true);

  static std::string include(std::string includeToken);

  //  static std::string instanceReal(
  //      std::string name, int size = 1, bool close = true,
  //      std::vector<std::string> defaultValue = std::vector<std::string>());
  //  static std::string instanceBool(
  //      std::string name, int size = 1, bool close = true,
  //      std::vector<std::string> defaultValue = std::vector<std::string>());

  static std::string assignment(std::string name, std::string value,
                                bool close = true);

  static std::string dereference(std::string varName) { return "*" + varName; }

  static std::string reference(std::string varName) { return "&" + varName; }

  static std::string generateExpression(ExpressionNode::ExpressionType type,
                                        std::string left,
                                        std::string right = std::string());

  static std::string domainProcessCall(std::string domainId,
                                       std::string instanceToken,
                                       std::string outToken) {
    return functionCall(instanceToken + "." + domainId + "_process", outToken);
  }

  static std::string functionCall(std::string name, std::string arguments,
                                  std::string templateParams = "",
                                  bool close = true);

  static std::string functionDeclaration(std::string returnType,
                                         std::string name,
                                         std::string arguments,
                                         std::string code,
                                         std::string templateArgs = "");

  static std::string generateModuleDeclaration(std::string functionName,
                                               std::string templateArgs,
                                               std::string declarations,
                                               std::string arguments,
                                               std::string constructorCode,
                                               std::string functionCode);

  static std::string reactionConditionBegin(std::string condition) {
    return "if (" + condition + ") {\n";
  }

  static std::string reactionConditionEnd() { return "}\n"; }
  static std::string loopConditionBegin(std::string condition) {
    return "while (!(" + condition + ")) {\n";
  }

  static std::string loopConditionEnd() { return "}\n"; }

  static std::string triggerBegin(std::string condition) {
    return "if (" + condition + ") {\n";
  }

  static std::string triggerEnd() { return "}\n"; }

  static std::string trueKeyword() { return "true"; }

  static std::string falseKeyword() { return "false"; }

  static std::string numberValue(std::shared_ptr<ValueNode> valueNode) {
    std::string literalString;
    if (valueNode->getNodeType() == AST::Int) {
      literalString = valueNode->toString();
    } else if (valueNode->getNodeType() == AST::Real) {
      literalString = valueNode->toString();
      if (literalString.find('.') == std::string::npos) {
        literalString += ".";
      }
      literalString += "f";
    }

    return literalString;
  }

  //  static std::string getDataType(std::string strideType);

  static std::string getDataType(std::shared_ptr<DeclarationNode> decl,
                                 StrideSystem *system);
};
} // namespace strd

#endif // LANGUAGESYNTAX_HPP

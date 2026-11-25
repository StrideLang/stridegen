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

#ifndef STRIDE_CPPCODEGEN_LANGUAGESYNTAX_HPP
#define STRIDE_CPPCODEGEN_LANGUAGESYNTAX_HPP

#include <string>

#include "stride/parser/expressionnode.h"
#include "stride/parser/valuenode.h"
#include "stride/stridegen/cppcodegen/codeentities.hpp"
#include "stride/stridegen/stridesystem.hpp"

namespace strd {
// C++ source-level code generation helpers (separate from LLVM JIT backend).
class LanguageSyntax {
public:
  static std::string endStatement() { return ";\n"; }

  static std::string getDeclarationType(const std::string &type,
                                        const std::string &name,
                                        const SignalAccess &access,
                                        uint64_t size);
  static std::string
  getDeclarationForType(const std::string &type, const std::string &name,
                        const SignalAccess &access, uint64_t size,
                        const std::vector<std::string> &defaultValue,
                        const std::vector<std::string> &constructorArgs = {});
  static std::string getDeclarationForType(Instance &instance);

  static std::string include(const std::string &includeToken);

  static std::string assignment(const std::string &name,
                                const std::string &value, bool close = true);

  static std::string dereference(const std::string &varName) {
    return "*" + varName;
  }

  static std::string reference(const std::string &varName) {
    return "&" + varName;
  }

  static std::string generateExpression(ExpressionNode::ExpressionType type,
                                        const std::string &left,
                                        const std::string &right = std::string());

  static std::string domainProcessCall(const std::string &domainId,
                                       const std::string &instanceToken,
                                       const std::string &outToken) {
    return functionCall(instanceToken + "." + domainId + "_process", outToken);
  }

  static std::string functionCall(const std::string &name,
                                  const std::string &arguments,
                                  const std::string &templateParams = "",
                                  bool close = true);

  static std::string functionDeclaration(const std::string &returnType,
                                         const std::string &name,
                                         const std::string &arguments,
                                         const std::string &code,
                                         const std::string &templateArgs = "");

  static std::string generateModuleClass(const std::string &functionName,
                                         const std::string &templateArgs,
                                         const std::string &declarations,
                                         const std::string &arguments,
                                         const std::string &constructorCode,
                                         const std::string &functionCode);

  static std::string reactionConditionBegin(const std::string &condition) {
    return "if (" + condition + ") {\n";
  }

  static std::string reactionConditionEnd() { return "}\n"; }
  static std::string loopConditionBegin(const std::string &condition) {
    return "while (!(" + condition + ")) {\n";
  }

  static std::string loopConditionEnd() { return "}\n"; }

  static std::string triggerBegin(const std::string &condition) {
    return "if (" + condition + ") {\n";
  }

  static std::string triggerEnd() { return "}\n"; }

  static std::string trueKeyword() { return "true"; }
  static std::string falseKeyword() { return "false"; }

  static std::string numberValue(const std::shared_ptr<ValueNode> &valueNode) {
    if (!valueNode) {
      return {};
    }
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

  static std::string getDataType(const std::shared_ptr<DeclarationNode> &decl,
                                 StrideSystem *system);
};
} // namespace strd

#endif // STRIDE_CPPCODEGEN_LANGUAGESYNTAX_HPP

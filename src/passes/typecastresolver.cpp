/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#include "typecastresolver.hpp"

#include "stride/parser/valuenode.h"
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astfunctions.h"

#include <vector>

using namespace strd;

void TypeCastResolver::resolveTypeCasting() {
  for (const auto &node : m_ctx.tree->getChildren()) {
    resolveTypeCastForNode(node, {}, m_ctx.tree);
  }
}

void TypeCastResolver::resolveTypeCastForNode(const ASTNode &node,
                                              const ScopeStack &scopeStack,
                                              const ASTNode &tree) {
  if (!node) {
    return;
  }
  if (node->getNodeType() == AST::Stream) {
    resolveTypeCastForStream(std::static_pointer_cast<StreamNode>(node),
                             scopeStack, tree);
  } else if (node->getNodeType() == AST::Declaration ||
             node->getNodeType() == AST::ArrayDeclaration) {
    resolveTypeCastForDeclaration(
        std::static_pointer_cast<DeclarationNode>(node), scopeStack, tree);
  }
}

void TypeCastResolver::resolveTypeCastForStream(
    const std::shared_ptr<StreamNode> &stream, const ScopeStack &scopeStack,
    const ASTNode &tree) {
  if (!stream) {
    return;
  }
  ASTNode left = stream->getLeft();
  ASTNode right = stream->getRight();
  ASTNode next = right;

  auto freeLiteral = [](const ASTNode &n) {
    return n && n->getNodeType() == AST::Int &&
           std::static_pointer_cast<ValueNode>(n)->getIntValue() >= 0;
  };

  while (left) {
    if (right && right->getNodeType() == AST::Stream) {
      next = std::static_pointer_cast<StreamNode>(right)->getLeft();
    }
    bool isVariadicFunctionCall = false;
    if (left->getNodeType() == AST::List && next &&
        next->getNodeType() == AST::Function) {
      if (CodeAnalysis::getNodeNumInputs(next, scopeStack, tree) ==
          CodeAnalysis::SIZE_MEMBER_ACCESS) {
        isVariadicFunctionCall = true;
      }
    }
    if ((left->getNodeType() == AST::List && !isVariadicFunctionCall) ||
        left->getNodeType() == AST::Expression) {
      std::string resolvedType;
      bool needsResolution = false;
      bool validNumericList = true;
      std::vector<ASTNode> literals;

      for (const auto &listNode : left->getChildren()) {
        std::string nodeType =
            CodeAnalysis::resolveNodeOutDataType(listNode, scopeStack, tree);
        if (!DataTypes::resolve(nodeType, tree).isNumeric()) {
          validNumericList = false;
          break;
        }
        if (freeLiteral(listNode)) {
          literals.push_back(listNode);
          continue;
        }
        if (resolvedType.empty()) {
          resolvedType = nodeType;
        } else if (nodeType != resolvedType) {
          resolvedType = DataTypes::promote(resolvedType, nodeType, tree);
          needsResolution = true;
        }
      }

      for (const auto &literal : literals) {
        std::string nodeType =
            CodeAnalysis::resolveNodeOutDataType(literal, scopeStack, tree);
        if (resolvedType.empty()) {
          resolvedType = nodeType;
          continue;
        }
        if (nodeType == resolvedType) {
          continue;
        }
        const auto info = DataTypes::resolve(resolvedType, tree);
        const int64_t value =
            std::static_pointer_cast<ValueNode>(literal)->getIntValue();
        bool fits = !info.isInteger();
        if (info.isInteger()) {
          const unsigned bits = info.isSigned ? info.bits - 1 : info.bits;
          fits = bits >= 63 || value <= ((int64_t(1) << bits) - 1);
        }
        if (!fits) {
          resolvedType = DataTypes::promote(resolvedType, nodeType, tree);
        }
        needsResolution = true;
      }

      if (needsResolution && validNumericList &&
          left->getNodeType() == AST::Expression) {
        resolvedType =
            CodeAnalysis::resolveNodeOutDataType(left, scopeStack, tree);
      }
      if (needsResolution && validNumericList) {
        for (const auto &listNode : left->getChildren()) {
          std::string nodeType =
              CodeAnalysis::resolveNodeOutDataType(listNode, scopeStack, tree);
          if (nodeType != resolvedType) {
            setTypecast(listNode, nodeType, resolvedType);
          }
        }
      }
    }

    if (right) {
      std::string leftType =
          CodeAnalysis::resolveNodeOutDataType(left, scopeStack, tree);
      std::string nextType =
          CodeAnalysis::resolveNodeOutDataType(next, scopeStack, tree);
      if (!leftType.empty() && !nextType.empty() && leftType != nextType) {
        std::string widened = DataTypes::widenedType(leftType, nextType, tree);
        if (left->getNodeType() == AST::Expression && widened != leftType) {
          widenOperands(left, widened, scopeStack, tree);
          leftType = widened;
        }
        if (leftType != nextType) {
          setTypecast(left, leftType, nextType);
        }
      }
    }

    if (right) {
      if (right->getNodeType() == AST::Stream) {
        auto rightStream = std::static_pointer_cast<StreamNode>(right);
        left = rightStream->getLeft();
        right = rightStream->getRight();
        next = right;
      } else {
        left = right;
        next = right = nullptr;
      }
    } else {
      left = nullptr;
    }
  }
}

void TypeCastResolver::setTypecast(const ASTNode &node, const std::string &from,
                                   const std::string &to) {
  node->setCompilerProperty(
      "typecast", std::make_shared<ValueNode>(to, __FILE__, __LINE__));
  // Read by CodeValidator::findLossyConversions()
  node->setCompilerProperty(
      "typecastFrom", std::make_shared<ValueNode>(from, __FILE__, __LINE__));
}

void TypeCastResolver::widenOperands(const ASTNode &expression,
                                     const std::string &type,
                                     const ScopeStack &scopeStack,
                                     const ASTNode &tree) {
  for (const auto &operand : expression->getChildren()) {
    if (!operand) {
      continue;
    }
    if (operand->getNodeType() == AST::Expression) {
      widenOperands(operand, type, scopeStack, tree);
      continue;
    }
    if (operand->getNodeType() != AST::Int &&
        operand->getNodeType() != AST::Real &&
        operand->getNodeType() != AST::Entity &&
        operand->getNodeType() != AST::Array) {
      continue;
    }
    std::string operandType =
        CodeAnalysis::resolveNodeOutDataType(operand, scopeStack, tree);
    if (DataTypes::resolve(operandType, tree).isNumeric() &&
        operandType != type) {
      setTypecast(operand, operandType, type);
    }
  }
}

void TypeCastResolver::resolveTypeCastForDeclaration(
    const std::shared_ptr<DeclarationNode> &decl, ScopeStack scopeStack,
    const ASTNode &tree) {
  auto entities = decl->getPropertyValue("entities");
  if (entities) {
    scopeStack.push_back({decl, entities->getChildren()});
    for (const auto &node : entities->getChildren()) {
      resolveTypeCastForNode(node, scopeStack, tree);
    }
  }
  auto streams = decl->getPropertyValue("streams");
  if (streams) {
    for (const auto &node : streams->getChildren()) {
      resolveTypeCastForNode(node, scopeStack, tree);
    }
  }
}

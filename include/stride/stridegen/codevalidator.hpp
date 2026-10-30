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

#ifndef STRIDEGEN_H
#define STRIDEGEN_H

#include "stride/parser/strideparser.h"

namespace strd {
class StrideSystem;

typedef std::vector<std::pair<ASTNode, std::vector<ASTNode>>> ScopeStack;

class CodeValidator {
public:
  typedef enum { NO_OPTIONS = 0x00, NO_RATE_VALIDATION = 0x01 } Options;

  CodeValidator(ASTNode tree = nullptr, Options options = NO_OPTIONS);
  ~CodeValidator();

  bool isValid();
  bool platformIsValid();

  std::vector<LangError> getErrors();
  // Warnings don't make the tree invalid, e.g. lossy conversions
  std::vector<LangError> getWarnings();
  std::vector<std::string> getPlatformErrors();

  // Static functions -----------------------------

  static std::vector<StreamNode *> getStreamsAtLine(ASTNode tree, int line);

  // Conversions in a resolved tree that can lose data or change values:
  // narrowing, signed to unsigned, float to integer, integers too large for
  // a float, and mixed operands whose promoted type can't hold both. Literals
  // that fit the destination type aren't reported. Each one is a
  // LossyConversion warning with tokens {from, to, reason}.
  static std::vector<LangError> findLossyConversions(ASTNode tree);

  // Calls in a resolved tree that could not be broadcast automatically
  // because their input bundles have different widths ([A[0:8], B[0:7]] >>
  // Equal()). Each one is a StreamMemberSizeMismatch error.
  static std::vector<LangError> findBroadcastMismatches(ASTNode tree);

  // Writes to constants after their first: a constant with a value is
  // written by it, and one without a value is written once, by the first
  // stream that writes it (constant Table[8] { type: Int } filled at setup).
  // Each later write is a ConstantWrittenTwice error.
  static std::vector<LangError> findConstantRewrites(ASTNode tree);

  // Calls of functions with blocks sized by a main port whose size nothing
  // gives: Output[OutputPort.size] takes the size of what the call writes
  // to, so the call must write to a declared array, element or bundle, and
  // Input[InputPort.size] needs the call to be fed. Each is an UnsizedPort
  // error.
  static std::vector<LangError> findUnsizedPorts(ASTNode tree);

  // Reactions and actions that declare a mainInputPort. Their input is the
  // trigger that runs them (a switch converts to one), which also sets their
  // domain, so values reach them through property ports. Each is a
  // ReactionMainInput error.
  static std::vector<LangError> findReactionMainInputs(ASTNode tree);

private:
  void validateTree(ASTNode tree);
  void validate();

  void validatePlatform(ASTNode node, std::vector<LangError> &errors);

  void validateArrayIndices(ASTNode node, std::vector<LangError> &errors,
                            ScopeStack scope);
  void validateArraySizes(ASTNode node, std::vector<LangError> &errors,
                          ScopeStack scope);
  void validateSymbolUniqueness(ScopeStack scope,
                                std::vector<LangError> &errors);
  void validateStreamSizes(ASTNode tree, std::vector<LangError> &errors,
                           ScopeStack scope);
  void validateRates(ASTNode tree);
  void validateConstraints(ASTNode tree);

  void sortErrors();

  void validateStreamInputSize(StreamNode *stream,
                               std::vector<LangError> &errors,
                               ScopeStack scope);

  void validateNodeRate(ASTNode node, ASTNode tree);

  void validateConstraints(std::shared_ptr<StreamNode> stream,
                           ScopeStack scopeStack, ASTNode tree);
  void validateFunctionConstraints(std::shared_ptr<FunctionNode> function,
                                   ScopeStack scopeStack, ASTNode tree);

  void validateConstraintStream(std::shared_ptr<StreamNode> stream,
                                std::shared_ptr<FunctionNode> function,
                                std::shared_ptr<DeclarationNode> declaration,
                                ScopeStack scopeStack, ASTNode tree);

  std::vector<ASTNode>
  resolveConstraintNode(ASTNode node, std::vector<ASTNode> previous,
                        std::shared_ptr<FunctionNode> function,
                        std::shared_ptr<DeclarationNode> declaration,
                        ScopeStack scopeStack, ASTNode tree);

  int getEntityDataSize(std::shared_ptr<DeclarationNode> declaration,
                        ScopeStack scope,
                        std::vector<LangError> *errors = nullptr);

  std::shared_ptr<StrideSystem> m_system;
  ASTNode m_tree;
  std::vector<LangError> m_errors;
  std::vector<LangError> m_warnings;
  Options m_options;
};
} // namespace strd

#endif // STRIDEGEN_H

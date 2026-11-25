/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

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

#ifndef CODEVALIDATOR_HPP
#define CODEVALIDATOR_HPP

#include <string>
#include <vector>

#include "stride/parser/strideparser.h"
#include "stride/utils/astquery.h"

namespace strd {

class CodeValidator {
public:
  enum Options { NO_OPTIONS = 0x00, NO_RATE_VALIDATION = 0x01 };

  explicit CodeValidator(const ASTNode &tree = nullptr,
                         Options options = NO_OPTIONS);
  ~CodeValidator() = default;

  bool isValid() const { return m_errors.empty(); }

  const std::vector<LangError> &getErrors() const { return m_errors; }
  // Warnings don't make the tree invalid, e.g. lossy conversions
  const std::vector<LangError> &getWarnings() const { return m_warnings; }

  // Conversions in a resolved tree that can lose data or change values:
  // narrowing, signed to unsigned, float to integer, integers too large for
  // a float, and mixed operands whose promoted type can't hold both. Literals
  // that fit the destination type aren't reported. Each one is a
  // LossyConversion warning with tokens {from, to, reason}.
  static std::vector<LangError> findLossyConversions(const ASTNode &tree);

  // Calls in a resolved tree that could not be broadcast automatically
  // because their input bundles have different widths ([A[0:8], B[0:7]] >>
  // Equal()). Each one is a StreamMemberSizeMismatch error.
  static std::vector<LangError> findBroadcastMismatches(const ASTNode &tree);

  // Calls of functions with entities sized by a main port whose size nothing
  // gives: Output[OutputPort.size] takes the size of what the call writes
  // to, so the call must write to a declared array, element or bundle, and
  // Input[InputPort.size] needs the call to be fed. Each is an UnsizedPort
  // error.
  static std::vector<LangError> findUnsizedPorts(const ASTNode &tree);

  // Reads across domains: a signal in one domain read from another domain is
  // valid only if the domains share a common ancestor domain (shared memory)
  // or an explicit connection exists between them.
  static std::vector<LangError>
  findInvalidCrossDomainAccess(const ASTNode &tree);

  // Resource dependency and conflict validation: checks that operations in each
  // active domain have their required resources (and transitive dependsOn)
  // satisfied by the domain, and enforces exclusive resources, maxInstances,
  // and peripheral reservation conflicts.
  static std::vector<LangError> findResourceErrors(ASTNode tree);

private:
  void validate();

  void validateArraySizes(const ASTNode &node, std::vector<LangError> &errors,
                          const ScopeStack &scope);
  void validateStreamSizes(const ASTNode &tree, std::vector<LangError> &errors,
                           const ScopeStack &scope);
  void validateLoopIteratorTripCounts(const ASTNode &tree,
                                      std::vector<LangError> &errors);
  void validateRates(const ASTNode &tree);
  void validateConstraints(const ASTNode &tree);

  void sortDiagnostics();

  void validateStreamInputSize(StreamNode *stream,
                               std::vector<LangError> &errors,
                               const ScopeStack &scope);
  void validateNodeRate(const ASTNode &node, const ASTNode &tree);

  void validateConstraintsForStream(std::shared_ptr<StreamNode> stream,
                                    const ScopeStack &scopeStack,
                                    const ASTNode &tree);
  void
  validateFunctionConstraints(const std::shared_ptr<FunctionNode> &function,
                              ScopeStack scopeStack, const ASTNode &tree);
  void validateConstraintStream(std::shared_ptr<StreamNode> stream,
                                const std::shared_ptr<FunctionNode> &function,
                                const std::shared_ptr<DeclarationNode> &decl,
                                const ScopeStack &scopeStack,
                                const ASTNode &tree);
  std::vector<ASTNode>
  resolveConstraintNode(const ASTNode &node,
                        const std::vector<ASTNode> &previous,
                        const std::shared_ptr<FunctionNode> &function,
                        const std::shared_ptr<DeclarationNode> &decl,
                        const ScopeStack &scopeStack, const ASTNode &tree);

  int getEntityDataSize(const std::shared_ptr<DeclarationNode> &declaration,
                        const ScopeStack &scope,
                        std::vector<LangError> *errors = nullptr);

  ASTNode m_tree;
  std::vector<LangError> m_errors;
  std::vector<LangError> m_warnings;
  Options m_options;
};

} // namespace strd

#endif // CODEVALIDATOR_HPP

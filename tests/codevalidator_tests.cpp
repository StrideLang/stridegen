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

#include <algorithm>
#include <chrono>
#include "gtest/gtest.h"

#include "stride/stridegen/coderesolver.hpp"
#include "stride/stridegen/codevalidator.hpp"
#include "stride/parser/platformnode.h"
#include "stride/utils/astfunctions.h"

using namespace strd;

TEST(CodeValidator, ValidTree) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "resolver/block_persistence.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  EXPECT_TRUE(validator.isValid());
  EXPECT_EQ(validator.getErrors().size(), 0);
  for (auto &error : validator.getErrors()) {
    ADD_FAILURE() << error.getErrorText();
  }
}

TEST(CodeValidator, InvalidStreamSizes) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "validator/invalid_sizes.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  EXPECT_FALSE(validator.isValid());

  auto errors = validator.getErrors();
  EXPECT_GT(errors.size(), 0);
  // Look for stream size mismatch error
  bool foundSizeError = false;
  for (const auto &err : errors) {
    if (err.type == LangError::StreamMemberSizeMismatch) {
      foundSizeError = true;
      break;
    }
  }
  EXPECT_TRUE(foundSizeError);
}

TEST(CodeValidator, ShadowedSymbolInReactionOrLoop) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "validator/shadowed_symbol.stride");
  ASSERT_TRUE(tree != nullptr);
  // The system brings in the library types that make reactions and loops
  // code generators and domain members.
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  EXPECT_FALSE(validator.isValid());

  // {shadowing block, owner}
  std::vector<std::pair<std::string, std::string>> shadowed;
  for (auto &err : validator.getErrors()) {
    if (err.type == LangError::ShadowedSymbol) {
      ASSERT_EQ(err.errorTokens.size(), 4u);
      shadowed.push_back({err.errorTokens[0], err.errorTokens[1]});
      EXPECT_FALSE(err.getErrorText().empty());
    }
  }
  std::sort(shadowed.begin(), shadowed.end());
  std::vector<std::pair<std::string, std::string>> expected = {
      {"Count", "ShadowingLoop"}, {"Flag", "ShadowingReaction"}};
  // NotShadowingModule's Level is a module block, which may reuse the name.
  EXPECT_EQ(shadowed, expected);
}

TEST(CodeValidator, InvalidBundleAccess) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "validator/invalid_bundle.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  // TODO implement validation of statically sized bundles
  // EXPECT_FALSE(validator.isValid());

  // auto errors = validator.getErrors();
  // EXPECT_GT(errors.size(), 0);

  // bool foundIndexError = false;
  // for (const auto &err : errors) {
  //   if (err.type == LangError::ArrayIndexOutOfBounds) {
  //     foundIndexError = true;
  //     break;
  //   }
  // }
  // EXPECT_TRUE(foundIndexError);
}

TEST(CodeValidator, BenchmarkValidation) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "resolver/block_persistence.stride");
  ASSERT_TRUE(tree != nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  auto start = std::chrono::steady_clock::now();
  CodeValidator validator(tree);
  auto end = std::chrono::steady_clock::now();

  EXPECT_TRUE(validator.isValid());
  auto elapsedMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  std::cout << "[BENCHMARK] CodeValidator runtime: " << elapsedMs << " ms"
            << std::endl;
  // Should validate basic tree in well under 1000ms
  EXPECT_LT(elapsedMs, 1000);
}

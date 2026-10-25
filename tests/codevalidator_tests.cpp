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
#include "stride/utils/astquery.h"

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

TEST(CodeValidator, DataTypesAndSchemaTypes) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "validator/data_types.stride");
  ASSERT_TRUE(tree != nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  EXPECT_FALSE(validator.isValid());

  std::vector<std::string> invalid;
  for (auto &err : validator.getErrors()) {
    if (err.type == LangError::InvalidDataType) {
      ASSERT_EQ(err.errorTokens.size(), 2u);
      invalid.push_back(err.errorTokens[0]);
      EXPECT_FALSE(err.getErrorText().empty());
    }
  }
  std::sort(invalid.begin(), invalid.end());
  // The library and JIT declarations, and the valid blocks at the top of the
  // file, report nothing.
  std::vector<std::string> expected = {
      "Byte8", "Float16",      "Int12",       "Int32",
      "LoopA", "LoopB",        "NoKind",      "NumbersClass",
      "Real",  "_Hidden",      "_HiddenAlias", "_IntLiteral",
      "_IntLiteral", "_NumericTypes"};
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(invalid, expected);
  if (invalid != expected) {
    for (auto &err : validator.getErrors()) {
      if (err.type == LangError::InvalidDataType) {
        ADD_FAILURE() << err.getErrorText();
      }
    }
  }
}

TEST(DataTypes, AliasesPromotionAndConversion) {
  // Builtin descriptions apply without a tree
  auto intInfo = DataTypes::resolve("Int");
  EXPECT_EQ(intInfo.canonical, "Int32");
  EXPECT_TRUE(intInfo.isInteger());
  EXPECT_TRUE(intInfo.isSigned);
  auto byteInfo = DataTypes::resolve("UInt8");
  EXPECT_EQ(byteInfo.bits, 8);
  EXPECT_FALSE(byteInfo.isSigned);
  EXPECT_FALSE(DataTypes::resolve("_IntLiteral").valid());
  EXPECT_FALSE(DataTypes::isDataTypeName("_NumericTypes"));

  EXPECT_EQ(DataTypes::promote("Int", "Real"), "Real");
  EXPECT_EQ(DataTypes::promote("Float32", "Float64"), "Float64");
  EXPECT_EQ(DataTypes::promote("Int", "Int32"), "Int");
  EXPECT_EQ(DataTypes::promote("UInt8", "UInt16"), "UInt16");
  EXPECT_EQ(DataTypes::promote("Int", "String"), "");
  // Mixing signed and unsigned gives a signed type that holds both
  EXPECT_EQ(DataTypes::promote("UInt8", "Int16"), "Int16");
  EXPECT_EQ(DataTypes::promote("UInt8", "Int8"), "Int16");
  EXPECT_EQ(DataTypes::promote("Int32", "UInt32"), "Int64");
  EXPECT_EQ(DataTypes::promote("UInt16", "Int8"), "Int32");
  EXPECT_EQ(DataTypes::promote("UInt64", "Int8"), "Int64");
  EXPECT_TRUE(DataTypes::promotionRisk("Int32", "UInt32").empty());
  EXPECT_FALSE(DataTypes::promotionRisk("UInt64", "Int8").empty());
  EXPECT_FALSE(DataTypes::promotionRisk("Int64", "Real").empty());

  EXPECT_TRUE(DataTypes::conversionRisk("Int16", "Int32").empty());
  EXPECT_TRUE(DataTypes::conversionRisk("UInt8", "Int16").empty());
  EXPECT_TRUE(DataTypes::conversionRisk("Int32", "Float64").empty());
  EXPECT_FALSE(DataTypes::conversionRisk("Int32", "Int16").empty());
  EXPECT_FALSE(DataTypes::conversionRisk("Int8", "UInt32").empty());
  EXPECT_FALSE(DataTypes::conversionRisk("UInt32", "Int32").empty());
  EXPECT_FALSE(DataTypes::conversionRisk("Real", "Int").empty());
  EXPECT_FALSE(DataTypes::conversionRisk("Float64", "Float32").empty());
  EXPECT_FALSE(DataTypes::conversionRisk("Int32", "Float32").empty());

  EXPECT_TRUE(DataTypes::implicitlyConvertible("Int", "Real"));
  EXPECT_TRUE(DataTypes::implicitlyConvertible("UInt8", "Int16"));
  EXPECT_TRUE(DataTypes::implicitlyConvertible("Int", "Int32"));
  EXPECT_FALSE(DataTypes::implicitlyConvertible("Real", "Int"));
  EXPECT_FALSE(DataTypes::implicitlyConvertible("Int8", "UInt16"));
  EXPECT_FALSE(DataTypes::implicitlyConvertible("UInt32", "Int32"));
  EXPECT_FALSE(DataTypes::implicitlyConvertible("Switch", "Int"));
}

TEST(CodeValidator, LossyConversions) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "validator/lossy_conversions.stride");
  ASSERT_TRUE(tree != nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  // {line, to type}
  std::vector<std::pair<int, std::string>> found;
  for (auto &warning : validator.getWarnings()) {
    EXPECT_EQ(warning.type, LangError::LossyConversion);
    EXPECT_EQ(warning.severity, LangError::Warning);
    ASSERT_EQ(warning.errorTokens.size(), 3u);
    EXPECT_FALSE(warning.errorTokens[2].empty());
    found.push_back({warning.lineNumber, warning.errorTokens[1]});
  }
  std::sort(found.begin(), found.end());
  std::vector<std::pair<int, std::string>> expected = {
      {11, "UInt8"}, {13, "Int"}, {15, "Int64"}, {21, "UInt8"}};
  EXPECT_EQ(found, expected);
  // Warnings don't make the tree invalid
  for (auto &err : validator.getErrors()) {
    EXPECT_NE(err.type, LangError::LossyConversion);
  }
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

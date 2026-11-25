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
#include <fstream>
#include <map>
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

TEST(CodeValidator, IgnoredRateInOneShotDomainWarning) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  std::string code = R"(
_domainDefinition OneShotDom {
    rate: -1
}
signal RatedSig { rate: 100 domain: OneShotDom }
signal OutSig { domain: OneShotDom }
RatedSig >> OutSig;
)";
  std::string tempFile = "temp_validator_oneshot.stride";
  {
    std::ofstream out(tempFile);
    out << code;
  }
  ASTNode tree = AST::parseFile(tempFile.c_str());
  ASSERT_NE(tree, nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  EXPECT_TRUE(validator.isValid());
  EXPECT_EQ(validator.getErrors().size(), 0u);

  bool foundWarning = false;
  for (const auto &w : validator.getWarnings()) {
    if (w.type == LangError::IgnoredRateInOneShotDomain) {
      foundWarning = true;
      EXPECT_EQ(w.severity, LangError::Warning);
      ASSERT_GE(w.errorTokens.size(), 1u);
      EXPECT_EQ(w.errorTokens[0], "RatedSig");
    }
  }
  EXPECT_TRUE(foundWarning);
}

TEST(CodeValidator, InvalidZeroRateError) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  std::string code = R"(
_domainDefinition RateDom {
    rate: 100
}
signal ZeroRateSig { rate: 0 domain: RateDom }
)";
  std::string tempFile = "temp_validator_zerorate.stride";
  {
    std::ofstream out(tempFile);
    out << code;
  }
  ASTNode tree = AST::parseFile(tempFile.c_str());
  ASSERT_NE(tree, nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  EXPECT_FALSE(validator.isValid());

  bool foundError = false;
  for (const auto &err : validator.getErrors()) {
    if (err.type == LangError::UnresolvedRate) {
      foundError = true;
      break;
    }
  }
  EXPECT_TRUE(foundError);
}

TEST(CodeValidator, RateAdapterPropagatesPortRatesAndWarnsOnScalarToArray) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  std::string code = R"(
_domainDefinition AudioDom {
    rate: 48000
}
signal Out { rate: 48000 domain: AudioDom }
signal Arr[8] { domain: AudioDom }
1.0 >> RateAdapter(rate: 12000) >> Out;
0.0 >> Cos() >> Arr;
)";
  std::string tempFile = "temp_validator_rateadapter.stride";
  {
    std::ofstream out(tempFile);
    out << code;
  }
  ASTNode tree = AST::parseFile(tempFile.c_str());
  ASSERT_NE(tree, nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  EXPECT_TRUE(validator.isValid());

  for (const auto &err : validator.getErrors()) {
    ADD_FAILURE() << err.getErrorText();
  }
  bool foundScalarToArrayWarning = false;
  for (const auto &w : validator.getWarnings()) {
    if (w.type == LangError::ScalarCallToArray) {
      foundScalarToArrayWarning = true;
      EXPECT_EQ(w.severity, LangError::Warning);
    }
  }
  EXPECT_TRUE(foundScalarToArrayWarning);
  std::remove(tempFile.c_str());
}

// A typeClass of constants is an enumeration: a schema property typed with it
// accepts its members, qualified by their package when they come from one,
// and rejects any other constant.
TEST(CodeValidator, EnumConstantGroups) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  ASTNode tree =
      AST::parseFile(TESTS_SOURCE_DIR "validator/enum_constants.stride");
  ASSERT_TRUE(tree != nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  std::map<int, std::vector<std::string>> errorsByLine;
  for (auto &err : validator.getErrors()) {
    if (err.filename.find("enum_constants.stride") != std::string::npos) {
      errorsByLine[err.lineNumber].push_back(err.getErrorText());
    }
  }
  // _Mixed (line 13), BadGroup (29) and BadStray (32). _Colors, GoodLocal
  // and GoodQualified are valid.
  std::vector<int> lines;
  for (const auto &[line, texts] : errorsByLine) {
    lines.push_back(line);
  }
  EXPECT_EQ(lines, (std::vector<int>{13, 29, 32}));
  // The message names the constant that was given.
  ASSERT_EQ(errorsByLine[29].size(), 1u);
  EXPECT_NE(errorsByLine[29][0].find("PinOutput"), std::string::npos)
      << errorsByLine[29][0];
  for (const auto &[line, texts] : errorsByLine) {
    for (const auto &t : texts) {
      std::cout << "  validator: " << t << std::endl;
    }
  }
}

TEST(CodeValidator, IsrDomainRejectsMissingResources) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  std::string code = R"(
use AVR version 1.0 on ArduinoUno

signal TickCount { type: Int domain: AvrTimer1IsrDomain default: 0 }
TickCount + 1 >> TickCount;
0.01 >> Sleep(domain: AvrTimer1IsrDomain);
"isr" >> PrintLn();
)";
  std::string tempFile = "temp_validator_isr_missing_res.stride";
  {
    std::ofstream out(tempFile);
    out << code;
  }
  ASTNode tree = AST::parseFile(tempFile.c_str());
  ASSERT_NE(tree, nullptr);
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  EXPECT_FALSE(validator.isValid());

  bool foundTimer0Error = false;
  bool foundUsart0Error = false;
  for (const auto &err : validator.getErrors()) {
    if (err.type == LangError::MissingDomainResource &&
        err.errorTokens.size() >= 3) {
      if (err.errorTokens[0] == "Sleep" &&
          err.errorTokens[1] == "_AvrTimer0" &&
          err.errorTokens[2] == "AvrTimer1IsrDomain") {
        foundTimer0Error = true;
      }
      if (err.errorTokens[0] == "PrintLn" &&
          err.errorTokens[1] == "_AvrUsart0") {
        foundUsart0Error = true;
      }
    }
  }
  EXPECT_TRUE(foundTimer0Error);
  std::remove(tempFile.c_str());
}

TEST(CodeValidator, IsrDomainSelectsPoly5WithFastPolicyAndRejectsSinWithoutPolicy) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  // 1. AvrTimer1IsrDomain has qualityPolicy: QualityPolicy::Fast -> Sin()
  // resolves to Sin@Poly5 (pure polynomial, no _AvrMathLib required) -> valid!
  {
    std::string validCode = R"(
use AVR version 1.0 on ArduinoUno

signal Angle { type: Real domain: AvrTimer1IsrDomain default: 0.5 }
signal FastOut { type: Real domain: AvrTimer1IsrDomain default: 0.0 }
Angle >> Sin() >> FastOut;
)";
    std::string tempFile = "temp_validator_isr_fast_sin.stride";
    {
      std::ofstream out(tempFile);
      out << validCode;
    }
    ASTNode tree = AST::parseFile(tempFile.c_str());
    ASSERT_NE(tree, nullptr);

    CodeResolver resolver(tree, strideroot);
    resolver.process();

    CodeValidator validator(tree);
    EXPECT_TRUE(validator.isValid());
    for (const auto &err : validator.getErrors()) {
      ADD_FAILURE() << err.getErrorText();
    }
    std::remove(tempFile.c_str());
  }

  // 2. CustomIsrDomain without qualityPolicy: Sin() resolves to Sin -> __Sin
  // (requires _AvrMathLib) -> must NOT silently fall back, emits MissingDomainResource!
  {
    std::string invalidCode = R"(
use AVR version 1.0 on ArduinoUno

_domainDefinition CustomIsrDomain {
    framework: _AvrFramework
    parentDomain: RootDomain
    requiresResources: [_RootContext, _AvrProcessor, _AvrFlashMemory, _AvrSram, _AvrGpio]
    rate: 4000
    execution: ExecutionInterrupt
}

signal Angle { type: Real domain: CustomIsrDomain default: 0.5 }
signal Out { type: Real domain: CustomIsrDomain default: 0.0 }
Angle >> Sin() >> Out;
)";
    std::string tempFile = "temp_validator_isr_no_policy_sin.stride";
    {
      std::ofstream out(tempFile);
      out << invalidCode;
    }
    ASTNode tree = AST::parseFile(tempFile.c_str());
    ASSERT_NE(tree, nullptr);

    CodeResolver resolver(tree, strideroot);
    resolver.process();

    CodeValidator validator(tree);
    EXPECT_FALSE(validator.isValid());
    bool foundMathLibError = false;
    for (const auto &err : validator.getErrors()) {
      if (err.type == LangError::MissingDomainResource &&
          err.errorTokens.size() >= 3 &&
          err.errorTokens[0] == "Sin" &&
          err.errorTokens[1] == "_AvrMathLib" &&
          err.errorTokens[2] == "CustomIsrDomain") {
        foundMathLibError = true;
      }
    }
    EXPECT_TRUE(foundMathLibError);
    std::remove(tempFile.c_str());
  }
}

TEST(CodeValidator, ExclusiveResourceAndMaxInstancesEnforcement) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  std::string code = R"(
resource _ExclusiveHwTimer {
    exclusive: on
}

resource _LimitedDmaChannel {
    exclusive: off
    maxInstances: 1
}

_domainDefinition DomA {
    framework: _JitFramework
    requiresResources: [_ExclusiveHwTimer, _LimitedDmaChannel]
    rate: 1000
}

_domainDefinition DomB {
    framework: _JitFramework
    requiresResources: [_ExclusiveHwTimer, _LimitedDmaChannel]
    rate: 2000
}

signal A { domain: DomA default: 0.0 }
signal B { domain: DomB default: 0.0 }
A + 1.0 >> A;
B + 1.0 >> B;
)";
  std::string tempFile = "temp_validator_exclusive_res.stride";
  {
    std::ofstream out(tempFile);
    out << code;
  }
  ASTNode tree = AST::parseFile(tempFile.c_str());
  ASSERT_NE(tree, nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  EXPECT_FALSE(validator.isValid());

  bool foundExclusiveConflict = false;
  bool foundLimitExceeded = false;
  for (const auto &err : validator.getErrors()) {
    if (err.type == LangError::ExclusiveResourceConflict &&
        !err.errorTokens.empty() &&
        err.errorTokens[0] == "_ExclusiveHwTimer") {
      foundExclusiveConflict = true;
    }
    if (err.type == LangError::ResourceInstanceLimitExceeded &&
        !err.errorTokens.empty() &&
        err.errorTokens[0] == "_LimitedDmaChannel") {
      foundLimitExceeded = true;
    }
  }
  EXPECT_TRUE(foundExclusiveConflict);
  EXPECT_TRUE(foundLimitExceeded);
  std::remove(tempFile.c_str());
}

TEST(CodeValidator, PioInstructionOverflowEnforcement) {
  auto strideroot = ASTFunctions::getDefaultStrideRoot();
  std::ostringstream ss;
  ss << "_domainDefinition PioOverflowDom {\n"
     << "    framework: _JitFramework\n"
     << "    execution: ExecutionHardware\n"
     << "    requiresResources: [_Pio0_Sm0]\n"
     << "    rate: 1000000\n"
     << "}\n"
     << "signal Word { type: UInt32 domain: PioOverflowDom default: 0 }\n";
  for (int i = 0; i < 33; ++i) {
    ss << "Word >> PioShiftOut(bits: 1);\n";
  }

  std::string tempFile = "temp_validator_pio_overflow.stride";
  {
    std::ofstream out(tempFile);
    out << ss.str();
  }
  ASTNode tree = AST::parseFile(tempFile.c_str());
  ASSERT_NE(tree, nullptr);
  tree->addChild(
      std::make_shared<SystemNode>("JIT", 1, 0, __FILE__, __LINE__));
  ASTFunctions::preprocess(tree);

  CodeResolver resolver(tree, strideroot);
  resolver.process();

  CodeValidator validator(tree);
  EXPECT_FALSE(validator.isValid());

  bool foundOverflow = false;
  for (const auto &err : validator.getErrors()) {
    if (err.type == LangError::PioInstructionOverflow &&
        err.errorTokens.size() >= 3 &&
        err.errorTokens[0] == "PioOverflowDom" &&
        err.errorTokens[1] == "33" &&
        err.errorTokens[2] == "_Pio0") {
      foundOverflow = true;
    }
  }
  EXPECT_TRUE(foundOverflow);
  std::remove(tempFile.c_str());
}

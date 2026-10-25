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

#include "gtest/gtest.h"

#include "stride/stridegen/stridesystem.hpp"
#include "stride/utils/astfunctions.h"

using namespace strd;

TEST(StrideSystem, Initialize) {
    std::string strideRoot = ASTFunctions::getDefaultStrideRoot();
    
    // Test listAvailableSystems
    auto systems = StrideSystem::listAvailableSystems(strideRoot);
    EXPECT_GE(systems.size(), 0);

    // Non existent system should record error
    StrideSystem system(strideRoot, "NonExistentSystem", 1, 0, {});
    
    auto errors = system.getErrors();
    EXPECT_GT(errors.size(), 0);
}

TEST(StrideSystem, EmptySystemName) {
    std::string strideRoot = ASTFunctions::getDefaultStrideRoot();
    StrideSystem system(strideRoot, "", -1, -1, {});
    EXPECT_EQ(system.systemName(), "");
    EXPECT_EQ(system.getErrors().size(), 0);
}

TEST(StrideSystem, SafeOperationsOnEmpty) {
    std::string strideRoot = ASTFunctions::getDefaultStrideRoot();
    StrideSystem system(strideRoot, "", -1, -1, {});
    
    EXPECT_NO_THROW({
        auto imports = system.listAvailableImports();
        auto errors = system.getErrors();
        auto warnings = system.getWarnings();
        auto names = system.getFrameworkNames();
    });
}

// Generated code uses the frameworkName of each data type. An alias uses the
// one of the data type it stands for.
TEST(StrideSystem, FrameworkDataTypeNames) {
    std::string strideRoot = ASTFunctions::getDefaultStrideRoot();
    auto frameworkName = [](std::shared_ptr<DeclarationNode> decl) {
        if (!decl) {
            return std::string("<none>");
        }
        auto node = decl->getPropertyValue("frameworkName");
        if (!node || node->getNodeType() != AST::String) {
            return std::string("<none>");
        }
        return std::static_pointer_cast<ValueNode>(node)->getStringValue();
    };

    StrideSystem cpp(strideRoot, "TestSystem", 1, 0, {});
    EXPECT_EQ(frameworkName(cpp.getFrameworkDataType("TestFramework", "UInt8")),
              "uint8_t");
    EXPECT_EQ(
        frameworkName(cpp.getFrameworkDataType("TestFramework", "Float32")),
        "float");
    EXPECT_EQ(frameworkName(cpp.getFrameworkDataType("TestFramework", "Int")),
              "int32_t");

    StrideSystem stm32(strideRoot, "SaturnM7", 1, 0, {});
    EXPECT_EQ(frameworkName(stm32.getFrameworkDataType("STM32F7", "Real")),
              "float");

    StrideSystem jit(strideRoot, "JIT", 1, 0, {});
    EXPECT_EQ(frameworkName(jit.getFrameworkDataType("JIT", "Real")), "double");
    EXPECT_EQ(frameworkName(jit.getFrameworkDataType("JIT", "Int")), "int32_t");
    EXPECT_EQ(frameworkName(jit.getFrameworkDataType("JIT", "UInt")),
              "uint32_t");
}

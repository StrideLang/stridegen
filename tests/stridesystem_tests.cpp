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
#include "stride/stridegen/codeanalysis.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

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


#include "stride/stridegen/codevalidator.hpp"
#include "stride/parser/arraynode.h"
#include "stride/parser/streamnode.h"
#include "stride/parser/valuenode.h"

TEST(StrideSystem, ParameterizedConnectionDefaults) {
    std::string strideRoot = ASTFunctions::getDefaultStrideRoot();
    StrideSystem system(strideRoot, "", -1, -1, {});

    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "validator/conn_param_default.stride");
    ASSERT_TRUE(tree != nullptr);

    system.generateDomainConnections(tree);

    std::vector<std::shared_ptr<StreamNode>> streams;
    for (const auto &node : tree->getChildren()) {
        if (node->getNodeType() == AST::Stream) {
            streams.push_back(std::static_pointer_cast<StreamNode>(node));
        }
    }
    ASSERT_EQ(streams.size(), 2);
    EXPECT_EQ(streams[0]->getLeft()->toText(0, 0, false), "SigA");
    EXPECT_EQ(streams[0]->getRight()->toText(0, 0, false), "SerialOut[ 0 ]");
    EXPECT_EQ(streams[1]->getLeft()->toText(0, 0, false), "SerialIn[ 0 ]");
    EXPECT_EQ(streams[1]->getRight()->toText(0, 0, false), "SigB");
}

TEST(StrideSystem, ParameterizedConnectionOverrideInSystem) {
    std::string strideRoot = ASTFunctions::getDefaultStrideRoot();
    StrideSystem system(strideRoot, "", -1, -1, {});

    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "validator/conn_param_override.stride");
    ASSERT_TRUE(tree != nullptr);

    system.generateDomainConnections(tree);

    std::vector<std::shared_ptr<StreamNode>> streams;
    for (const auto &node : tree->getChildren()) {
        if (node->getNodeType() == AST::Stream) {
            streams.push_back(std::static_pointer_cast<StreamNode>(node));
        }
    }
    ASSERT_EQ(streams.size(), 2);
    EXPECT_EQ(streams[0]->getLeft()->toText(0, 0, false), "SigA");
    EXPECT_EQ(streams[0]->getRight()->toText(0, 0, false), "SerialOut[ 2 ]");
    EXPECT_EQ(streams[1]->getLeft()->toText(0, 0, false), "SerialIn[ 2 ]");
    EXPECT_EQ(streams[1]->getRight()->toText(0, 0, false), "SigB");
}

TEST(StrideSystem, ParameterizedConnectionCallSyntax) {
    std::string strideRoot = ASTFunctions::getDefaultStrideRoot();
    StrideSystem system(strideRoot, "", -1, -1, {});

    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "validator/conn_param_call_syntax.stride");
    ASSERT_TRUE(tree != nullptr);

    system.generateDomainConnections(tree);

    std::vector<std::shared_ptr<StreamNode>> streams;
    for (const auto &node : tree->getChildren()) {
        if (node->getNodeType() == AST::Stream) {
            streams.push_back(std::static_pointer_cast<StreamNode>(node));
        }
    }
    ASSERT_EQ(streams.size(), 2);
    EXPECT_EQ(streams[0]->getLeft()->toText(0, 0, false), "SigA");
    EXPECT_EQ(streams[0]->getRight()->toText(0, 0, false), "SerialOut[ 3 ]");
    EXPECT_EQ(streams[1]->getLeft()->toText(0, 0, false), "SerialIn[ 3 ]");
    EXPECT_EQ(streams[1]->getRight()->toText(0, 0, false), "SigB");
}

TEST(CodeValidator, ConnectionInvalidPropertyRejected) {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "validator/conn_invalid_prop.stride");
    ASSERT_TRUE(tree != nullptr);

    auto errors = CodeValidator::findInvalidConnectionProperties(tree);
    ASSERT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0].type, LangError::InvalidPort);
    ASSERT_GE(errors[0].errorTokens.size(), 2);
    EXPECT_EQ(errors[0].errorTokens[0], "TestSerialConn");
    EXPECT_EQ(errors[0].errorTokens[1], "badProperty");
}

TEST(CodeValidator, ConnectionNegativePortRejected) {
    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "validator/conn_negative_port.stride");
    ASSERT_TRUE(tree != nullptr);

    auto errors = CodeValidator::findInvalidConnectionProperties(tree);
    ASSERT_EQ(errors.size(), 1);
    EXPECT_EQ(errors[0].type, LangError::ArrayIndexOutOfBounds);
    ASSERT_GE(errors[0].errorTokens.size(), 2);
    EXPECT_EQ(errors[0].errorTokens[0], "port");
    EXPECT_EQ(errors[0].errorTokens[1], "-1");
}

TEST(StrideSystem, ConnectionSourceAndDestinationEntities) {
    std::string strideRoot = ASTFunctions::getDefaultStrideRoot();
    StrideSystem system(strideRoot, "", -1, -1, {});

    ASTNode tree = AST::parseFile(TESTS_SOURCE_DIR "validator/conn_entities.stride");
    ASSERT_TRUE(tree != nullptr);

    auto propErrors = CodeValidator::findInvalidConnectionProperties(tree);
    EXPECT_TRUE(propErrors.empty());

    system.generateDomainConnections(tree);

    std::vector<std::shared_ptr<StreamNode>> streams;
    std::shared_ptr<DeclarationNode> hostSenderDecl;
    std::shared_ptr<DeclarationNode> mcuReceiverDecl;
    std::shared_ptr<DeclarationNode> portNumberDecl;

    for (const auto &node : tree->getChildren()) {
        if (node->getNodeType() == AST::Stream) {
            streams.push_back(std::static_pointer_cast<StreamNode>(node));
        } else if (node->getNodeType() == AST::Declaration) {
            auto decl = std::static_pointer_cast<DeclarationNode>(node);
            if (decl->getName() == "NativeHostSender") {
                hostSenderDecl = decl;
            } else if (decl->getName() == "NativeMcuReceiver") {
                mcuReceiverDecl = decl;
            } else if (decl->getName() == "PortNumber") {
                portNumberDecl = decl;
            }
        }
    }

    ASSERT_EQ(streams.size(), 2);
    EXPECT_EQ(streams[0]->getLeft()->toText(0, 0, false), "SigA");
    EXPECT_EQ(streams[0]->getRight()->toText(0, 0, false), "NativeHostSender()");
    EXPECT_EQ(streams[1]->getLeft()->toText(0, 0, false), "NativeMcuReceiver()");
    EXPECT_EQ(streams[1]->getRight()->toText(0, 0, false), "SigB");

    ASSERT_TRUE(hostSenderDecl != nullptr);
    ASSERT_TRUE(mcuReceiverDecl != nullptr);
    ASSERT_TRUE(portNumberDecl != nullptr);

    auto portVal = portNumberDecl->getPropertyValue("value");
    ASSERT_TRUE(portVal != nullptr);
    EXPECT_EQ(portVal->toText(0, 0, false), "2");

    EXPECT_EQ(CodeAnalysis::getNodeDomainName(hostSenderDecl, {}, tree), "DomainA");
    EXPECT_EQ(CodeAnalysis::getNodeDomainName(mcuReceiverDecl, {}, tree), "DomainB");

    auto senderCallDecl = streams[0]->getRight()->getCompilerProperty("declaration");
    ASSERT_TRUE(senderCallDecl != nullptr);
    EXPECT_EQ(ASTQuery::getNodeName(senderCallDecl), "NativeHostSender");

    auto recvCallDecl = streams[1]->getLeft()->getCompilerProperty("declaration");
    ASSERT_TRUE(recvCallDecl != nullptr);
    EXPECT_EQ(ASTQuery::getNodeName(recvCallDecl), "NativeMcuReceiver");
}


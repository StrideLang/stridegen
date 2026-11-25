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

#include "stride/stridegen/coderesolver.hpp"

#include "passes/astnormalizer.hpp"
#include "passes/connectionanddependencyanalyzer.hpp"
#include "passes/domainresolver.hpp"
#include "passes/gpioconfigurator.hpp"
#include "passes/platforminitializer.hpp"
#include "passes/policyvariantresolver.hpp"
#include "passes/rateandtyperesolver.hpp"
#include "passes/resolvercontext.hpp"
#include "passes/streamexpander.hpp"
#include "passes/typecastresolver.hpp"
#include "stride/utils/astfunctions.h"
#include "stride/utils/astquery.h"

using namespace strd;

CodeResolver::CodeResolver(ASTNode tree, std::string striderootDir,
                           SystemConfiguration systemConfig,
                           FeatureConfig featureConfig)
    : m_ctx(std::make_unique<ResolverContext>(ResolverContext{
          std::move(tree), nullptr, std::move(systemConfig),
          std::move(striderootDir), PlatformPolicyManager(),
          std::move(featureConfig), {}, false, std::nullopt, {}})) {
  PlatformInitializer(*m_ctx).initializeSystemAndPolicies();
}

CodeResolver::~CodeResolver() = default;

const std::vector<LangError> &CodeResolver::getErrors() const {
  return m_ctx->errors;
}

std::shared_ptr<StrideSystem> CodeResolver::getSystem() const {
  return m_ctx->system;
}

const PlatformPolicyManager &CodeResolver::getPolicyManager() const {
  return m_ctx->policyManager;
}

PlatformPolicyManager &CodeResolver::getPolicyManager() {
  return m_ctx->policyManager;
}

const FeatureConfig &CodeResolver::getFeatureConfig() const {
  return m_ctx->featureConfig;
}

void CodeResolver::process() {
  PlatformInitializer platformInit(*m_ctx);
  DomainResolver domainResolver(*m_ctx);
  GpioConfigurator gpioConfigurator(*m_ctx, platformInit);
  PolicyVariantResolver policyResolver(*m_ctx);
  AstNormalizer astNormalizer(*m_ctx);
  StreamExpander streamExpander(*m_ctx);
  RateAndTypeResolver rateAndTypeResolver(*m_ctx);
  TypeCastResolver typeCastResolver(*m_ctx);
  ConnectionAndDependencyAnalyzer connAnalyzer(*m_ctx);

  platformInit.processSystem();
  domainResolver.resolveDomainAliases();
  gpioConfigurator.processGpio();
  platformInit.declarePlatformConstants();
  if (!m_ctx->errors.empty()) {
    return;
  }

  astNormalizer.desugar();
  for (const auto &decl : ASTQuery::getDomainIOEntityDeclarations(m_ctx->tree)) {
    m_ctx->tree->addChild(decl);
  }

  policyResolver.resolvePolicyVariants();

  ASTFunctions::insertRequiredObjects(m_ctx->tree,
                                      m_ctx->system->getImportTrees());
  platformInit.overlayFrameworkDataTypes();
  ASTFunctions::fillDefaultProperties(m_ctx->tree);
  ASTFunctions::processAnoymousDeclarations(m_ctx->tree);

  astNormalizer.declareModuleInternalEntities();
  m_ctx->system->injectResourceConfiguration(m_ctx->tree);

  streamExpander.expandValueRanges();
  astNormalizer.resolveSelfPropertyAccesses();
  streamExpander.expandIndexedStreams();
  streamExpander.expandParallel();
  streamExpander.resolveStreamSymbols();

  // Resolve objects and properties introduced by stream/parallel expansion
  ASTFunctions::insertRequiredObjects(m_ctx->tree,
                                      m_ctx->system->getImportTrees());
  platformInit.overlayFrameworkDataTypes();
  ASTFunctions::fillDefaultProperties(m_ctx->tree);
  ASTFunctions::processAnoymousDeclarations(m_ctx->tree);
  streamExpander.expandValueRanges();

  if (m_ctx->systemConfig.testing) {
    platformInit.applyTestingOverrides();
    ASTFunctions::insertRequiredObjects(m_ctx->tree,
                                        m_ctx->system->getImportTrees());
    ASTFunctions::fillDefaultProperties(m_ctx->tree);
    ASTFunctions::processAnoymousDeclarations(m_ctx->tree);
  }

  m_ctx->system->injectResourceConfiguration(m_ctx->tree);

  astNormalizer.resolveConstants();
  astNormalizer.resolveSelfPropertyAccesses();
  astNormalizer.hoistDeclarationChildren();

  domainResolver.processDomains();
  connAnalyzer.storeDeclarations();

  connAnalyzer.analyzeConnections();
  connAnalyzer.analyzeParents();

  rateAndTypeResolver.setPortEntityAttributes({}, m_ctx->tree);
  rateAndTypeResolver.resolveRatesAndTypes({}, m_ctx->tree);
  typeCastResolver.resolveTypeCasting();
  rateAndTypeResolver.resolveDeclarationDataTypes();

  connAnalyzer.analyzePersistence();
  connAnalyzer.analyzeStreamDependencies();
}

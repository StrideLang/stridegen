/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#ifndef PLATFORMINITIALIZER_HPP
#define PLATFORMINITIALIZER_HPP

#include "resolvercontext.hpp"

namespace strd {

class PlatformInitializer {
public:
  explicit PlatformInitializer(ResolverContext &ctx) : m_ctx(ctx) {}

  void initializeSystemAndPolicies();
  void processSystem();
  void declarePlatformConstants();
  void overlayFrameworkDataTypes();
  void applyTestingOverrides();

  const std::optional<ResolvedHardware> &ensureHardwareResolved();

private:
  ResolverContext &m_ctx;
};

} // namespace strd

#endif // PLATFORMINITIALIZER_HPP

/*
    Stride is licensed under the terms of the GNU Affero General Public License
   version 3.

    Copyright (C) 2022-2026 Andres Cabrera
    All rights reserved.
*/

#ifndef GPIOCONFIGURATOR_HPP
#define GPIOCONFIGURATOR_HPP

#include "platforminitializer.hpp"
#include "resolvercontext.hpp"

namespace strd {

class GpioConfigurator {
public:
  GpioConfigurator(ResolverContext &ctx, PlatformInitializer &platformInit)
      : m_ctx(ctx), m_platformInit(platformInit) {}

  void processGpio();

private:
  ResolverContext &m_ctx;
  PlatformInitializer &m_platformInit;
};

} // namespace strd

#endif // GPIOCONFIGURATOR_HPP

# StrideGen

**StrideGen** is the semantic analysis, AST resolution, and code generation orchestration library for the [Stride](https://github.com/andres-cabrera/stride) programming language. It bridges the frontend AST parsed by `StrideParser` with backend execution targets and compiler backends such as `StrideJIT`.

---

## Overview & Architecture

Stride is a multi-domain, stream-oriented audio and DSP processing language. Stride programs define streams, reactions, modules, domains, and platform configurations that must be statically validated, typed, expanded, and lowered before machine code or intermediate representations can be generated.

`stridegen` provides the core pipeline for:
- **Lexical Scoping & Namespace Resolution**: Managing hierarchical block imports, package root files, and domain encapsulation.
- **AST Preprocessing & Multi-Pass Lowering**: Desugaring syntactic constructs, expanding parallel streams/bundles, propagating constants, resolving symbol references, and managing variable persistence.
- **Type & Rate Inference**: Deduce signal rates, data types, port properties, and typecasting rules across stream connections.
- **Semantic Validation**: Enforcing semantic invariants, array dimensions, indexing bounds, and platform constraints.
- **System & Framework Orchestration**: Managing system definitions, runtime frameworks (e.g. JIT, C++), target platforms (e.g. Desktop, Bela, STM32), and builder plugins.

```
       +-----------------------+
       |     .stride Source    |
       +-----------+-----------+
                   |
                   v
       +-----------------------+
       |     StrideParser      |  (Tokenization & AST Construction)
       +-----------+-----------+
                   |  Raw AST
                   v
       +-----------------------+
       |       StrideGen       |  (Semantic Analysis, Desugaring,
       |  - CodeResolver       |   Type & Rate Inference, Validation,
       |  - CodeValidator      |   System/Platform Coordination)
       |  - CodeAnalysis       |
       +-----------+-----------+
                   |  Resolved & Validated AST
                   v
       +-----------------------+
       |   Backend / Builder   |  (e.g., StrideJIT / LLVM IR, C++, HDL)
       +-----------------------+
```

---

## Key Modules & Classes

All classes in `stridegen` reside in the `strd` namespace:

### 1. `CodeResolver` (`include/stride/stridegen/coderesolver.hpp`)
Drives the multi-pass transformation pipeline on the AST:
- Resolves imports and system platforms.
- Expands parallel blocks and indexed stream bundles.
- Injects platform-specific declarations and domain I/O entities.
- Infers port properties, connection rates, and data types across streams.
- Performs typecasting resolution (tagging nodes with the `typecast` compiler property).
- Analyzes variable persistence (e.g., determining whether module variables require persistent state).

### 2. `CodeValidator` (`include/stride/stridegen/codevalidator.hpp`)
Performs static semantic verification on the AST:
- Validates signal rates and types across stream connections.
- Checks array indexing and size specifications.
- Detects shadowed symbols and illegal scope accesses.
- Verifies platform module constraints and hardware port declarations.

### 3. `CodeAnalysis` (`include/stride/stridegen/codeanalysis.hpp`)
Static analysis utilities for querying and reasoning over the AST:
- Queries active domains, frameworks, and declarations.
- Determines variable roles (local stack alloca, PHI, persistent state, or external argument).
- Resolves node output data types and container properties.
- Analyzes signal access boundaries (single-domain vs. multi-domain reads and writes).

### 4. `StrideSystem` & `SystemConfiguration` (`include/stride/stridegen/stridesystem.hpp`)
Orchestrates multi-target architectures:
- Resolves system specifications (`use System version X.Y`).
- Discovers available systems and platforms under `$STRIDEROOT`.
- Injects hardware/OS platform drivers and platform modules (`platformModule`).
- Acts as a factory for target platform builders (`Builder`).

### 5. `Builder` (`include/stride/stridegen/builder.hpp`)
The abstract base interface for target platform backends and plugins. Concrete builders implement:
- `generateCode(ASTNode tree)`: Lowers the resolved AST to target source or IR.
- `build(...)`: Compiles or links target binaries.
- `deploy()`: Deploys the artifacts to the target environment or device.
- `run(...)`: Launches or executes the process.

### 6. `LanguageSyntax` & `CodeEntities` (`include/stride/stridegen/languagesyntax.hpp`, `codeentities.hpp`)
Internal models for code generation, handling signal declarations, instances, and access classifications (`ACCESS_SDR`, `ACCESS_SDW`, `ACCESS_MDR`, `ACCESS_MDW`, cross-framework boundaries).

---

## Detailed Specifications

More in-depth architecture notes and phase specifications can be found under the `doc/` directory:
- [doc/namespaces.md](file:///C:/Users/Andres/source/repos/boardgame/libgame/external/stridegen/doc/namespaces.md): Formal semantics for namespaces, lexical scopes, block imports, barrel files, and platform/framework resolution.
- [doc/preprocess.md](file:///C:/Users/Andres/source/repos/boardgame/libgame/external/stridegen/doc/preprocess.md): Detailed multi-pass table of compiler properties read and written by `CodeResolver::process()`.

---

## Directory Structure

```
stridegen/
├── CMakeLists.txt              # CMake build configuration for StrideGen
├── License.txt                 # GNU AGPLv3 License
├── README.md                   # This file
├── doc/                        # Architecture & pipeline documentation
│   ├── namespaces.md           # Scoping, imports, and system resolution specs
│   └── preprocess.md           # Preprocessing pass sequence & compiler properties
├── include/stride/stridegen/   # Public API headers
│   ├── builder.hpp             # Abstract backend Builder interface
│   ├── codeanalysis.hpp        # AST static analysis utilities
│   ├── codeentities.hpp        # Internal representations for declarations & instances
│   ├── coderesolver.hpp        # Multi-pass AST resolver and desugarer
│   ├── codevalidator.hpp       # Static semantics and rate/type validator
│   ├── languagesyntax.hpp      # Code emission helpers and type string syntax
│   ├── pythonproject.h         # Python build integration helpers
│   ├── strideframework.hpp     # Framework abstractions
│   ├── stridesystem.hpp        # System and platform coordination
│   ├── systemconfiguration.hpp # Configuration settings for StrideSystem
│   └── toolmanager.hpp         # External tool discovery and templates
├── src/                        # Implementation files
│   ├── builder_interface.hpp
│   ├── codeanalysis.cpp
│   ├── coderesolver.cpp
│   ├── codevalidator.cpp
│   ├── languagesyntax.cpp
│   ├── pythonproject.cpp
│   ├── strideframework.cpp
│   ├── stridesystem.cpp
│   ├── systemconfiguration.cpp
│   └── toolmanager.cpp
└── tests/                      # Unit and integration test suite
    ├── CMakeLists.txt          # Test build target (stridegen_tests)
    ├── analysis.cpp
    ├── codeanalysis_tests.cpp
    ├── codevalidator_tests.cpp
    ├── compiler_properties.cpp
    ├── main.cpp
    ├── resolver.cpp
    ├── stridesystem_tests.cpp
    └── data/                   # Test fixtures (.stride files)
```

---

## Dependencies & Requirements

- **C++ Standard**: C++20 (`set_property(TARGET StrideGen PROPERTY CXX_STANDARD 20)`).
- **Libraries**:
  - `StrideParser`: Lexer and AST data structures.
  - `StrideUtils`: AST traversal and helper routines.
  - `GoogleTest`: Required for running the test suite (`stridegen_tests`).

---

## Building and Testing

`StrideGen` is built using CMake as part of the overall Stride project or standalone:

```powershell
# Configure build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug

# Build
cmake --build build --config Debug

# Run tests
ctest --test-dir build --output-on-failure -R "stridegen"
```

---

## License

Stride is licensed under the terms of the **GNU Affero General Public License version 3** (AGPLv3).

It is based on code previously released under the **Modified BSD License** (3-Clause BSD), Copyright © 2017 The Regents of the University of California, developed by Andres Cabrera and Joseph Tilbian.

See [License.txt](file:///C:/Users/Andres/source/repos/boardgame/libgame/external/stridegen/License.txt) for the complete license terms and notices.

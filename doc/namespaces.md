# Stride Namespace & Scope Specification

This document defines the formal semantics for namespaces, lexical scopes, block-based imports, package root files, and system/framework/platform resolution in the Stride language.

---

## 1. Lexical Scoping & Domain-Gated Variable Access

### 1.1 Scope Hierarchy & Lookup Order
When resolving variable or signal identifiers, the compiler checks scopes in the following strict order:
1. **Local Block Scope (`scopeStack.back()`)**: Temporary variables, loop induction/PHI allocas, reaction locals.
2. **Immediate Parent Scope**: The container directly enclosing the block.
3. **Enclosing Declaration Scope (Domain / Module)**:
   - **Root-level DomainMembers (Reactions & Loops)**: Have direct read and write access to signals, constants, switches, and IO blocks declared at that domain's scope.
   - **Domain Gate Invariant for Nested Blocks**: When a reaction or loop is nested inside another declaration (such as a module or another block), it inherits variable access **only from its immediate parent scope**. Access across scope boundaries is gated to the **single domain** (even if it is an undefined port property domain) of the immediate parent scope.
   - **Module Boundary**: Modules are strictly encapsulated units of computation. A `module` (and any reactions/loops defined inside it) **cannot** implicitly access external domain variables. All interactions must occur via declared input/output ports or port properties.
4. **Shadowing**: Local declarations strictly shadow parent or domain variables of the same name.

---

## 2. Filesystem Conventions & Block-Based Imports

### 2.1 Capitalization & Block Identifier Invariant
- **Rule**: All importable directories and `.stride` source files **must start with a Capital Letter** (e.g. `Filters/`, `Generators/`, `Biquad.stride`).
- Import statements accept native **Block Identifiers** (never quoted string literals):
  ```stride
  import Filters
  import Filters::Biquad
  import Filters as Flt
  ```

### 2.2 Conditional Imports with `@with`
Import statements can be conditionally included based on feature toggles using `@with(...)`, either as single statements or grouped in blocks:
```stride
@with(Audio)
import Filters

@with(!Audio)
import DummyAudio

@with(Audio && Advanced) {
  import Filters::Biquad
  import Generators as Gen
}
```
When a feature condition evaluates to `false`, guarded import statements are pruned by `FeatureResolver` during preprocessing, preventing inactive dependencies from being loaded or resolved.

### 2.3 Include Path Search Order
When an import statement is resolved, the compiler searches include roots in the following priority order:
1. **Current Directory**: Sibling files in the directory of the compiling file.
2. **Project Include Directories**: Configured project source directories and `-I` include roots.
3. **Active System Directory**: `$STRIDEROOT/systems/<System>/<Major>.<Minor>/`.
4. **Standard Library**: `$STRIDEROOT/library/<Major>.<Minor>/`.

---

## 3. Package Root (Barrel File) Model

When a directory `Name/` and a file `Name.stride` share the same name at the same filesystem level:

```
src/
├── Filters.stride          # Package Root (Barrel File)
└── Filters/                # Sub-namespace Directory
    ├── Biquad.stride       # Sub-module Filters::Biquad
    └── Lowpass.stride      # Sub-module Filters::Lowpass
```

### 3.1 Resolution Semantics
1. **`Name.stride` as Package Root**: Contains top-level exports, shared types, default module definitions, and package constants for the `Name` namespace.
2. **`Name/` as Sub-Namespace**: Contains nested sub-modules accessible via `Name::SubModule`.
3. **Import Statements**:
   - `import Filters`: Parses `Filters.stride` into scope and registers `Filters/` for on-demand sub-module resolution.
   - `import Filters::Biquad`: Resolves directly to `Filters/Biquad.stride`.
   - `import Filters as Flt`: Aliases both the root file and child modules under `Flt::` (e.g., `Flt::Biquad`).
4. **Collision Precedence**: If both `Filters.stride` and `Filters/Biquad.stride` declare a symbol named `Biquad`, the explicit file `Filters/Biquad.stride` takes precedence for `Filters::Biquad`.

---

## 4. Systems, Frameworks, and Platforms

| Concept | Role | Example | Location |
| :--- | :--- | :--- | :--- |
| **System** | Top-level architecture coordinating multiple frameworks and defining platform targets. | `AudioDesktop` | `$STRIDEROOT/systems/<System>/<Ver>/System.stride` |
| **Framework** | Execution runtime and codegen model for a domain. | `JIT`, `CPlusPlus`, `VHDL` | Declared in `System.stride` |
| **Platform** | Hardware / OS abstraction layer providing drivers and `platformModule` blocks. | `Desktop`, `Bela`, `Stm32` | `$STRIDEROOT/systems/<System>/<Ver>/<Platform>/` |

### 4.1 `use` Directive
```stride
use AudioDesktop version 1.0   # Full System specification
use JIT version 1.0            # Single Framework shortcut
```
- The `use` statement sets the active system configuration and loads platform modules into the compiler registry.

### 4.2 Platform & Framework Namespacing
1. **Domain Framework Matching**: Unqualified platform blocks (`platformModule`, `platformBlock`) resolve automatically if their `framework` compiler property matches the enclosing domain's assigned framework.
2. **Explicit Qualification**: Multi-platform conflicts across systems are disambiguated via platform namespace prefixes (e.g. `Bela::AnalogIn`, `Desktop::AudioOut`).

---

## 5. Package Declarations & Namespace Derivation

### 5.1 Explicit vs. Path-Derived Namespaces
1. **Explicit `package` Statement**:
   ```stride
   package Audio::Filters
   ```
   Explicitly sets the logical namespace for all declarations in the file, overriding folder-derived namespaces.
2. **Path-Based Derivation (Fallback)**:
   If no `package` statement is present, files in capitalized subdirectories automatically derive their namespace from their relative folder path (e.g., `Audio/Filters/Biquad.stride` $\to$ `Audio::Filters::Biquad`).

# AGENTS.md: RMA Coding and Decision Rules

## Task

Build RMA into a maintainable implementation of a defined ECMAScript edition. Prioritize correct, coherent source code under `src/` over feature-count claims, demo-only behavior, or changes made merely to satisfy tests.

Parsing JavaScript syntax or implementing a list of built-in method names does not establish ECMAScript compliance. Correct observable behavior, including edge cases and interactions between features, is the goal.

## Product decision and specification scope

- Initial conformance baseline: **ECMAScript 2025 (ECMA-262, 16th edition)**.
- Do not silently switch the target edition or implement Stage 1–3 proposals as standard features.
- A newer edition requires an explicit, documented scope update. Keep edition-specific features identifiable.
- The initial target is ECMA-262. ECMA-402 internationalization support is a separate scope decision; do not claim it is included by default.
- Aim for the standard language and standard built-ins within the chosen scope. Arrays, strings, and objects are important families, not the complete standard library.

## Required workflow before changing code

1. Inspect relevant files in `src/`, their callers, and existing runtime abstractions before editing.
2. Identify the semantic responsibility of the change and the existing abstraction that should own it.
3. Make the smallest coherent implementation change. Do not perform unrelated cleanup or a broad rewrite.
4. Build the actual project with its existing build workflow.
5. Run relevant executable behavior and regression checks where available. Compare observable behavior with a trusted ECMAScript implementation when practical.
6. Review the final diff for unrelated edits, duplicate logic, misleading comments, and accidental API or namespace changes.
7. Report precisely what changed, which commands were actually run, their results, and what remains unsupported.

If a required build or check cannot run, state that clearly. Never claim a build, test, example, or conformance check passed unless it was actually executed and observed.

## Architecture boundaries

Keep these responsibilities separate:

- **Lexer/parser:** tokenize and parse source; do not implement runtime behavior here.
- **AST:** represent syntax structurally, with source locations where applicable.
- **Runtime values and objects:** represent ECMAScript values, objects, arrays, property keys, and descriptors.
- **References and environments:** represent binding/property reads and writes, scope, and assignment semantics.
- **Semantic operations:** centralize conversions, equality, property-key conversion, call semantics, and other shared rules.
- **Built-ins:** implement standard functions and methods on top of runtime interfaces. Do not hardcode each method name into the parser or compiler evaluator.
- **Execution/backend:** execute or compile programs independently of semantic definitions. Do not make language behavior depend on precomputing console output during compilation.

Reuse existing `rma::syntax` and `rma::compiler` types and established engine abstractions where appropriate. Do not invent parallel value/object/environment models without demonstrating why the existing model cannot serve the requirement.

## Built-in implementation rules

- Implement built-in families on shared primitives, not isolated special cases.
- A method must have correct receiver handling, argument processing, return value, side effects, property attributes, coercions, and errors for the supported specification scope.
- Do not mark a whole family as supported because a few method names work.
- Array iteration methods such as `map`, `filter`, `reduce`, `some`, and `every` depend on callable functions and correct callback invocation. Do not fake them with hardcoded callback patterns.
- Object methods must use the same property, descriptor, prototype, and key-conversion machinery as ordinary source code.
- String methods must respect the runtime string representation and the target edition's specified indexing and Unicode behavior.
- Avoid implementing semantics twice. If the same conversion or property rule is needed in multiple methods, put it in the shared semantic layer.

## Incremental development policy

- Work in small, reviewable vertical slices. Each slice should improve real behavior end-to-end.
- Prefer completing a foundational abstraction and a few dependent features over adding many disconnected method names.
- Do not rewrite stable subsystems just to match a preferred style.
- Do not add speculative frameworks, plugin systems, code-generation layers, or dependencies without a concrete requirement.
- Do not create oversized source files. Split code by stable responsibility when doing so reduces coupling; do not split files mechanically just to hit a line count.
- Keep existing public interfaces stable unless the task requires a change. If an interface changes, update all real callers in the same change.
- Do not rename project namespaces or core concepts casually. Use the project's actual namespaces; `rma::syntax` exists, `edon::ecma` is not the project's namespace.
- Do not change the Makefile, CMake configuration, release packaging, or unrelated CLI behavior unless required for the implementation.
- Do not delete user work, use destructive Git commands, reset branches, or overwrite unrelated files.
- Do not make a test-only change and present it as feature progress. Tests are evidence and regression protection; implementation in `src/` must deliver the behavior.

## Specification and conformance discipline

- Before implementing a language feature, identify its relevant ECMA-262 section or algorithm and the prerequisites it depends on.
- Prefer the official specification at https://tc39.es/ecma262/ and Test262 at https://github.com/tc39/test262/.
- Use focused tests for new behavior and regression checks for affected existing behavior. Where practical, compare results and errors with a trusted implementation such as Node.js.
- Treat Test262 as a conformance aid, not proof of total compliance. Record unsupported features, intentionally excluded features, harness limitations, and known failures.
- Maintain a feature inventory with explicit states: `unsupported`, `partial`, `implemented`, or `verified against tests`. “Implemented” does not mean fully compliant.
- Do not claim an overall compliance percentage unless the metric, edition, test selection, and denominator are defined.
- Do not silently alter expected behavior just to make an existing test pass. Check the specification and reference behavior first.

## Definition of done

A feature change is complete only when:

- behavior is implemented in the correct architectural layer;
- shared semantics are reused rather than duplicated;
- relevant existing behavior is preserved or intentional incompatibilities are documented;
- the project builds, or the exact build failure is reported;
- focused behavior has been exercised, or the inability to do so is stated;
- feature inventory/documentation is updated when support status changes;
- the final report lists actual files changed, actual verification performed, limitations, and the next dependency or task.

## Autonomous-agent stopping rules

Stop and report before proceeding if:

- the change requires choosing a new ECMAScript edition or expanding product scope;
- the existing architecture cannot support the feature without a broad rewrite;
- a proposed change would remove existing functionality or alter public behavior outside the task;
- the specification and reference implementation disagree in a way not yet understood;
- required dependencies, build changes, or generated artifacts would introduce material maintenance or distribution costs.

Do not hide uncertainty by guessing. Explain the blocker and recommend the smallest decision needed to continue.

## Recommended implementation order

1. Stabilize values, references, environments, object properties, and shared semantic operations.
2. Establish consistent function calls, `this`, parameters, returns, exceptions, and scope.
3. Complete ordinary object and array behavior, including descriptors and prototype interactions required by the target edition.
4. Implement built-in families incrementally: Object, Array, String, then the remaining standard built-ins according to dependencies.
5. Implement remaining language constructs and execution semantics needed by the chosen edition, including control flow, functions/closures, modules, and other in-scope features.
6. Run progressively broader Test262 subsets, track failures by feature, and fix semantic root causes rather than individual symptoms.
7. Reassess the next edition only after the current target has a documented coverage baseline.

The order may be adjusted when source inspection shows a dependency, but changes must preserve these boundaries and explain the reason for deviation.


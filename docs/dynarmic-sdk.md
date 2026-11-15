# Pinned Dynarmic SDK

The optional native ARM9 implementation uses Dynarmic commit
`a41c380246d3d9f9874f0f792d234dc0cc17c180` with the patches below. Start
from a fresh, isolated checkout of that commit. Keep builds outside the SDK
source directory and leave any SDK used by another build untouched.

Apply these existing patches in order:

1. [`dynarmic-readonly-pages.patch`](../runner/cmake/dynarmic-readonly-pages.patch)
2. [`dynarmic-cold-register-allocation.patch`](../runner/cmake/dynarmic-cold-register-allocation.patch)
3. [`dynarmic-prepared-blocks.patch`](../runner/cmake/dynarmic-prepared-blocks.patch)
4. [`dynarmic-observation-callbacks.patch`](../runner/cmake/dynarmic-observation-callbacks.patch)

For example, with absolute paths to the framework and isolated SDK:

```powershell
$framework = (Resolve-Path 'path/to/ndsrecomp').Path
$sdk = (Resolve-Path 'path/to/isolated-dynarmic').Path
git -C $sdk checkout --detach a41c380246d3d9f9874f0f792d234dc0cc17c180
foreach ($name in @('readonly-pages', 'cold-register-allocation',
                    'prepared-blocks', 'observation-callbacks')) {
    $patch = Join-Path $framework "runner/cmake/dynarmic-$name.patch"
    git -C $sdk apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw "SDK patch check failed: $name" }
    git -C $sdk apply $patch
    if ($LASTEXITCODE -ne 0) { throw "SDK patch failed: $name" }
}
git -C $sdk diff --check
```

The first three patches supply the common native SDK requirements. The fourth
adds guarded IRQ-mask writes and live MMIO callback timing for
`NDS_ARM9_OBSERVATION_SERVICE=ON`; its SDK features remain disabled unless
requested by the consumer. The engine retains portable execution when the
optional Windows/x64 native host is unavailable.

This observation-service profile uses `NDS_NATIVE_PUBLISHED_STORES=OFF`.
[`dynarmic-published-stores.patch`](../runner/cmake/dynarmic-published-stores.patch)
is a separate feature described in
[`native-published-stores-20261028.md`](native-published-stores-20261028.md);
it is not part of this SDK recipe.

The ordered recipe was applied to a clean base and compared with the qualified
SDK. Every library, header, backend, frontend, allocator and test source file
matches. The recipe keeps upstream `CMakeLists.txt`: the qualification workspace
also contained an unrelated research executable with local source paths, which
is unnecessary for either `dynarmic` or `dynarmic_tests` and must not be copied.
No additional functional SDK patch is required.

Before creating build outputs, the exact patched source tree can be checked
without making a commit:

```powershell
git -C $sdk add --update
git -C $sdk write-tree
# Expected: f85ff954eb4f794a15bbcfa12cd02b781c47cba1
```

Point the title's existing supported runner configuration at this source using
`NDS_ARM9_DYNARMIC_ROOT=<absolute-sdk-path>`. For the observation-service build,
also select `NDS_ARM9_OBSERVATION_SERVICE=ON` and leave published stores OFF.
Rebuild the SDK and its consumers together: the callback/configuration ABI must
match, so an older SDK archive cannot be mixed with these headers. Current
CMake versions may require `CMAKE_POLICY_VERSION_MINIMUM=3.5` for the bundled
dependencies. The runner configures the SDK as its A32-only subproject; a
standalone SDK build can use `DYNARMIC_FRONTENDS=A32`, `DYNARMIC_TESTS=ON` and
the existing `dynarmic_tests` filter `[nds-observation],arm: svc` for the focused
callback/control checks.

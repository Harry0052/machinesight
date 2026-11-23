# Contributing

MachineSight is a small C17 command-line program. Changes should keep the demo runnable with CMake and a C compiler, without a network fetch or a third-party library.

1. Branch from the current main line.
2. Keep domain comparison code separate from file loading and the command-line wrapper.
3. Update `fixtures/demo/ORACLE.md` and the expected report when a fixture result changes, and record the new checksums in `fixtures/MANIFEST.md`.
4. Build and test:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

5. Run `bash scripts/compare-compilers.sh` when both gcc and clang are installed.
6. For a change that touches parsing or arithmetic, also configure with `-DMACHINESIGHT_SANITIZE=ON` and rerun the tests.

Use a conventional commit subject (`feat`, `fix`, `docs`, `test`, `ci`, `chore`, or `refactor`). Do not commit build directories, credentials, or field logs. See `docs/data-terms.md`.

This program must not gain a path that starts, stops, or declares equipment safe to operate.

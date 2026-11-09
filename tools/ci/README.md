# InputNextAdapters CI tools

`inputnext-adapters-ci` prepares pinned tools, verifies compilers, selects product checks and runs
CMake/CTest. Reports, step logs and JUnit results use the output root's `reports/`, `logs/` and
`junit/` directories; native build trees use `builds/`. See [CI and releases](../../docs/ci.md)
for workflow scope and product packages for packaging commands.

`Invoke-CI.ps1` enters the verified Visual Studio developer environment, checks that the selected
MSVC executable matches the compiler report, and forwards the supplied argument array to the CLI.
Call it through PowerShell 7.6 `-CommandWithArgs` when dynamic arguments must cross the PowerShell
process boundary.

The bounded process runner, verified archive download/cache, archive inspection and extraction,
toolchain reports, compiler discovery, and compiler bootstrap code are adapted from the
[InputNext commit `9943f6ca223255d47bf409abc6ecf0e5e1af9761`](https://github.com/osyka-yuri/InputNext/commit/9943f6ca223255d47bf409abc6ecf0e5e1af9761)
under the repository's MIT license. InputNextAdapters implements its own product plans and package
qualification.

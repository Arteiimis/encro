## 1. Canonical-form wording

- [ ] 1.1 Update the existing help-routing assertions in `tests/cmd_completion_command_tests.cpp` (they pin the current `encro completion <powershell|bash>` substring) and add coverage that `encro completion --help` shows the flag-first synopsis `encro completion [--install | --uninstall] <powershell|bash>` and a concrete example line, then update `kCompletionUsageLines` in `src/cmd/cmd.cpp` and verify `xmake test-report --tag="[completion]"` passes
- [ ] 1.2 Add a failing test in `tests/cmd_completion_command_tests.cpp` asserting the missing-shell error shows the flag-first form, then update the error string in `src/cmd/completion_command.cpp` and verify it passes
- [ ] 1.3 Add a test asserting `encro completion powershell --install` and `encro completion --install powershell` both route to install (sandboxed env), guarding the both-orders-accepted guarantee

## 2. PowerShell dual-profile install

- [ ] 2.1 Add failing tests in `tests/cmd_completion_install_tests.cpp`: first-time install (no profiles) creates both the WindowsPowerShell and PowerShell profile files with one guarded entry each; pre-existing profile content is preserved. Then drop the `foundProfile` branch in `installPowerShell` (`src/cmd/completion_install.cpp`) to wire both unconditionally and verify the tests pass (filesystem state and exit code; the wired-files print rides the existing install-output posture)

## 3. Uninstall empty-file cleanup

- [ ] 3.1 Add failing tests: uninstall deletes a profile/startup file that is empty after block removal (whether install created it or it pre-existed empty) and keeps files with other content, then apply the empty-after-unwire deletion rule to `uninstallPowerShell` and `uninstallBash` and verify the tests pass

## 4. Verification

- [ ] 4.1 Run `xmake test-parallel` and confirm no regressions across unit + e2e suites

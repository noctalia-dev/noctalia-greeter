<!-- A bot checks this description and converts the pull request back to a draft if
     required structure is missing. It never closes the pull request.

     Required: the Summary, Motivation, Type of Change, Testing, and Checklist
     headings, and the Checklist wording below. Before marking a pull request ready for
     review, select at least one change type and check every Checklist item.

     Everything else may be deleted, including these guidance comments and any of the
     Related Issue, Manual Coverage, Screenshots / Videos, and Additional Notes sections.
     In Type of Change, keep only the lines that apply.

     An explanation does not replace a required check. If a required statement is not
     true yet, keep the pull request as Draft. -->

## Summary

<!-- What changed and why? -->

## Motivation

<!-- What problem does this solve? -->

## Type of Change

<!-- Mark all that apply. -->

- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Refactoring
- [ ] Build / packaging

## Related Issue

<!-- Example: Closes #123 -->

## Testing

<!-- List commands run and any manual testing. If not run, say why. -->

## Manual Coverage

<!-- Mark what applies to this PR. -->

- [ ] Tested under greetd (real login flow)
- [ ] Tested with `just run` / `just run-local` (dev compositor)
- [ ] Tested with multiple monitors
- [ ] Tested appearance sync from Noctalia Shell (`Sync Now`)
- [ ] Tested on NixOS (`programs.noctalia-greeter`)
- [ ] Tested with a pinned `[output].name`
- [ ] Tested with custom `[output].layout` / `[output].transforms`

## Screenshots / Videos

<!-- Include screenshots or videos for UI, visual, animation, or layout changes. -->

## Checklist

<!-- Before marking the pull request ready for review, check every item below. -->

- [ ] This PR is ready for review, or it is marked as Draft.
- [ ] I read and followed the relevant guidance in `CONTRIBUTING.md` and `README.md`.
- [ ] I ran `just format` with clang-format v22+ installed, or this PR has no code changes.
- [ ] I ran the relevant build or test commands, or explained why they were not run.
- [ ] I self-reviewed the changes.
- [ ] I checked for new warnings or errors.
- [ ] I updated user-facing documentation in [`docs/user/`](docs/user/) when this PR changes user-visible behavior or configuration, or this PR does not require documentation changes.
- [ ] I used the existing canonical names for config keys, paths, and identifiers.

## Additional Notes

<!-- Add follow-up notes, reviewer context, or known limitations. -->

# Upstream and licence notes

## Canonical source

- Repository: `https://github.com/sjnewbury/beebem-1.git`
- Role: semi-official Linux/SDL BeebEm source.
- Pinned submodule commit: `aa2ffc1fd7fe87f2de9114caa13779d280e76a75`
- Commit date: 2020-10-16.

Handheld BeebEm ports were consulted for platform-boundary ideas, but this port
is based on the Linux/SDL tree above.

## BeebEm conditions

BeebEm's `COPYING` file is a custom licence, not a conventional permissive
licence. In summary, it requires free distribution, preservation of the
copyright notice, complete source with every distributed binary, no warranty,
acknowledgement for reused sections, and asks the author before large sections
are used in another application.

This is therefore a source-available BeebEm port and must not be distributed as
firmware alone. The exact derived core used by the firmware is in
`components/bbc_core/src`; `LICENSES/BeebEm.txt` reproduces the upstream terms.
Before any public or commercial binary release, review those terms and obtain
clarification or permission where appropriate.

## ROM and software policy

The upstream tree contains historical ROM and media files, but that does not
establish redistribution rights for a new firmware image. The public source
tree does not contain BBC MOS, BASIC, filing-system ROMs, games, disc images,
or archive artwork. Builders supply those assets locally; installed assets and
generated output directories stay ignored.

Hashes for the private bring-up ROMs are recorded in `TEST_ASSETS.md`. Local
testing does not change the release or distribution policy.

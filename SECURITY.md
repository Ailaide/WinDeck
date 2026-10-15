# Security policy

## Supported versions

WinDeck is in early development. Security fixes go into the latest release only; please check that the problem still occurs there before reporting it.

## Reporting a vulnerability

**Please do not report security problems in public issues, discussions, or pull requests.**

Report them privately through GitHub instead: open the repository's **Security** tab and choose **Report a vulnerability**, or go directly to [github.com/Ailaide/WinDeck/security/advisories/new](https://github.com/Ailaide/WinDeck/security/advisories/new).

Please include:

- the WinDeck version and your Windows version,
- what an attacker could do and under which conditions,
- steps or a proof of concept to reproduce it.

Areas that are especially relevant include the ViGEmBus driver download, its SHA-256 verification and the elevated installer launch, the parsing of profile files, and anything that could make WinDeck run code or send input it shouldn't.

WinDeck is maintained in spare time, so responses are best-effort. You'll get a reply in the advisory once the report has been looked at, and you'll be credited in the advisory unless you prefer otherwise. Please give us a reasonable time to release a fix before you disclose the problem publicly.

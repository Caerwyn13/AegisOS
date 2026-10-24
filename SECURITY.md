# Security Policy

AegisOS is an experimental operating system and is not intended for sensitive
or production workloads. Security reports are welcome, and responsible
disclosure helps protect users while issues are investigated.

## Supported Versions

Security fixes are made against the latest published release. Older releases
may not receive fixes; users should update to the latest release before
reporting an issue.

| Version | Supported |
| ------- | --------- |
| Latest release | Yes |
| Earlier releases | No |
| Unreleased development builds | Best effort; fixes are made on `main` |

Support for an alpha release does not imply that it is suitable for production
use. The supported-version list will be updated when a new release is
published.

## Reporting a Vulnerability

Please do not report suspected vulnerabilities in public issues, discussions,
or pull requests. A public report could expose users before a fix is
available.

Use GitHub's **Report a vulnerability** option on the
[AegisOS Security Advisories page](https://github.com/Caerwyn13/AegisOS/security/advisories/new)
to send a private report to the maintainers. Include as much of the following
information as you can:

- The affected release, commit, component, or file.
- A description of the issue and its potential impact.
- Steps to reproduce the issue, including configuration or environment
  details.
- Any proof-of-concept or logs that help confirm the issue. Do not include
  real credentials, private data, or destructive payloads.
- A suggested fix, if you have one.

If GitHub does not offer the private reporting option, do not post vulnerability
details publicly. Open a public issue containing only a request for a private
reporting channel, without describing the vulnerability.

### What to expect

- The maintainers aim to acknowledge a report within **7 calendar days**.
- We aim to provide an initial assessment within **14 calendar days** and to
  send an update at least every **14 days** while the report is being
  investigated.
- If the report is accepted, we will work toward a fix and coordinate
  disclosure with the reporter. Timing depends on the severity, complexity,
  and availability of maintainers; we cannot guarantee a release date.
- If the report is declined or is not considered a security vulnerability, we
  will explain why and, where useful, suggest an appropriate public issue for
  any remaining non-security bug.
- Please allow time for investigation and a coordinated fix before publishing
  vulnerability details.

These timelines are goals for a volunteer project, not a guarantee of support
or remediation.

# Security policy

## Supported version

Security fixes target the latest release on the default branch.

## Reporting

Do not publish credentials or exploitable device details in a public issue.
Contact the repository maintainer privately through the security-reporting
option shown on the GitHub repository.

Include the firmware version, hardware variant, impact, reproduction steps,
and a minimal sanitized example. Remove tokens, passwords, personal files,
private repository names, public webhook URLs, and network identifiers.

## Deployment guidance

- Keep GeekTV on a trusted local network.
- Set a non-default web admin password.
- Treat exported configuration JSON as a secret because it contains Wi-Fi
  passwords.
- Grant GitHub fine-grained tokens read-only access to selected repositories.
- Use signed webhooks and a unique random secret.
- Do not expose the device HTTP API directly to the public internet.
- Verify board type and firmware checksum before flashing.

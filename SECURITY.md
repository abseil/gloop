# Security Policy

## Supported Versions

Only the tip of main is currently supported, reflecting our "live at head"
policy.

## Reporting a Vulnerability

We will accept
[private vulnerability reports](https://github.com/abseil/gloop/security/advisories/new)
through GitHub.

Given the nature of these libraries though, we will generally only respond to
critical vulnerabilities that are reachable through normal use. The following
types of issues are not in scope:

*   Bugs that produce incorrect, but generally safe, behavior
*   Bad behavior that requires violating the documented contract of our APIs
*   Crashes from hardening checks on deprecated APIs, that generally protect
    against worse vulnerabilities
*   Anything that falls outside of our [documented support matrix](./README.md)

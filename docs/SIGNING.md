# Windows signing preparation

Xenon's current releases are unsigned. No signing identity or service has been configured, and the current scripts must not be used to label a package as signed. SHA-256 verifies bytes; it does not identify their publisher.

## Public-trust service setup

Microsoft Azure Artifact Signing supports public-trust signing for individual developers in the United States and Canada. Use an Azure subscription with an **Individual** billing account whose legal name and sold-to address match the government ID used for verification. Complete **Individual → Public** identity validation in the Azure portal, then create a **Public Trust** certificate profile. Its subject identifies the verified individual; a product name alone is not a publisher identity. See Microsoft's [setup instructions](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart) and [current pricing](https://azure.microsoft.com/en-us/pricing/details/artifact-signing/).

Identity documents belong in Microsoft's verification flow. They must never enter this repository, release attachments or build logs. Account, regional endpoint and certificate-profile names are sufficient for subsequent build configuration. Use federated CI authentication with narrowly scoped signing access rather than putting a private key or Azure client secret in source.

## Release integration still required

1. Build and verify Xenon's approved CEF bootstrap branding first. Apply Authenticode signatures to the branded `Xenon.exe` and project-owned `Xenon.dll`, with SHA-256 and RFC 3161 timestamps. Preserve upstream signatures on third-party runtime files.
2. Verify each signature, expected publisher and timestamp before generating the runtime manifest, file hashes and portable ZIP. Signing changes bytes. Never sign an already published artifact in place.
3. Assemble setup from that verified payload, sign setup and its uninstaller, verify their signatures, then generate final installer checksums. Configure Inno Setup's signing support for the uninstaller.
4. Extend package verification, signed/unsigned labels and native update asset selection together. Today they require `signed: false` and exact `-unsigned` filenames. Retain fixed-repository/version selection, size/hash checks, TLS, downgrade protection and the running/setup mutex. A signed path also needs native publisher verification immediately before launch.
5. Test the signed extracted package and isolated installation/update fixtures, then publish a new version with final signature and hash evidence. Preserve the historical unsigned artifacts and their records.

Microsoft documents [SignTool and CI integrations](https://learn.microsoft.com/en-us/azure/artifact-signing/how-to-signing-integrations). Timestamping is required because Artifact Signing's signing certificates are short lived. CEF bootstrap verification must account for the added Authenticode certificate while continuing to compare executable code, manifest and all non-branding resources against the pinned original.

Public-trust signing does not immediately eliminate every SmartScreen warning or prove that a file is malware free. Microsoft explains [publisher reputation and signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options).

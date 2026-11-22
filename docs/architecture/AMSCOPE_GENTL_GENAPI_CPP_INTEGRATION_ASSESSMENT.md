# GenTL and GenApi Integration Assessment for the C++ Camera Module

**Assessment date:** 22 November 2026  
**C++ repository snapshot:** MICROSCOPE_CPP/amscope-native-driver at e1f2e65cb762a78c9bf48461bc0fd2b404944894  
**Related Swift implementation:** AMSCOPE_DRIVER/macos-version-0.4 at bf25e5f93494679fb5de7b6b93d04a7e03113262  
**Status:** Dependency discovery completed; C++ GenTL Consumer integration decision remains open. No source dependency has been integrated by this assessment.

---

## 1. Executive finding

There is an official, current C++ GenApi reference runtime available from EMVA, and there is an existing GenTL Consumer implementation in the user's Swift AMSCOPE_DRIVER repository. They solve different parts of the system:

- **GenApi:** use the official GenApi 3.5.1 reference runtime from the GenICam 2026.07 package. The release includes macOS Runtime and SDK packages and the 3.5.1 release notes state that a CMake package configuration file was added.
- **GenTL:** the official package supplies the GenTL 1.6 standard and reference C header. A camera vendor supplies the actual GenTL Producer as a .cti library. A Consumer must call that Producer through the GenTL C ABI. The official package is not a generic CTI Consumer library.
- **Existing Consumer implementation:** AMSCOPE_DRIVER/macos-version-0.4 contains GenTLNative, a tested Swift Consumer that loads actual .cti libraries with dlopen/symbol lookup and forwards the GenTL operation surface. It is a strong behavioral and test authority, but it is not a C++ library and has no C ABI facade in the inspected branch.
- **AmScope:** retain the native Amcam SDK backend. The Amcam SDK feature getters/setters do not, in the inspected implementation, establish a GenTL Producer, GenApi XML device description, or GenTL register-port access. Do not create a synthetic AmScope .cti or invent GenApi nodes/registers.

Therefore GenApi is no longer an unidentified dependency. The unresolved integration boundary is how the C++ process will consume real CTI Producers without duplicating the existing Swift Consumer or introducing an unapproved Swift runtime dependency.

## 2. Standards and runtime roles

These are separate components, not interchangeable names for one SDK.

| Component | Role | Supplied by | What it does not provide |
|---|---|---|---|
| GenTL 1.6 | C ABI for transport-layer discovery, module lifecycle, device access, streams, buffers, and events | EMVA specification and reference header; Producer vendor implements the exports | A universal CTI Producer or a ready-made Consumer runtime |
| GenTL Producer | Actual transport-specific implementation exported as a .cti library | Camera/transport vendor | A universal application camera model or necessarily GenApi XML |
| GenTL Consumer | Loads a Producer and invokes its GenTL C exports | Application/runtime integration | The Producer's transport implementation |
| GenApi 3.5.1 | Parses a device's GenApi XML description and presents typed feature/node access through the transport's register-port interface | Official EMVA GenApi reference implementation | Device driver or transport Producer |
| Amcam SDK | Native AmScope discovery, device handle, acquisition, frame retrieval, features and accessories | AmScope vendor SDK | GenTL Producer or documented GenApi register/XML endpoint in the inspected API |

EMVA explicitly distinguishes GenApi from GenTL. GenApi maps higher-level feature access described by XML to low-level register access; GenTL standardizes transport enumeration, control and acquisition. The EMVA license document also states that the GenApi reference implementation contains no driver and cannot control a camera by itself.

Official sources:
- [EMVA GenICam downloads](https://www.emva.org/standards-technology/genicam/genicam-downloads/)
- [EMVA GenICam downloads archive](https://www.emva.org/standards-technology/genicam/genicam-downloads-archive/)
- [EMVA GenICam introduction](https://www.emva.org/standards-technology/genicam/introduction-new/)
- [EMVA GenICam license, 9 October 2023](https://www.emva.org/wp-content/uploads/GenICam_License_20231009.pdf)

## 3. Official GenICam dependency identified

The current official full package is **GenICam 2026.07**. EMVA lists:

- GenApi 3.5.1.
- GenTL 1.6.
- SFNC 2.8.
- PFNC 2.4.
- GenTL SFNC 1.2.
- GenCP 1.3.1.
- GenDC 1.2.
- CLProtocol 1.2.
- FWUpdate 1.0.1.

The package SHA-256 published by EMVA is:

e74b8d982c3e7697399b03c254b6e8c596eddf0279959eba7de18734e279b64f

### 3.1 GenApi package suitability

The archive lists GenApi reference implementation 3.5.1 as a maintenance release based on GenApi standard 2.1.1. Its release notes dated 30 June 2026 list:

- GenApi-3.5.1-Mac64_Universal-Runtime.tar.gz
- GenApi-3.5.1-Mac64_Universal-SDK.tar.gz
- GenApi-3.5.1-Mac64_Universal-RuntimeLogging.tar.gz

The release notes also state that GenApi 3.5.1 added a CMake package configuration file and a macOS build. This is the correct existing C++ GenApi dependency to evaluate for this macOS project.

The name Mac64_Universal is not treated as proof of the exact x86_64 and arm64 slices required by this repository. Inspect the delivered binaries and CMake package metadata, then validate both architectures before committing the dependency integration. The package archive itself has not yet been downloaded or inspected in this assessment.

### 3.2 GenTL package role

The EMVA archive lists the GenTL 1.6 reference header separately from the GenApi reference implementation. It is the C ABI declaration authority, not a dynamically loadable generic CTI Consumer runtime.

The application must use the GenTL header matching the chosen standard revision and call the function table exported by each vendor's .cti Producer. Do not confuse:
- GenTL standard/header,
- GenTL Producer supplied by a camera/transport vendor,
- GenTL Consumer implementation owned by the application or an existing integration library.

The exact CTI Producer is camera/transport-vendor-specific and is not present in the reviewed C++ repository.

## 4. Existing Swift implementation in the user's repositories

The most complete existing Consumer implementation located is:

- Repository: [Shennagyp/AMSCOPE_DRIVER](https://github.com/Shennagyp/AMSCOPE_DRIVER)
- Branch: macos-version-0.4
- Commit: bf25e5f93494679fb5de7b6b93d04a7e03113262
- Workflow: [#44892073579](https://github.com/Shennagyp/AMSCOPE_DRIVER/actions/runs/44892073579)

The workflow completed successfully with all six jobs green: portable imaging/runtime tests, GenTLNative, GenTLAPI, GenTLRouter, AmcamImagingGenTLAdapter, and AmcamGenTL.

### 4.1 Components

The branch contains:

- GenTLABI: one imported C ABI header/module and ABI layout checks.
- GenTLAPI: a Swift protocol mirroring 64 GenTL operations and typed feature-access contracts.
- GenTLNative: dynamic CTI loading, required-symbol validation, and native C function-pointer dispatch.
- GenTLRouter: producer/system/interface/device/stream routing and lifecycle ownership.
- AmcamGenTL: a second implementation mapping the common GenTL-shaped API to native Amcam SDK functions where supported.
- AmcamImagingGenTLAdapter: adapter between the imaging layer and GenTL router.
- MockGenTLProducer.c: a compiled C mock Producer loaded through the production dynamic-loader path.

The native loader validates the complete GenTL 1.0 base export set and treats later revision additions as optional where the contract requires. The existing test suite exercises the actual dynamic loading and symbol dispatch path against the compiled mock .cti, as well as Producer discovery, router/session lifecycle, buffer ownership, and failure cleanup.

Selected CI test counts visible in the successful run logs:
- GenTLNative: 19 tests.
- GenTLAPI: 13 tests.
- GenTLRouter: 24 tests.

These are automated/mock-Producer tests. They are not evidence of a physical CTI camera working.

### 4.2 Limits for direct C++ reuse

The implementation is Swift Package Manager code. Its application-facing GenTL contract is a Swift protocol; the inspected branch does not expose a C ABI facade or a C++ API that MICROSCOPE_CPP can directly link.

The branch's GenTLABI README records that its pinned GenTL 1.6 header was moved from the former GenTLNative target without byte changes and that repository history records its origin as the GenTL 1.6 header included with Aravis 0.9.2. For a new C++ integration, use the official EMVA GenTL 1.6 reference header as the ABI authority or verify byte-level equivalence and provenance before reusing the Swift copy.

### 4.3 Amcam does not become GenApi merely by implementing GenTL-shaped methods

The Swift CONFORMANCE.md expressly marks Amcam GenTL XML URL access and physical port/register access as not implemented. The Amcam adapter maps documented SDK capabilities and reports unsupported GenTL operations honestly. It does not invent XML, register maps, chunks, or multipart payloads.

This confirms the intended backend distinction:
- Native Amcam backend: native Amcam SDK session and acquisition path.
- GenTL backend: vendor .cti Producer, GenTL module/session/stream path, and GenApi feature map only when the Producer/device actually exposes the required XML and port operations.

Common camera models do not imply that both backends must use identical low-level control or acquisition mechanisms.

## 5. C++ repository audit

The following relevant repository branches were checked for GenTL/GenApi/CTI implementation paths:

- MICROSCOPE_CPP/amscope-native-driver.
- MICROSCOPE_CPP/minimal-amscope-gui-mac.
- MICROSCOPE/station-development.
- AMSCOPE_MU503_DRIVER/swift-gentl-api.
- AMSCOPE_DRIVER/main and AMSCOPE_DRIVER/macos-version-0.4.

No existing C++ GenTL Consumer, GenApi runtime, CTI adapter, or vendor CTI library was found in those inspected trees. The active C++ branch currently has no imported GenTL/GenApi C++ targets and no CTI Producer.

This is a repository audit, not proof that no relevant SDK is installed on the developer's machine or available in another repository/branch.

## 6. License and distribution boundary

The EMVA GenICam license document distinguishes runtime and development versions of the GenApi reference implementation:

- The runtime is available as binary to the public under a modified BSD-style license.
- Binary redistribution must reproduce the required copyright notice, conditions, and disclaimer in documentation/materials.
- The runtime reference implementation must not be modified (apart from the stated installation procedure).
- The development source is restricted to GenICam Working Group member companies and has additional obligations; it must not be copied into this project as ordinary public source.
- Third-party modules included in a GenApi package may have their own licenses, which override the GenICam terms for those modules.

Consequently, prefer consuming the official unmodified runtime/SDK package through its supported CMake configuration. Before packaging a release, inspect the exact package's license files and transitive third-party notices, decide whether to require an external runtime installation or redistribute permitted runtime binaries, and add the required notices. Do not vendor or patch restricted development source.

The vendor CTI Producer has its own vendor distribution and architecture constraints. It must be installed/supplied separately and loaded as a plugin; it must not be renamed or modified as if it were an application-owned library.

## 7. C++ integration alternatives requiring an explicit decision

The existing code and standards do not yet select one C++ Consumer integration mechanism.

| Option | Mechanism | Main consequence |
|---|---|---|
| A — Reuse Swift Consumer | Add a narrow C ABI facade to the existing Swift GenTLNative/Router stack and link that product from C++ | Preserves the existing tested Consumer implementation, but adds Swift runtime/build/package requirements and requires designing/maintaining a C ABI facade |
| B — C++ Consumer port | Port the existing GenTLNative Consumer behavior to C++ against the official GenTL 1.6 ABI header; retain the Swift implementation as behavioral/test authority | Produces a native C++ dependency but duplicates/ports Consumer implementation logic; this needs explicit acceptance because the project rule is not to reimplement GenTL |
| C — Existing external C++ Consumer | Identify a currently available C++ GenTL Consumer library/runtime already used by the developer or camera vendor | Potentially avoids both Swift runtime integration and a new Consumer implementation; exact library, ABI, version, and distribution conditions must be verified |

The official GenApi 3.5.1 runtime is a separate dependency and can be used with the selected Consumer path when a device exposes valid GenApi XML and port access. It does not resolve the Consumer choice by itself.

**No option has been selected. No C++ GenTL adapter or dependency has been added.**

## 8. Required evidence before Phase 5 implementation

1. Confirm the target CTI camera model and its exact vendor Producer package, version, .cti path, license, and supported macOS architectures.
2. Inspect the official GenApi 3.5.1 Runtime/SDK package, CMake config, transitive libraries, architecture slices, and redistribution notices.
3. Resolve the Consumer integration choice in Section 7.
4. Verify which GenApi XML and register-port capabilities the target CTI camera/Producer exposes.
5. Build and run the first physical CTI camera test while retaining the AmScope regression unchanged.
6. Add a hardware record for both the CTI camera and AmScope reference camera before Phase 5 can pass.

Until these gates are satisfied, Phase 5 remains blocked. This does not block non-behavior-changing dependency research or Phase 0 camera baseline preparation.

---

## References

1. EMVA, [GenICam downloads](https://www.emva.org/standards-technology/genicam/genicam-downloads/), package 2026.07.
2. EMVA, [GenICam downloads archive](https://www.emva.org/standards-technology/genicam/genicam-downloads-archive/), GenApi 3.5.1, GenTL 1.6 reference header, and release notes.
3. EMVA, [GenICam introduction](https://www.emva.org/standards-technology/genicam/introduction-new/), module responsibilities.
4. EMVA, [GenICam License, 9 October 2023](https://www.emva.org/wp-content/uploads/GenICam_License_20231009.pdf), runtime and development license conditions.
5. AMSCOPE_DRIVER/macos-version-0.4, especially GenTLABI, GenTLAPI, GenTLNative, GenTLRouter, AmcamGenTL, and CONFORMANCE.md.

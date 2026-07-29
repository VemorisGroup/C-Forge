// swift-tools-version: 5.9
// C-Forge iOS / macOS Package — Swift Package Manager
// Embeds el intérprete C-Forge como biblioteca C++ y expone una API Swift

import PackageDescription

let package = Package(
    name: "CForgeApp",
    platforms: [
        .iOS(.v16),
        .macOS(.v13)
    ],
    products: [
        .library(name: "CForgeRuntime", targets: ["CForgeRuntime"]),
        .executable(name: "CForgeApp", targets: ["CForgeApp"])
    ],
    targets: [
        // ── Runtime C++ (intérprete cforgev embebido) ──────────────────────────
        .target(
            name: "CForgeRuntime",
            path: "Sources/CForgeRuntime",
            publicHeadersPath: "include",
            cxxSettings: [
                .define("CFORGE_EMBEDDED"),
                .define("CFV_DISABLE_HTTP"),      // HTTP usa BSD sockets, ok en iOS
                .define("CFV_DISABLE_READLINE"),   // sin readline en iOS
                .headerSearchPath("include"),
                .unsafeFlags(["-std=c++20", "-O2", "-fexceptions"])
            ],
            linkerSettings: [
                .linkedLibrary("c++")
            ]
        ),
        // ── App Swift ─────────────────────────────────────────────────────────
        .executableTarget(
            name: "CForgeApp",
            dependencies: ["CForgeRuntime"],
            path: "Sources/CForgeApp",
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ]
        )
    ],
    cxxLanguageStandard: .cxx20
)

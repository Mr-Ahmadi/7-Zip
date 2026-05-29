// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "SevenZip",
    platforms: [
        .macOS(.v14),
        .iOS(.v17)
    ],
    products: [
        .library(name: "CSevenZip", targets: ["CSevenZip"]),
        .executable(name: "7-Zip", targets: ["SevenZipApp"]),
        .executable(name: "7z", targets: ["SevenZipCLI"])
    ],
    targets: [
        .target(
            name: "CSevenZip",
            publicHeadersPath: "include",
            cxxSettings: [
                .unsafeFlags(["-std=c++17"])
            ]
        ),
        .executableTarget(
            name: "SevenZipApp",
            dependencies: ["CSevenZip"]
        ),
        .executableTarget(
            name: "SevenZipCLI",
            dependencies: ["CSevenZip", "CNcurses"]
        ),
        .target(
            name: "CNcurses"
        )
    ],
    cxxLanguageStandard: .cxx17
)

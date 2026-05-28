// swift-tools-version: 6.2
import PackageDescription

let package = Package(
    name: "StopwatchBridge",
    platforms: [.macOS(.v14)],
    products: [
        .executable(name: "stopwatch-bridge", targets: ["StopwatchBridge"])
    ],
    targets: [
        .executableTarget(
            name: "StopwatchBridge",
            path: "Sources/StopwatchBridge"
        ),
        .testTarget(
            name: "StopwatchBridgeTests",
            dependencies: ["StopwatchBridge"],
            path: "Tests/StopwatchBridgeTests",
            resources: [
                .copy("../../../shared/fixtures")
            ]
        )
    ]
)

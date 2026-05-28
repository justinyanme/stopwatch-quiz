import Foundation

@main
struct StopwatchBridge {
    static func main() {
        let args = Array(CommandLine.arguments.dropFirst())
        guard let cmd = args.first else {
            print("Usage: stopwatch-bridge [run|install|pair|decode-snapshot <hex>]")
            exit(2)
        }
        switch cmd {
        case "version":
            print("stopwatch-bridge 0.1.0")
        default:
            print("unknown command: \(cmd)")
            exit(2)
        }
    }
}

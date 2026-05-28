import Foundation

@main
struct StopwatchBridge {
    static func main() async {
        let args = Array(CommandLine.arguments.dropFirst())
        guard let cmd = args.first else { usage(); exit(2) }
        switch cmd {
        case "run":               await runCommand()
        case "version":           print("stopwatch-bridge 0.1.0")
        case "install", "pair", "decode-snapshot":
            print("\(cmd): not yet implemented (coming in Task A.9–A.10)")
            exit(2)
        default: usage(); exit(2)
        }
    }

    static func usage() {
        print("""
        Usage: stopwatch-bridge <command>
          run                       Foreground daemon (launchd invokes this)
          install                   Install as launchd agent (TODO)
          pair                      Foreground with verbose logging (TODO)
          decode-snapshot <hex>     Print a captured snapshot as JSON (TODO)
          version                   Print version
        """)
    }

    static func runCommand() async {
        let cfg: Config
        do {
            if let loaded = try Config.load() {
                cfg = loaded
            } else {
                cfg = Config.makeDefault()
                try Config.save(cfg)
                FileHandle.standardOutput.write(Data("wrote default config to \(Config.defaultPath.path)\n".utf8))
            }
        } catch {
            FileHandle.standardError.write(Data("config error: \(error)\n".utf8))
            exit(1)
        }
        let service = await BridgeService(config: cfg)
        await service.run()
    }
}

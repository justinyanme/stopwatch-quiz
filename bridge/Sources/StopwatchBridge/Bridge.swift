import Foundation

@main
struct StopwatchBridge {
    static func main() async {
        let args = Array(CommandLine.arguments.dropFirst())
        guard let cmd = args.first else { usage(); exit(2) }
        switch cmd {
        case "run":               await runCommand(verbose: false)
        case "pair":              await runCommand(verbose: true)
        case "install":           exit(InstallCommand.run())
        case "decode-snapshot":
            guard args.count >= 2 else { usage(); exit(2) }
            exit(DecodeCommand.run(args[1]))
        case "version":           print("stopwatch-bridge 0.1.0")
        default: usage(); exit(2)
        }
    }

    static func usage() {
        print("""
        Usage: stopwatch-bridge <command>
          run                       Foreground daemon (launchd invokes this)
          install                   Install as launchd agent
          pair                      Foreground with verbose logging
          decode-snapshot <hex>     Print a captured snapshot as JSON
          version                   Print version
        """)
    }

    static func runCommand(verbose: Bool) async {
        if verbose {
            FileHandle.standardOutput.write(Data("pair mode: verbose logging on\n".utf8))
        }
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

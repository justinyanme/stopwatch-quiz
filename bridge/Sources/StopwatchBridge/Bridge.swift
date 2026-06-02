import Foundation

@main
struct StopwatchBridge {
    static func main() async {
        let args = Array(CommandLine.arguments.dropFirst())
        guard let cmd = args.first else { usage(); exit(2) }
        switch cmd {
        case "run":               await runCommand(verbose: false)
        case "pair":              await runCommand(verbose: true)
        case "serve-config":      printServeConfig()
        case "install":           exit(InstallCommand.run())
        case "decode-snapshot":
            guard args.count >= 2 else { usage(); exit(2) }
            exit(DecodeCommand.run(args[1]))
        case "set-key":
            guard args.count >= 2 else { usage(); exit(2) }
            exit(KeyCommand.setKey(args[1]))
        case "list-keys":   exit(KeyCommand.listKeys())
        case "delete-key":
            guard args.count >= 2 else { usage(); exit(2) }
            exit(KeyCommand.deleteKey(args[1]))
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
          serve-config              Print HTTP server URL and redacted token metadata
          decode-snapshot <hex>     Print a captured snapshot as JSON
          set-key <id>              Store a provider API key in the Keychain (reads stdin)
          list-keys                 List provider ids with stored keys
          delete-key <id>           Remove a stored provider key
          version                   Print version
        """)
    }

    static func printServeConfig() {
        do {
            let cfg: Config
            if let loaded = try Config.load() {
                cfg = loaded
            } else {
                cfg = Config.makeDefault()
                try Config.save(cfg)
            }
            print("url=http://\(cfg.httpBindHost):\(cfg.httpPort)")
            print("apiToken=set length=\(cfg.apiToken.count)")
        } catch {
            FileHandle.standardError.write(Data("config error: \(error)\n".utf8))
            exit(1)
        }
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

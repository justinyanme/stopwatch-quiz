import Foundation
import Testing
@testable import StopwatchBridge

@Suite struct InstallCommandTests {

    @Test func launchAgentPlistEscapesBinaryPathXMLCharacters() throws {
        let plist = try InstallCommand.launchAgentPlist(binaryPath: "/Users/me/R&D/<stopwatch>/stopwatch-bridge")
        let object = try PropertyListSerialization.propertyList(from: plist, options: [], format: nil)
        let dict = try #require(object as? [String: Any])
        let args = try #require(dict["ProgramArguments"] as? [String])

        #expect(args == ["/Users/me/R&D/<stopwatch>/stopwatch-bridge", "run"])
    }
}

import Testing
@testable import StopwatchBridge

@Suite struct HTTPAuthTests {
    @Test func acceptsExactBearerToken() {
        let auth = HTTPAuthenticator(apiToken: "secret")
        #expect(auth.isAuthorized(headers: ["authorization": "Bearer secret"]))
    }

    @Test func rejectsMissingWrongAndMalformedToken() {
        let auth = HTTPAuthenticator(apiToken: "secret")
        #expect(!auth.isAuthorized(headers: [:]))
        #expect(!auth.isAuthorized(headers: ["authorization": "secret"]))
        #expect(!auth.isAuthorized(headers: ["authorization": "Bearer wrong"]))
        #expect(!auth.isAuthorized(headers: ["authorization": "bearer secret"]))
    }

    @Test func disabledTokenRejectsAllRequests() {
        let auth = HTTPAuthenticator(apiToken: "")
        #expect(!auth.isAuthorized(headers: ["authorization": "Bearer "]))
    }
}

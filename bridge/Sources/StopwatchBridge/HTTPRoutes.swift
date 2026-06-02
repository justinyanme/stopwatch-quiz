public enum HTTPRoute: Equatable, Sendable {
    case health
    case read(SnapshotKind)
    case refresh(UInt8)

    public static func parse(method: String, path: String, query: [String: String]) throws -> HTTPRoute {
        switch path {
        case "/v1/health":
            guard method == "GET" else { throw HTTPRouteError.methodNotAllowed }
            return .health
        case "/v1/snapshot":
            guard method == "GET" else { throw HTTPRouteError.methodNotAllowed }
            return .read(.snapshot)
        case "/v1/cost":
            guard method == "GET" else { throw HTTPRouteError.methodNotAllowed }
            return .read(.cost)
        case "/v1/balances":
            guard method == "GET" else { throw HTTPRouteError.methodNotAllowed }
            return .read(.balances)
        case "/v1/balance-usage":
            guard method == "GET" else { throw HTTPRouteError.methodNotAllowed }
            return .read(.balanceUsage)
        case "/v1/refresh":
            guard method == "POST" else { throw HTTPRouteError.methodNotAllowed }
            guard let raw = query["scope"],
                  let intValue = Int(raw),
                  intValue >= 0,
                  intValue <= 255
            else {
                throw HTTPRouteError.badRequest
            }
            return .refresh(UInt8(intValue))
        default:
            throw HTTPRouteError.notFound
        }
    }
}

public enum HTTPRouteError: Error, Equatable, Sendable {
    case methodNotAllowed
    case notFound
    case badRequest
}

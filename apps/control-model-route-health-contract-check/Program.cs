using Hibiki.ControlModel;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static RouteHealthCardV1 Route(
    string id,
    string name = "Route",
    string detail = "Status",
    RouteHealthStateV1 state = RouteHealthStateV1.Ready,
    bool requiresUserAction = false) =>
    new(id, name, state, detail, requiresUserAction);

static void ExpectReject(
    ExpertSurfaceModel model,
    IReadOnlyList<RouteHealthCardV1> cards,
    IReadOnlyList<RouteHealthCardV1> expected,
    string label)
{
    Check(!model.TryApplyRouteHealth(cards, out _), $"{label} must be rejected.");
    Check(model.RouteHealth.SequenceEqual(expected),
        $"{label} rejection must preserve the previous route-health state.");
}

var model = new ExpertSurfaceModel();
Check(model.TryApplyRouteHealth(
          [Route("baseline", "Baseline", "Baseline detail")], out var baselineError) &&
      baselineError.Length == 0,
      "A valid baseline route-health card must be accepted.");
var baseline = model.RouteHealth.ToArray();

Check(model.TryApplyRouteHealth(
          [Route(new string('i', 31), new string('n', 63), new string('d', 119))],
          out var boundaryError) && boundaryError.Length == 0,
      "ASCII values exactly at the byte limits must be accepted.");
var boundary = model.RouteHealth.ToArray();

Check(model.TryApplyRouteHealth(
          [Route("路由", "正常", "🎧 ready")], out var unicodeError) &&
      unicodeError.Length == 0,
      "Valid multibyte UTF-8 and supplementary-plane text must be accepted.");
var unicode = model.RouteHealth.ToArray();

ExpectReject(model, [Route(new string('i', 32))], unicode, "31-byte ID overflow");
ExpectReject(model, [Route("路由路由路由路由路由路由")], unicode,
              "multibyte ID overflow");
ExpectReject(model, [Route("name-overflow", new string('n', 64))], unicode,
              "63-byte name overflow");
ExpectReject(model, [Route("detail-overflow", detail: new string('d', 120))], unicode,
              "119-byte detail overflow");
ExpectReject(model, [Route("high-surrogate-\uD800")], unicode,
              "isolated high surrogate");
ExpectReject(model, [Route("low-surrogate-\uDC00")], unicode,
              "isolated low surrogate");
ExpectReject(model, [Route("control-name", "Name\u0001")], unicode,
              "C0 control character");
ExpectReject(model, [Route("control-detail", detail: "Detail\u007F")], unicode,
              "DEL control character");
ExpectReject(model, [Route("whitespace-name", "   ")], unicode,
              "whitespace-only name");
ExpectReject(model, [Route("duplicate"), Route("duplicate", "Other", "Other")], unicode,
              "duplicate route ID");
ExpectReject(model, [new RouteHealthCardV1(
                  "invalid-state", "Route", (RouteHealthStateV1)99, "Status")], unicode,
              "unknown route state");

var sixteen = Enumerable.Range(0, 16)
    .Select(index => Route($"route-{index:00}"))
    .ToArray();
Check(model.TryApplyRouteHealth(sixteen, out var sixteenError) &&
      sixteenError.Length == 0 && model.RouteHealth.Count == 16,
      "The local control model must intentionally retain up to 16 route cards.");
var sixteenSnapshot = model.RouteHealth.ToArray();
ExpectReject(model, sixteen.Append(Route("route-16")).ToArray(), sixteenSnapshot,
              "17-card local capacity overflow");

Check(model.TryApplyRouteHealth(baseline, out var restoreError) && restoreError.Length == 0 &&
      model.RouteHealth.SequenceEqual(baseline),
      "A valid route-health snapshot must be applicable after rejected inputs.");
Check(boundary.Length == 1 && boundary[0].Id.Length == 31 &&
      boundary[0].Name.Length == 63 && boundary[0].Detail.Length == 119,
      "Boundary fixture must retain its exact UTF-16 lengths for regression clarity.");
Check(unicode.Length == 1 && unicode[0].Detail.Contains("🎧", StringComparison.Ordinal),
      "Unicode fixture must retain the supplementary-plane scalar.");

Console.WriteLine("Control-model route-health UTF-8 boundary checks passed.");

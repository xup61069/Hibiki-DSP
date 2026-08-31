using Hibiki.ControlModel;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

var session = new EasyControlSession();

// 1. Rejection on empty / whitespace
var emptyResult = session.OneTapEnhance("   ");
Check(!emptyResult.Succeeded, "Whitespace output group must fail.");
Check(session.Status == AudioControlStatus.Degraded, "Empty output group sets Degraded status.");
Check(session.ActiveOutputGroup == null, "Empty output group leaves ActiveOutputGroup null.");
Check(session.ActiveScene == null, "Empty output group leaves ActiveScene null.");

// 2. Rejection on unknown / unsupported group
var unknownResult = session.OneTapEnhance("bogus-group");
Check(!unknownResult.Succeeded, "Unknown output group must fail.");
Check(unknownResult.Message == "不支援的輸出群組", "Unknown output group message must match.");
Check(session.ActiveOutputGroup == null, "Unknown output group leaves ActiveOutputGroup null.");
Check(session.ActiveScene == null, "Unknown output group leaves ActiveScene null.");
Check(session.Status == AudioControlStatus.Degraded, "Unknown output group preserves previous status.");

// 3. Success for all three fixed catalog members
var fixedGroups = new[] { "main", "low-latency", "surround" };
foreach (var group in fixedGroups)
{
    var result = session.OneTapEnhance($"  {group}  ");
    Check(result.Succeeded, $"Group '{group}' must succeed.");
    Check(session.ActiveOutputGroup == group, $"Group '{group}' must be trimmed and set.");
    Check(session.ActiveScene != null && session.ActiveScene.Id == "game", "ActiveScene defaults to game.");
    Check(session.Status == AudioControlStatus.Controlled, "Status becomes Controlled on success.");
}

// 4. Rejection after successful control preserves existing active state
var rejectedAfterSuccess = session.OneTapEnhance("headphones-2");
Check(!rejectedAfterSuccess.Succeeded, "Invalid output group after success must fail.");
Check(session.ActiveOutputGroup == "surround", "ActiveOutputGroup must be preserved after rejection.");
Check(session.ActiveScene != null && session.ActiveScene.Id == "game", "ActiveScene must be preserved after rejection.");
Check(session.Status == AudioControlStatus.Controlled, "Status must be preserved after rejection.");

Console.WriteLine("Control-model output-group boundary checks passed.");
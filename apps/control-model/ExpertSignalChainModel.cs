// SPDX-License-Identifier: GPL-3.0-only

namespace Hibiki.ControlModel;

// This is deliberately a reference topology, not a live graph snapshot.  The
// control plane has no versioned readback for individual attachment stages, so
// the model must not turn a visual state into an engine-commit claim.
public enum ExpertSignalChainNodeState
{
    Reference,
    Conditional,
    Safety,
}

public sealed record ExpertSignalChainNode(
    string Id,
    string Name,
    string Detail,
    ExpertSignalChainNodeState State)
{
    public string StateText => State switch
    {
        ExpertSignalChainNodeState.Reference => "固定參考",
        ExpertSignalChainNodeState.Conditional => "依設定",
        ExpertSignalChainNodeState.Safety => "安全防護",
        _ => "未確認",
    };

    public string AccessibleText => $"{Name}：{StateText}。{Detail}";
}

public sealed record ExpertSignalChainEdge(string SourceId, string DestinationId);

public sealed record ExpertSignalChainStep(
    ExpertSignalChainNode Node,
    string ConnectorText,
    string ConnectorAccessibleText)
{
    public string AccessibleText => $"{Node.AccessibleText} {ConnectorAccessibleText}";
}

public sealed class ExpertSignalChainModel
{
    public const int ReferenceNodeCount = 7;
    public const int MaxNodeIdLength = 32;
    public const int MaxDisplayTextLength = 160;

    private static readonly string[] ReferenceNodeIds =
    [
        "lane-mix",
        "ir",
        "equal-loudness",
        "program-aware",
        "vst3",
        "group-master",
        "limiter",
    ];

    private ExpertSignalChainModel(
        IReadOnlyList<ExpertSignalChainNode> nodes,
        IReadOnlyList<ExpertSignalChainEdge> edges)
    {
        Nodes = Array.AsReadOnly(nodes.ToArray());
        Edges = Array.AsReadOnly(edges.ToArray());
        Steps = BuildSteps(Nodes);
    }

    public static ExpertSignalChainModel Reference { get; } = CreateReference();

    public string Title => "DSP 訊號鏈（唯讀）";

    public string BoundaryText =>
        "這是固定處理順序的架構參考，不是引擎已提交 graph、可聽輸出或實體音訊證據。";

    public IReadOnlyList<ExpertSignalChainNode> Nodes { get; }

    public IReadOnlyList<ExpertSignalChainEdge> Edges { get; }

    public IReadOnlyList<ExpertSignalChainStep> Steps { get; }

    public string AccessibleSummary =>
        $"{Title}。唯讀架構參考，非引擎已提交 graph readback。順序：" +
        string.Join(" → ", Nodes.Select(node => $"{node.Name}（{node.StateText}）")) + "。";

    public static bool TryCreate(
        IReadOnlyList<ExpertSignalChainNode>? nodes,
        IReadOnlyList<ExpertSignalChainEdge>? edges,
        out ExpertSignalChainModel? model,
        out string error)
    {
        model = null;
        error = string.Empty;

        if (nodes is null || nodes.Count != ReferenceNodeCount)
        {
            error = $"Expert 參考訊號鏈必須剛好有 {ReferenceNodeCount} 個節點。";
            return false;
        }

        if (edges is null || edges.Count != nodes.Count - 1)
        {
            error = "訊號鏈必須為每對相鄰節點提供一條連線。";
            return false;
        }

        var knownIds = new HashSet<string>(StringComparer.Ordinal);
        for (var index = 0; index < nodes.Count; index++)
        {
            var node = nodes[index];
            if (node is null || !IsValidId(node.Id) || !IsDisplayText(node.Name) ||
                !IsDisplayText(node.Detail) || !Enum.IsDefined(node.State) ||
                !knownIds.Add(node.Id) || node.Id != ReferenceNodeIds[index])
            {
                error = "訊號鏈節點含有無效、重複或不屬於固定參考順序的身份。";
                return false;
            }
        }

        for (var index = 0; index < edges.Count; index++)
        {
            var edge = edges[index];
            if (edge is null || edge.SourceId != nodes[index].Id ||
                edge.DestinationId != nodes[index + 1].Id)
            {
                error = "訊號鏈連線必須依節點順序，且不得跳接、反向或指向未知節點。";
                return false;
            }
        }

        model = new ExpertSignalChainModel(nodes.ToArray(), edges.ToArray());
        return true;
    }

    private static ExpertSignalChainModel CreateReference()
    {
        ExpertSignalChainNode[] nodes =
        [
            new("lane-mix", "Lane Mix", "將有界 lane 混音為指定輸出群組。",
                ExpertSignalChainNodeState.Reference),
            new("ir", "IR", "只有已準備的 IR 場景設定才可能使用。",
                ExpertSignalChainNodeState.Conditional),
            new("equal-loudness", "Equal-loudness",
                "依現有等響度補償設定處理；此畫面不提供曲線或校準值。",
                ExpertSignalChainNodeState.Conditional),
            new("program-aware", "Program-aware", "依 program-aware 控制設定處理。",
                ExpertSignalChainNodeState.Conditional),
            new("vst3", "VST3", "只有隔離且已啟用的 lane 才可能經過 plugin。",
                ExpertSignalChainNodeState.Conditional),
            new("group-master", "Group Master", "套用輸出群組的音量與安全控制。",
                ExpertSignalChainNodeState.Reference),
            new("limiter", "True-peak limiter", "最後的安全上限階段；不表示目前可聽輸出。",
                ExpertSignalChainNodeState.Safety),
        ];

        var edges = nodes.Zip(nodes.Skip(1),
            (source, destination) => new ExpertSignalChainEdge(source.Id, destination.Id)).ToArray();
        if (!TryCreate(nodes, edges, out var reference, out var error) || reference is null)
        {
            throw new InvalidOperationException($"內建 Expert 訊號鏈無效：{error}");
        }

        return reference;
    }

    private static IReadOnlyList<ExpertSignalChainStep> BuildSteps(
        IReadOnlyList<ExpertSignalChainNode> nodes)
    {
        var steps = new ExpertSignalChainStep[nodes.Count];
        for (var index = 0; index < nodes.Count; index++)
        {
            var node = nodes[index];
            if (index + 1 < nodes.Count)
            {
                var destination = nodes[index + 1];
                steps[index] = new ExpertSignalChainStep(
                    node,
                    $"↓ 下一階段：{destination.Name}",
                    $"訊號方向：{node.Name} 至 {destination.Name}。");
            }
            else
            {
                steps[index] = new ExpertSignalChainStep(
                    node,
                    "此參考鏈結束；不代表實體輸出。",
                    "此參考鏈結束；不代表實體輸出。");
            }
        }

        return Array.AsReadOnly(steps);
    }

    private static bool IsValidId(string value) =>
        !string.IsNullOrWhiteSpace(value) && value.Length <= MaxNodeIdLength &&
        value.All(character => char.IsAsciiLetterOrDigit(character) || character == '-');

    private static bool IsDisplayText(string value) =>
        !string.IsNullOrWhiteSpace(value) && value.Length <= MaxDisplayTextLength &&
        value.All(character => !char.IsControl(character));
}

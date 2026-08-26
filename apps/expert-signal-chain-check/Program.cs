using Hibiki.ControlModel;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

var chain = ExpertSignalChainModel.Reference;
string[] expectedNodeIds =
[
    "lane-mix",
    "ir",
    "equal-loudness",
    "program-aware",
    "vst3",
    "group-master",
    "limiter",
];

Check(chain.Nodes.Select(node => node.Id).SequenceEqual(expectedNodeIds),
    "Expert reference topology must preserve the documented fixed processing order.");
Check(chain.Nodes.Count == ExpertSignalChainModel.ReferenceNodeCount,
    "Expert reference topology must have exactly the fixed number of stages.");
Check(chain.Nodes is not ExpertSignalChainNode[] &&
      chain.Edges is not ExpertSignalChainEdge[] &&
      chain.Steps is not ExpertSignalChainStep[],
    "Expert reference topology must not expose mutable backing arrays.");
Check(chain.Edges.Count == expectedNodeIds.Length - 1 &&
      chain.Edges.Select(edge => edge.SourceId).SequenceEqual(expectedNodeIds[..^1]) &&
      chain.Edges.Select(edge => edge.DestinationId).SequenceEqual(expectedNodeIds[1..]),
    "Expert reference topology must expose one directed edge between each adjacent stage.");
Check(chain.Nodes.Select(node => node.Id).Distinct(StringComparer.Ordinal).Count() == chain.Nodes.Count &&
      chain.Nodes.All(node => !string.IsNullOrWhiteSpace(node.StateText) &&
                              !string.IsNullOrWhiteSpace(node.AccessibleText)),
    "Every Expert node must have a unique identity and readable state text.");
Check(chain.Steps.Count == chain.Nodes.Count &&
      chain.Steps.Take(chain.Steps.Count - 1)
          .All(step => step.ConnectorText.StartsWith("↓ 下一階段：", StringComparison.Ordinal)) &&
      chain.Steps[^1].ConnectorText.Contains("不代表實體輸出", StringComparison.Ordinal) &&
      chain.AccessibleSummary.Contains("非引擎已提交 graph readback", StringComparison.Ordinal) &&
      chain.BoundaryText.Contains("不是引擎已提交 graph", StringComparison.Ordinal),
    "Expert node canvas must expose direction and the reference-only boundary in text.");

var duplicateNodes = chain.Nodes.ToArray();
duplicateNodes[1] = duplicateNodes[1] with { Id = "lane-mix" };
Check(!ExpertSignalChainModel.TryCreate(duplicateNodes, chain.Edges, out _, out _),
    "Duplicate node identities must fail closed.");

var missingStageNodes = chain.Nodes.Skip(1).ToArray();
var missingStageEdges = chain.Edges.Skip(1).ToArray();
Check(!ExpertSignalChainModel.TryCreate(missingStageNodes, missingStageEdges, out _, out _),
    "A missing fixed stage must fail closed.");

var extraStageNodes = chain.Nodes.Concat(
    [new ExpertSignalChainNode("extra-stage", "Extra", "Not part of the reference chain.",
        ExpertSignalChainNodeState.Reference)]).ToArray();
var extraStageEdges = chain.Edges.Concat(
    [new ExpertSignalChainEdge("limiter", "extra-stage")]).ToArray();
Check(!ExpertSignalChainModel.TryCreate(extraStageNodes, extraStageEdges, out _, out _),
    "An extra fixed stage must fail closed.");

var replacedStageNodes = chain.Nodes.ToArray();
replacedStageNodes[2] = replacedStageNodes[2] with { Id = "other-stage" };
var replacedStageEdges = chain.Edges.ToArray();
replacedStageEdges[1] = new ExpertSignalChainEdge("ir", "other-stage");
replacedStageEdges[2] = new ExpertSignalChainEdge("other-stage", "program-aware");
Check(!ExpertSignalChainModel.TryCreate(replacedStageNodes, replacedStageEdges, out _, out _),
    "A replacement stage must fail closed even when its edges are otherwise linear.");

var reversedFirstEdge = chain.Edges.ToArray();
reversedFirstEdge[0] = new ExpertSignalChainEdge("ir", "lane-mix");
Check(!ExpertSignalChainModel.TryCreate(chain.Nodes, reversedFirstEdge, out _, out _),
    "Reversed or non-sequential edges must fail closed.");

var unknownEdge = chain.Edges.ToArray();
unknownEdge[1] = new ExpertSignalChainEdge("ir", "unknown-stage");
Check(!ExpertSignalChainModel.TryCreate(chain.Nodes, unknownEdge, out _, out _),
    "Edges targeting an unknown stage must fail closed.");

Console.WriteLine("Expert signal-chain foundation checks passed.");

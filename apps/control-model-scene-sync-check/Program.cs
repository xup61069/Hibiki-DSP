using System.Buffers.Binary;
using System.IO.Pipes;
using Hibiki.ControlModel;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static IpcEnvelopeV1 AckReply(IpcEnvelopeV1 request) =>
    new(ControlMessageType.Ack, request.RequestId, Array.Empty<byte>());

static IpcEnvelopeV1 ErrorReply(IpcEnvelopeV1 request) =>
    new(ControlMessageType.Error, request.RequestId, Array.Empty<byte>());

static async Task ReadExactAsync(Stream stream, Memory<byte> buffer,
                                 CancellationToken cancellationToken)
{
    var offset = 0;
    while (offset < buffer.Length)
    {
        var read = await stream.ReadAsync(buffer[offset..], cancellationToken);
        if (read <= 0) throw new EndOfStreamException("check pipe closed early");
        offset += read;
    }
}

static async Task RunErrorServerAsync(string pipeName, TaskCompletionSource connected,
                                      CancellationToken cancellationToken)
{
    while (!cancellationToken.IsCancellationRequested)
    {
        await using var server = new NamedPipeServerStream(
            pipeName, PipeDirection.InOut, 1, PipeTransmissionMode.Byte,
            PipeOptions.Asynchronous);
        try
        {
            await server.WaitForConnectionAsync(cancellationToken);
            connected.TrySetResult();
            while (true)
            {
                var lengthPrefix = new byte[4];
                await ReadExactAsync(server, lengthPrefix, cancellationToken);
                var frameLength = BinaryPrimitives.ReadUInt32LittleEndian(lengthPrefix);
                if (frameLength > IpcCodecV1.MaxPayloadBytes + IpcCodecV1.HeaderBytes)
                    throw new InvalidDataException("frame too large");
                var frame = new byte[checked((int)frameLength)];
                await ReadExactAsync(server, frame, cancellationToken);
                if (!IpcCodecV1.TryDecode(frame, out var request, out _))
                    throw new InvalidDataException("undecodable request");

                var reply = request!.Type == ControlMessageType.SceneCatalogCommand
                    ? ErrorReply(request)
                    : AckReply(request);
                var encoded = IpcCodecV1.Encode(reply);
                var replyLength = new byte[4];
                BinaryPrimitives.WriteUInt32LittleEndian(replyLength, (uint)encoded.Length);
                await server.WriteAsync(replyLength, cancellationToken);
                await server.WriteAsync(encoded, cancellationToken);
                await server.FlushAsync(cancellationToken);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
            return;
        }
        catch (EndOfStreamException)
        {
            // The client may close after a failed command; accept the next
            // connection so each fixture can observe a fresh failure.
        }
        catch (IOException)
        {
            // A failed command can close the pipe between requests.
        }
    }
}

static async Task<EasyControlViewModel> ConnectFailingViewModelAsync(
    string pipeName, string cardPath, string queuePath)
{
    var viewModel = new EasyControlViewModel(pipeName)
    {
        CustomSceneCatalogPath = cardPath,
        CustomSceneQueuePath = queuePath,
        SelectedOutputGroup = "main"
    };
    Check(await viewModel.ConnectAsync(TimeSpan.FromSeconds(5)),
        "Failure fixture could not complete the control connection.");
    Check(viewModel.IsConnected, "Failure fixture must be connected before the scene command.");
    return viewModel;
}

static void CheckPersistedUpsert(string queuePath, string sceneId, string name,
                                 string irReference)
{
    var queue = new CustomSceneSyncQueueV1();
    Check(queue.TryLoad(queuePath, out var dropped, out var error) &&
          dropped == 0 && error.Length == 0 && queue.Operations.Count == 1,
        "A failed connected upsert must be durably queued.");
    var operation = queue.Operations[0];
    Check(operation.IsUpsert && operation.SceneId == sceneId &&
          operation.Name == name && operation.OutputGroup == "main" &&
          operation.IrReference == irReference,
        "The persisted upsert must retain its exact replay payload.");
}

static void CheckPersistedRemove(string queuePath, string sceneId)
{
    var queue = new CustomSceneSyncQueueV1();
    Check(queue.TryLoad(queuePath, out var dropped, out var error) &&
          dropped == 0 && error.Length == 0 && queue.Operations.Count == 1,
        "A failed connected remove must be durably queued.");
    var operation = queue.Operations[0];
    Check(!operation.IsUpsert && operation.SceneId == sceneId &&
          operation.Name.Length == 0 && operation.OutputGroup.Length == 0 &&
          operation.IrReference.Length == 0,
        "The persisted remove must retain an empty remove payload.");
}

var root = Path.Combine(Path.GetTempPath(), $"hibiki-scene-failure-{Guid.NewGuid():N}");
Directory.CreateDirectory(root);
var cancellationTokenSource = new CancellationTokenSource();
var pipeName = $"HibikiDSP_scene_failure_{Guid.NewGuid():N}";
var connected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
var serverTask = RunErrorServerAsync(pipeName, connected, cancellationTokenSource.Token);

try
{
    var addCardPath = Path.Combine(root, "add-cards.json");
    var addQueuePath = Path.Combine(root, "add-queue.json");
    var addViewModel = await ConnectFailingViewModelAsync(pipeName, addCardPath, addQueuePath);
    const string addId = "connected-add-failure";
    const string addName = "連線失敗新增";
    const string addIrReference = "failure-ref";
    Check(!await addViewModel.AddCustomSceneAsync(new SceneCard(
            addId, addName, "命令失敗仍可恢復", "零額外緩衝", true,
            addIrReference)),
        "A connected Error must not report the scene add as synchronized.");
    Check(addViewModel.CustomSceneCards.Any(scene => scene.Id == addId) &&
          addViewModel.PendingSceneCatalogOpsCount == 1 &&
          addViewModel.StatusText.Contains("同步未完成", StringComparison.Ordinal) &&
          !addViewModel.StatusText.Contains("引擎已同步", StringComparison.Ordinal),
        "A failed connected add must retain the card and expose deferred sync.");
    CheckPersistedUpsert(addQueuePath, addId, addName, addIrReference);

    var addReader = new EasyControlViewModel
    {
        CustomSceneCatalogPath = addCardPath,
        CustomSceneQueuePath = addQueuePath
    };
    Check(addReader.LoadCustomScenes(out _) &&
          addReader.CustomSceneCards.Any(scene => scene.Id == addId) &&
          addReader.PendingSceneCatalogOpsCount == 1,
        "A restarted ViewModel must recover a failed connected upsert.");
    await addViewModel.DisconnectAsync();

    var removeCardPath = Path.Combine(root, "remove-cards.json");
    var removeQueuePath = Path.Combine(root, "remove-queue.json");
    var removeViewModel = new EasyControlViewModel
    {
        CustomSceneCatalogPath = removeCardPath,
        CustomSceneQueuePath = removeQueuePath
    };
    const string removeId = "connected-remove-failure";
    var removeScene = new SceneCard(removeId, "連線失敗移除", "命令失敗仍保留", "零額外緩衝", true);
    Check(removeViewModel.UpsertCustomScene(removeScene) &&
          removeViewModel.SaveCustomScenes(out _) &&
          removeViewModel.SelectScene(removeId),
        "The connected remove fixture could not prepare its card.");
    removeViewModel = await ConnectFailingViewModelAsync(pipeName, removeCardPath, removeQueuePath);
    // The connection fixture is intentionally separate from the card setup;
    // restore the saved card into this connected ViewModel before removing it.
    Check(removeViewModel.LoadCustomScenes(out _) && removeViewModel.SelectScene(removeId),
        "The connected remove fixture could not load its card.");
    Check(!await removeViewModel.RemoveCustomSceneAsync(removeId),
        "A connected Error must not report the scene remove as synchronized.");
    Check(removeViewModel.CustomSceneCards.Count == 0 &&
          removeViewModel.PendingSceneCatalogOpsCount == 1 &&
          removeViewModel.StatusText.Contains("同步未完成", StringComparison.Ordinal) &&
          !removeViewModel.StatusText.Contains("引擎已同步", StringComparison.Ordinal),
        "A failed connected remove must retain a deferred operation.");
    CheckPersistedRemove(removeQueuePath, removeId);
    var removeReader = new EasyControlViewModel
    {
        CustomSceneCatalogPath = removeCardPath,
        CustomSceneQueuePath = removeQueuePath
    };
    Check(removeReader.LoadCustomScenes(out _) &&
          removeReader.CustomSceneCards.Count == 0 &&
          removeReader.PendingSceneCatalogOpsCount == 1,
        "A restarted ViewModel must recover a failed connected remove.");
    await removeViewModel.DisconnectAsync();

    var rollbackCardPath = Path.Combine(root, "rollback-cards.json");
    var blockedPath = Path.Combine(root, "blocked-queue-parent");
    File.WriteAllText(blockedPath, "not a directory");
    var rollbackQueuePath = Path.Combine(blockedPath, "queue.json");
    var rollbackViewModel = new EasyControlViewModel
    {
        CustomSceneCatalogPath = rollbackCardPath,
        CustomSceneQueuePath = rollbackQueuePath
    };
    const string rollbackId = "connected-remove-rollback";
    var rollbackScene = new SceneCard(rollbackId, "回復移除", "佇列保存失敗", "零額外緩衝", true);
    Check(rollbackViewModel.UpsertCustomScene(rollbackScene) &&
          rollbackViewModel.SaveCustomScenes(out _) &&
          rollbackViewModel.SelectScene(rollbackId),
        "The queue-failure rollback fixture could not prepare its card.");
    rollbackViewModel = await ConnectFailingViewModelAsync(pipeName, rollbackCardPath, rollbackQueuePath);
    Check(rollbackViewModel.LoadCustomScenes(out _) && rollbackViewModel.SelectScene(rollbackId),
        "The queue-failure rollback fixture could not load its card.");
    Check(!await rollbackViewModel.RemoveCustomSceneAsync(rollbackId) &&
          rollbackViewModel.CustomSceneCards.Count == 1 &&
          rollbackViewModel.SelectedScene?.Id == rollbackId &&
          rollbackViewModel.PendingSceneCatalogOpsCount == 0 &&
          rollbackViewModel.StatusText.Contains("同步佇列保存失敗", StringComparison.Ordinal),
        "Queue persistence failure must restore the card, selection, and empty queue.");
    var rollbackReader = new EasyControlViewModel
    {
        CustomSceneCatalogPath = rollbackCardPath,
        CustomSceneQueuePath = rollbackQueuePath
    };
    Check(rollbackReader.LoadCustomScenes(out _) &&
          rollbackReader.CustomSceneCards.Count == 1 &&
          rollbackReader.CustomSceneCards[0].Id == rollbackId &&
          rollbackReader.PendingSceneCatalogOpsCount == 0,
        "Queue persistence rollback must also survive a restart.");
    await rollbackViewModel.DisconnectAsync();
}
finally
{
    cancellationTokenSource.Cancel();
    try { await serverTask; } catch (OperationCanceledException) { }
    cancellationTokenSource.Dispose();
    if (Directory.Exists(root)) Directory.Delete(root, recursive: true);
}

Console.WriteLine("Connected scene synchronization failure checks passed.");

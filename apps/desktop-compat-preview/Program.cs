// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using System.Globalization;
using Hibiki.ControlModel;

namespace Hibiki.DesktopPreview;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        var viewModel = new EasyControlViewModel();
        viewModel.LoadCustomScenes(out _);
        viewModel.LoadRouteRules(out _);
        Application.Run(new PreviewForm(viewModel));
    }
}

internal sealed class PreviewForm : Form
{
    private readonly EasyControlViewModel _viewModel;
    private readonly Label _connection = new() { AutoSize = true };
    private readonly Label _devices = new() { AutoSize = false, Width = 550, Height = 48 };
    private readonly Label _status = new() { AutoSize = false, Height = 48 };
    private readonly Label _routes = new() { AutoSize = false, Width = 550, Height = 58 };
    private readonly Label _sessions = new() { AutoSize = false, Width = 550, Height = 72 };
    private readonly ComboBox _sessionSelector = new() { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取 Expert App" };
    private readonly TrackBar _sessionVolume = new() { Minimum = -60, Maximum = 0, TickFrequency = 5, Width = 460, AccessibleName = "選取 App 音量分貝" };
    private readonly Button _applySessionVolume = new() { Text = "套用選取 App 音量", AutoSize = true, AccessibleName = "套用選取 App 音量" };
    private readonly TextBox _sessionLane = new() { Width = 220, PlaceholderText = "Lane ID", AccessibleName = "Lane ID 輸入欄" };
    private readonly TextBox _sessionOutput = new() { Width = 220, PlaceholderText = "Output Group", AccessibleName = "Output Group 輸入欄" };
    private readonly Button _applySessionRoute = new() { Text = "套用選取 App 路由", AutoSize = true, AccessibleName = "套用選取 App 路由" };
    private readonly ListBox _routeRuleList = new() { Width = 550, Height = 110, AccessibleName = "App 路由預設列表" };
    private readonly TextBox _routeRuleId = new() { Width = 260, PlaceholderText = "預設 ID（小寫英文／數字／- _ .）", AccessibleName = "App 路由預設 ID" };
    private readonly TextBox _routeRuleAppId = new() { Width = 260, PlaceholderText = "App ID（例如 game.exe，可留空）", AccessibleName = "App 路由預設 App ID" };
    private readonly TextBox _routeRuleDisplayName = new() { Width = 260, PlaceholderText = "顯示名稱（可留空）", AccessibleName = "App 路由預設顯示名稱" };
    private readonly TextBox _routeRuleLaneId = new() { Width = 260, PlaceholderText = "Lane ID", AccessibleName = "App 路由預設 Lane ID" };
    private readonly TextBox _routeRuleOutputGroup = new() { Width = 260, PlaceholderText = "Output Group（main／low-latency／surround）", AccessibleName = "App 路由預設 Output Group" };
    private readonly NumericUpDown _routeRulePriority = new() { Minimum = -1000000, Maximum = 1000000, Value = 0, Width = 160, AccessibleName = "App 路由預設優先級" };
    private readonly NumericUpDown _routeRuleMakeupGain = new() { Minimum = -144, Maximum = 12, DecimalPlaces = 1, Increment = 0.5M, Width = 160, AccessibleName = "App 路由預設補償增益分貝" };
    private readonly CheckBox _routeRuleEnabled = new() { Text = "啟用預設", AutoSize = true, Checked = true, AccessibleName = "啟用 App 路由預設" };
    private readonly ComboBox _routeRuleGainOwner = new() { Width = 220, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "App 路由預設增益控制者" };
    private readonly Button _applyRouteRule = new() { Text = "新增／更新預設", AutoSize = true, AccessibleName = "新增或更新 App 路由預設" };
    private readonly Button _removeRouteRule = new() { Text = "移除選取預設", AutoSize = true, AccessibleName = "移除選取的 App 路由預設" };
    private readonly Button _clearRouteRules = new() { Text = "清除全部預設", AutoSize = true, AccessibleName = "清除全部 App 路由預設" };
    private readonly Label _effective = new() { AutoSize = true };
    private readonly ComboBox _scenes = new() { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取情境設定檔" };
    private readonly TextBox _customSceneId = new() { Width = 220, PlaceholderText = "Scene ID", AccessibleName = "自訂場景 ID" };
    private readonly TextBox _customSceneName = new() { Width = 220, PlaceholderText = "名稱", AccessibleName = "自訂場景名稱" };
    private readonly TextBox _customSceneDescription = new() { Width = 460, PlaceholderText = "說明", AccessibleName = "自訂場景說明" };
    private readonly Button _addCustomScene = new() { Text = "加入自訂場景", AutoSize = true, AccessibleName = "加入自訂場景" };
    private readonly Button _removeCustomScene = new() { Text = "移除選取的自訂場景", AutoSize = true, AccessibleName = "移除選取的自訂場景" };
    private readonly ComboBox _irModes = new() { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取 IR 模式" };
    private readonly TrackBar _irStrength = new() { Minimum = 0, Maximum = 100, TickFrequency = 10, Width = 460, AccessibleName = "IR 強度百分比" };
    private readonly Label _irStatus = new() { AutoSize = false, Width = 550, Height = 58 };
    private readonly Button _loadIr = new() { Text = "載入 IR WAV 並準備", AutoSize = true, AccessibleName = "載入 IR WAV 並準備" };
    private readonly TrackBar _volume = new() { Minimum = -60, Maximum = 0, TickFrequency = 5, Width = 460, AccessibleName = "主音量分貝" };
    private readonly Button _enhance = new() { Text = "一鍵改善", AutoSize = true, Margin = new Padding(3, 12, 3, 3), AccessibleName = "一鍵改善" };
    private readonly ComboBox _vst3TimelineSelector = new() { Width = 220, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取 VST3 時間軸" };
    private readonly TextBox _vst3NewTimelineId = new() { Width = 180, PlaceholderText = "新時間軸 ID", AccessibleName = "新時間軸 ID" };
    private readonly Button _vst3Register = new() { Text = "註冊", AutoSize = true, AccessibleName = "註冊新時間軸" };
    private readonly ListBox _vst3RowList = new() { Width = 550, Height = 110, AccessibleName = "VST3 時間軸事件列表" };
    private readonly TextBox _vst3ParamId = new() { Width = 100, PlaceholderText = "參數 ID", AccessibleName = "新事件參數 ID" };
    private readonly TextBox _vst3Position = new() { Width = 140, PlaceholderText = "取樣位置", AccessibleName = "新事件取樣位置" };
    private readonly TextBox _vst3Value = new() { Width = 100, PlaceholderText = "值 0–1", AccessibleName = "新事件正規化值" };
    private readonly Button _vst3Upsert = new() { Text = "加入事件", AutoSize = true, AccessibleName = "加入時間軸事件" };
    private readonly TextBox _vst3RowValue = new() { Width = 120, PlaceholderText = "更新值", AccessibleName = "修改選取列的值" };
    private readonly Button _vst3SetRowValue = new() { Text = "更新選取列", AutoSize = true, AccessibleName = "更新選取列數值" };
    private readonly Button _vst3RemoveRow = new() { Text = "刪除選取列", AutoSize = true, AccessibleName = "刪除選取列" };
    private readonly Button _vst3BeginEdit = new() { Text = "開始草稿", AutoSize = true, AccessibleName = "開始時間軸草稿" };
    private readonly Button _vst3Commit = new() { Text = "提交草稿", AutoSize = true, AccessibleName = "提交時間軸草稿" };
    private readonly Button _vst3Discard = new() { Text = "捨棄草稿", AutoSize = true, AccessibleName = "捨棄時間軸草稿" };
    private readonly Button _vst3Undo = new() { Text = "復原", AutoSize = true, AccessibleName = "復原時間軸操作" };
    private readonly Button _vst3Redo = new() { Text = "重做", AutoSize = true, AccessibleName = "重做時間軸操作" };
    private readonly Button _vst3ClearHistory = new() { Text = "清除歷史", AutoSize = true, AccessibleName = "清除時間軸編輯歷史" };
    private readonly Button _vst3SaveBaseline = new() { Text = "保存基準", AutoSize = true, AccessibleName = "保存時間軸基準" };
    private readonly Button _vst3RemoveTimeline = new() { Text = "刪除時間軸", AutoSize = true, AccessibleName = "刪除目前時間軸" };
    private readonly Label _vst3Status = new() { AutoSize = false, Width = 550, Height = 48 };
    private readonly System.Windows.Forms.Timer _statusTimer = new() { Interval = 1000 };
    private bool _updatingScene;
    private bool _updatingSession;
    private bool _updatingRouteRules;
    private bool _statusRefreshActive;
    private bool _updatingVst3;

    private sealed record Vst3TimelineRowItem(int Index, string Display);

    internal PreviewForm(EasyControlViewModel viewModel)
    {
        _viewModel = viewModel;
        Text = "Hibiki DSP — Compatibility Preview";
        ClientSize = new Size(620, 540);
        StartPosition = FormStartPosition.CenterScreen;
        Font = new Font("Segoe UI", 10);

        var panel = new FlowLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(24), FlowDirection = FlowDirection.TopDown, WrapContents = false, AutoScroll = true };
        panel.Controls.Add(new Label { Text = "Hibiki DSP", AutoSize = true, Font = new Font("Segoe UI", 24, FontStyle.Bold) });
        panel.Controls.Add(new Label { Text = "本機 Compatibility Preview：自帶 .NET runtime，不需要 Windows App Runtime；不含 driver、系統攔截或正式音訊處理。", AutoSize = false, Width = 550, Height = 42 });
        panel.Controls.Add(new Label { Text = "離線預覽模式：先啟動 user-space Engine Preview，才能測試場景或音量命令。", AutoSize = true, ForeColor = Color.DimGray });
        panel.Controls.Add(new Label { Text = "輸出群組", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        var groups = new ComboBox { Width = 460, DropDownStyle = ComboBoxStyle.DropDownList, AccessibleName = "選取輸出群組", DataSource = _viewModel.OutputGroups.ToList(), DisplayMember = "Name", ValueMember = "Id" };
        groups.SelectedIndexChanged += (_, _) => { if (groups.SelectedValue is string id) _viewModel.SelectedOutputGroup = id; };
        panel.Controls.Add(groups);
        panel.Controls.Add(new Label { Text = "本機裝置 catalog（僅 metadata）", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        panel.Controls.Add(_devices);
        var connect = new Button { Text = "嘗試連接已啟動的 Hibiki 引擎", AutoSize = true, Margin = new Padding(3, 12, 3, 3), AccessibleName = "嘗試連接已啟動的 Hibiki 引擎" };
        connect.Click += async (_, _) => { await _viewModel.ConnectAsync(TimeSpan.FromSeconds(3)); RefreshView(); };
        panel.Controls.Add(connect);
        panel.Controls.Add(_connection);
        panel.Controls.Add(new Label { Text = "場景", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        SyncSceneList();
        _scenes.SelectedIndexChanged += async (_, _) =>
        {
            if (_updatingScene || _scenes.SelectedValue is not string id || !_viewModel.IsConnected)
                return;
            await _viewModel.SelectSceneAsync(id);
            RefreshView();
        };
        panel.Controls.Add(_scenes);
        panel.Controls.Add(new Label { Text = "自訂場景（最多 32 筆）", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        _customSceneId.TextChanged += (_, _) => _viewModel.CustomSceneId = _customSceneId.Text;
        _customSceneName.TextChanged += (_, _) => _viewModel.CustomSceneName = _customSceneName.Text;
        _customSceneDescription.TextChanged += (_, _) => _viewModel.CustomSceneDescription = _customSceneDescription.Text;
        var customSceneIdentity = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        customSceneIdentity.Controls.Add(_customSceneId);
        customSceneIdentity.Controls.Add(_customSceneName);
        panel.Controls.Add(customSceneIdentity);
        panel.Controls.Add(_customSceneDescription);
        _addCustomScene.Click += (_, _) =>
        {
            _viewModel.AddCustomScene();
            SyncSceneList();
            RefreshView();
        };
        _removeCustomScene.Click += (_, _) =>
        {
            if (_scenes.SelectedValue is string sceneId)
                _viewModel.RemoveCustomScene(sceneId);
            SyncSceneList();
            RefreshView();
        };
        var customSceneActions = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        customSceneActions.Controls.Add(_addCustomScene);
        customSceneActions.Controls.Add(_removeCustomScene);
        panel.Controls.Add(customSceneActions);
        panel.Controls.Add(new Label { Text = "IR 檔案與相位", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        _irModes.DataSource = _viewModel.IrPhaseModeOptions.ToList();
        _irModes.DisplayMember = "Label";
        _irModes.ValueMember = "Mode";
        _irModes.SelectedValueChanged += (_, _) =>
        {
            if (_irModes.SelectedValue is IrPhaseMode mode)
                _viewModel.IrPhaseMode = mode;
            RefreshView();
        };
        _irStrength.ValueChanged += (_, _) =>
        {
            _viewModel.IrPhaseStrength = _irStrength.Value / 100.0;
            RefreshView();
        };
        panel.Controls.Add(_irModes);
        panel.Controls.Add(_irStrength);
        _loadIr.Click += async (_, _) =>
        {
            using var dialog = new OpenFileDialog
            {
                Filter = "IR WAV 檔案 (*.wav)|*.wav|所有檔案 (*.*)|*.*",
                CheckFileExists = true,
                Multiselect = false,
                Title = "選擇 IR WAV"
            };
            if (dialog.ShowDialog(this) == DialogResult.OK)
            {
                await _viewModel.PrepareIrAsync(dialog.FileName);
                RefreshView();
            }
        };
        panel.Controls.Add(_loadIr);
        panel.Controls.Add(_irStatus);
        _enhance.Click += async (_, _) => { await _viewModel.OneTapEnhanceAsync(); RefreshView(); };
        panel.Controls.Add(_enhance);
        panel.Controls.Add(new Label { Text = "系統音量（dB）", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        _volume.Value = (int)_viewModel.RequestedVolumeDb;
        _volume.ValueChanged += async (_, _) => { _viewModel.RequestedVolumeDb = _volume.Value; if (_viewModel.IsConnected) await _viewModel.QueueVolumeAsync(); RefreshView(); };
        panel.Controls.Add(_volume);
        panel.Controls.Add(_effective);
        panel.Controls.Add(_routes);
        panel.Controls.Add(new Label { Text = "Expert App／工作階段（需以 -EnableSessionRouting 啟動）", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        panel.Controls.Add(_sessions);
        _sessionSelector.SelectedIndexChanged += (_, _) =>
        {
            if (_updatingSession || _sessionSelector.SelectedItem is not SessionCatalogEntryV1 entry) return;
            _viewModel.SelectSession(entry.Handle);
            SyncSessionControls();
            RefreshView();
        };
        panel.Controls.Add(_sessionSelector);
        _sessionVolume.ValueChanged += async (_, _) =>
        {
            if (_updatingSession || _viewModel.SelectedSession is null) return;
            _viewModel.SessionVolumeDb = _sessionVolume.Value;
            RefreshView();
        };
        panel.Controls.Add(_sessionVolume);
        _applySessionVolume.Click += async (_, _) =>
        {
            await _viewModel.ApplySelectedSessionVolumeAsync();
            RefreshView();
        };
        panel.Controls.Add(_applySessionVolume);
        _sessionLane.TextChanged += (_, _) =>
        {
            if (!_updatingSession) _viewModel.SessionRouteLaneId = _sessionLane.Text;
        };
        _sessionOutput.TextChanged += (_, _) =>
        {
            if (!_updatingSession) _viewModel.SessionRouteOutputGroup = _sessionOutput.Text;
        };
        var sessionRouteFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        sessionRouteFields.Controls.Add(_sessionLane);
        sessionRouteFields.Controls.Add(_sessionOutput);
        panel.Controls.Add(sessionRouteFields);
        _applySessionRoute.Click += async (_, _) =>
        {
            await _viewModel.ApplySelectedSessionRouteAsync();
            RefreshView();
        };
        panel.Controls.Add(_applySessionRoute);
        panel.Controls.Add(new Label { Text = "App 路由預設（Expert）", AutoSize = true, Margin = new Padding(3, 18, 3, 0) });
        panel.Controls.Add(new Label
        {
            Text = "建立後會保存到本機；只有 App 清單已同步且引擎回覆 Ack，才會顯示為已套用。App ID 或顯示名稱至少填一項。",
            AutoSize = false,
            Width = 550,
            Height = 42
        });
        SyncRouteRuleList();
        panel.Controls.Add(_routeRuleList);
        _routeRuleId.TextChanged += (_, _) => _viewModel.RouteRuleId = _routeRuleId.Text;
        _routeRuleAppId.TextChanged += (_, _) => _viewModel.RouteRuleAppId = _routeRuleAppId.Text;
        _routeRuleDisplayName.TextChanged += (_, _) => _viewModel.RouteRuleDisplayName = _routeRuleDisplayName.Text;
        _routeRuleLaneId.TextChanged += (_, _) => _viewModel.RouteRuleLaneId = _routeRuleLaneId.Text;
        _routeRuleOutputGroup.TextChanged += (_, _) => _viewModel.RouteRuleOutputGroup = _routeRuleOutputGroup.Text;
        _routeRulePriority.ValueChanged += (_, _) => _viewModel.RouteRulePriority = (int)_routeRulePriority.Value;
        _routeRuleMakeupGain.ValueChanged += (_, _) => _viewModel.RouteRuleMakeupGainDb = (double)_routeRuleMakeupGain.Value;
        _routeRuleEnabled.CheckedChanged += (_, _) => _viewModel.RouteRuleEnabled = _routeRuleEnabled.Checked;
        _routeRuleGainOwner.DataSource = _viewModel.RouteRuleGainOwners.ToList();
        _routeRuleGainOwner.SelectedIndexChanged += (_, _) =>
        {
            if (_updatingRouteRules || _routeRuleGainOwner.SelectedItem is not SessionRouteRuleGainOwnerV1 owner) return;
            _viewModel.RouteRuleGainOwner = owner;
        };
        _updatingRouteRules = true;
        try
        {
            _routeRuleGainOwner.SelectedItem = _viewModel.RouteRuleGainOwner;
        }
        finally
        {
            _updatingRouteRules = false;
        }
        var routeRuleIdentityFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        routeRuleIdentityFields.Controls.Add(_routeRuleId);
        routeRuleIdentityFields.Controls.Add(_routeRuleAppId);
        panel.Controls.Add(routeRuleIdentityFields);
        var routeRuleMatcherFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        routeRuleMatcherFields.Controls.Add(_routeRuleDisplayName);
        routeRuleMatcherFields.Controls.Add(_routeRuleLaneId);
        panel.Controls.Add(routeRuleMatcherFields);
        panel.Controls.Add(_routeRuleOutputGroup);
        var routeRuleNumericFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        routeRuleNumericFields.Controls.Add(_routeRulePriority);
        routeRuleNumericFields.Controls.Add(_routeRuleMakeupGain);
        panel.Controls.Add(routeRuleNumericFields);
        var routeRuleOptionFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        routeRuleOptionFields.Controls.Add(_routeRuleEnabled);
        routeRuleOptionFields.Controls.Add(_routeRuleGainOwner);
        panel.Controls.Add(routeRuleOptionFields);
        _applyRouteRule.Click += async (_, _) =>
        {
            await _viewModel.ApplyRouteRuleAsync();
            SyncRouteRuleList();
            RefreshView();
        };
        _removeRouteRule.Click += async (_, _) =>
        {
            if (_routeRuleList.SelectedValue is string ruleId)
            {
                await _viewModel.ApplyRemoveRouteRuleAsync(ruleId);
                SyncRouteRuleList();
            }
            RefreshView();
        };
        _clearRouteRules.Click += async (_, _) =>
        {
            await _viewModel.ApplyClearRouteRulesAsync();
            SyncRouteRuleList();
            RefreshView();
        };
        var routeRuleActions = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        routeRuleActions.Controls.Add(_applyRouteRule);
        routeRuleActions.Controls.Add(_removeRouteRule);
        routeRuleActions.Controls.Add(_clearRouteRules);
        panel.Controls.Add(routeRuleActions);
        _routeRuleList.SelectedIndexChanged += (_, _) =>
        {
            if (_updatingRouteRules) return;
            RefreshView();
        };
        panel.Controls.Add(new Label { Text = "VST3 時間軸編輯器（本機草稿）", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        var vst3IdentityActions = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        vst3IdentityActions.Controls.Add(_vst3NewTimelineId);
        vst3IdentityActions.Controls.Add(_vst3Register);
        vst3IdentityActions.Controls.Add(_vst3BeginEdit);
        vst3IdentityActions.Controls.Add(_vst3RemoveTimeline);
        panel.Controls.Add(vst3IdentityActions);
        var vst3HistoryActions = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        vst3HistoryActions.Controls.Add(_vst3Commit);
        vst3HistoryActions.Controls.Add(_vst3Discard);
        vst3HistoryActions.Controls.Add(_vst3Undo);
        vst3HistoryActions.Controls.Add(_vst3Redo);
        vst3HistoryActions.Controls.Add(_vst3ClearHistory);
        vst3HistoryActions.Controls.Add(_vst3SaveBaseline);
        panel.Controls.Add(vst3HistoryActions);
        panel.Controls.Add(_vst3TimelineSelector);
        panel.Controls.Add(new Label { Text = "事件列", AutoSize = true, Margin = new Padding(3, 12, 3, 0) });
        panel.Controls.Add(_vst3RowList);
        var vst3EventFields = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        vst3EventFields.Controls.Add(_vst3ParamId);
        vst3EventFields.Controls.Add(_vst3Position);
        vst3EventFields.Controls.Add(_vst3Value);
        panel.Controls.Add(vst3EventFields);
        var vst3RowActions = new FlowLayoutPanel
        {
            AutoSize = true,
            FlowDirection = FlowDirection.LeftToRight,
            WrapContents = false
        };
        vst3RowActions.Controls.Add(_vst3Upsert);
        vst3RowActions.Controls.Add(_vst3RowValue);
        vst3RowActions.Controls.Add(_vst3SetRowValue);
        vst3RowActions.Controls.Add(_vst3RemoveRow);
        panel.Controls.Add(vst3RowActions);
        panel.Controls.Add(_vst3Status);

        _vst3NewTimelineId.TextChanged += (_, _) => _viewModel.Vst3TimelineEditor.NewTimelineIdText = _vst3NewTimelineId.Text;
        _vst3ParamId.TextChanged += (_, _) => _viewModel.Vst3TimelineEditor.NewParameterIdText = _vst3ParamId.Text;
        _vst3Position.TextChanged += (_, _) => _viewModel.Vst3TimelineEditor.NewPositionText = _vst3Position.Text;
        _vst3Value.TextChanged += (_, _) => _viewModel.Vst3TimelineEditor.NewValueText = _vst3Value.Text;
        _vst3RowValue.TextChanged += (_, _) => _viewModel.Vst3TimelineEditor.SelectedRowValueText = _vst3RowValue.Text;
        _vst3Register.Click += (_, _) =>
        {
            _viewModel.Vst3TimelineEditor.RegisterTimeline(_viewModel.Vst3TimelineEditor.NewTimelineIdText);
            _viewModel.Vst3TimelineEditor.NewTimelineIdText = string.Empty;
            SyncVst3Controls();
        };
        _vst3BeginEdit.Click += (_, _) => RunVst3Action(() => _viewModel.Vst3TimelineEditor.BeginEdit());
        _vst3Commit.Click += (_, _) => RunVst3Action(() => _viewModel.Vst3TimelineEditor.Commit());
        _vst3Discard.Click += (_, _) => RunVst3Action(() => _viewModel.Vst3TimelineEditor.Discard());
        _vst3Undo.Click += (_, _) => RunVst3Action(() => _viewModel.Vst3TimelineEditor.Undo());
        _vst3Redo.Click += (_, _) => RunVst3Action(() => _viewModel.Vst3TimelineEditor.Redo());
        _vst3ClearHistory.Click += (_, _) => RunVst3Action(() => _viewModel.Vst3TimelineEditor.ClearHistory());
        _vst3SaveBaseline.Click += (_, _) => RunVst3Action(() => _viewModel.Vst3TimelineEditor.SaveSelected());
        _vst3Upsert.Click += (_, _) => RunVst3Action(() => _viewModel.Vst3TimelineEditor.UpsertFromFields());
        _vst3SetRowValue.Click += (_, _) =>
        {
            _viewModel.Vst3TimelineEditor.SetSelectedRowValue(_viewModel.Vst3TimelineEditor.SelectedRowValueText);
            _viewModel.Vst3TimelineEditor.SelectedRowValueText = string.Empty;
            SyncVst3Controls();
        };
        _vst3RemoveRow.Click += (_, _) => RunVst3Action(() => _viewModel.Vst3TimelineEditor.RemoveSelectedRow());
        _vst3RemoveTimeline.Click += (_, _) => RunVst3Action(() => _viewModel.Vst3TimelineEditor.RemoveSelectedTimeline());
        _vst3TimelineSelector.SelectedValueChanged += (_, _) =>
        {
            if (!_updatingVst3 && _vst3TimelineSelector.SelectedValue is string id)
                RunVst3Action(() => _viewModel.Vst3TimelineEditor.Select(id));
        };
        _vst3RowList.SelectedIndexChanged += (_, _) =>
        {
            if (_updatingVst3) return;
            if (_vst3RowList.SelectedItem is Vst3TimelineRowItem row)
            {
                _viewModel.Vst3TimelineEditor.SelectedRowIndex = row.Index;
                SyncVst3Controls();
            }
        };
        panel.Controls.Add(_status);
        Controls.Add(panel);
        _viewModel.PropertyChanged += OnViewModelChanged;
        _statusTimer.Tick += async (_, _) =>
        {
            if (!_viewModel.IsConnected || _viewModel.IsBusy || _statusRefreshActive) return;
            _statusRefreshActive = true;
            try
            {
                await _viewModel.RefreshControlStatusAsync();
                RefreshView();
            }
            finally
            {
                _statusRefreshActive = false;
            }
        };
        _statusTimer.Start();
        FormClosed += async (_, _) =>
        {
            _statusTimer.Stop();
            _statusTimer.Dispose();
            await _viewModel.DisconnectAsync();
        };
        Shown += async (_, _) =>
        {
            // The compatibility preview is deliberately useful as a single
            // double-click experience when the local Engine Preview is already
            // running, while still remaining safe when no engine is present.
            await _viewModel.ConnectAsync(TimeSpan.FromSeconds(1));
            RefreshView();
        };
        RefreshView();
    }

    private void OnViewModelChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (IsDisposed || !IsHandleCreated) return;
        if (e.PropertyName == nameof(EasyControlViewModel.RouteRules))
        {
            BeginInvoke(SyncRouteRuleList);
            return;
        }
        BeginInvoke(RefreshView);
    }

    private void RunVst3Action(Func<bool> action)
    {
        action();
        SyncVst3Controls();
        RefreshView();
    }

    private void RefreshView()
    {
        _connection.Text = _viewModel.ConnectionStatusText;
        var renderDevices = _viewModel.PhysicalDevices
            .Where(device => device.Flow == PhysicalDeviceFlowV1.Render)
            .ToArray();
        var captureDevices = _viewModel.PhysicalDevices
            .Where(device => device.Flow == PhysicalDeviceFlowV1.Capture)
            .ToArray();
        var defaultRender = renderDevices.FirstOrDefault(device => device.IsDefault)?.DisplayName;
        _devices.Text = _viewModel.PhysicalDevices.Count == 0
            ? "裝置 catalog：尚未收到引擎快照；不會自行枚舉或建立裝置。"
            : $"裝置 catalog：{_viewModel.PhysicalDevices.Count} 筆（render {renderDevices.Length}／capture {captureDevices.Length}）；" +
              $"預設輸出：{defaultRender ?? "未指定"}\r\n只顯示 metadata；physical sink 與實體切換尚未啟用。";
        _enhance.Enabled = _viewModel.IsConnected;
        _scenes.Enabled = _viewModel.IsConnected;
        _volume.Enabled = _viewModel.IsConnected;
        var sessions = _viewModel.SessionCatalog.ToArray();
        _sessions.Text = sessions.Length == 0
            ? "App catalog：尚未同步；請以 -EnableSessionRouting 啟動引擎，或目前沒有可控制的工作階段。"
            : $"App catalog：{sessions.Length} 筆；只顯示 bounded metadata。套用 App 音量會寫入 Windows session，" +
              "實體 per-App 重新送出仍未驗證。";
        _updatingSession = true;
        try
        {
            var selectedHandle = _viewModel.SelectedSessionHandle;
            _sessionSelector.DataSource = null;
            _sessionSelector.DisplayMember = nameof(SessionCatalogEntryV1.DisplayName);
            _sessionSelector.ValueMember = nameof(SessionCatalogEntryV1.Handle);
            _sessionSelector.DataSource = sessions;
            if (selectedHandle != 0UL)
            {
                var selectedIndex = Array.FindIndex(sessions, entry => entry.Handle == selectedHandle);
                if (selectedIndex >= 0) _sessionSelector.SelectedIndex = selectedIndex;
            }
            SyncSessionControls();
        }
        finally
        {
            _updatingSession = false;
        }
        var hasSession = _viewModel.HasSelectedSession;
        _sessionSelector.Enabled = _viewModel.IsConnected && sessions.Length > 0;
        _sessionVolume.Enabled = _viewModel.IsConnected && hasSession && _viewModel.SelectedSession?.VolumeAvailable == true;
        _applySessionVolume.Enabled = _sessionVolume.Enabled;
        _sessionLane.Enabled = _viewModel.IsConnected && hasSession;
        _sessionOutput.Enabled = _viewModel.IsConnected && hasSession;
        _applySessionRoute.Enabled = _viewModel.IsConnected && hasSession;
        _removeRouteRule.Enabled = _viewModel.RouteRules.Count > 0 && _routeRuleList.SelectedValue is string;
        _clearRouteRules.Enabled = _viewModel.RouteRules.Count > 0;
        _loadIr.Enabled = _viewModel.IsConnected && _viewModel.IrPhaseMode != IrPhaseMode.Bypass;
        _irStrength.Enabled = _viewModel.IrPhaseMode is IrPhaseMode.MixedPhase or IrPhaseMode.LinearPhase;
        _effective.Text = $"實際有效音量：{_viewModel.EffectiveVolumeDb:0.0} dB；{_viewModel.VolumeOriginText}；{_viewModel.VolumeActuatorText}";
        _routes.Text = _viewModel.Expert.RouteHealthAccessibleSummary;
        _status.Text = _viewModel.StatusText;
        _irStatus.Text = $"{_viewModel.IrPhaseModeText}；實測延遲 {_viewModel.IrAddedDelayMs:0.0} ms。\r\n{_viewModel.IrPrepareStatus}";
        var requested = Math.Clamp((int)Math.Round(_viewModel.RequestedVolumeDb), _volume.Minimum, _volume.Maximum);
        if (_volume.Value != requested) _volume.Value = requested;
        if (_irModes.SelectedValue is not IrPhaseMode currentMode || currentMode != _viewModel.IrPhaseMode)
            _irModes.SelectedValue = _viewModel.IrPhaseMode;
        var strength = Math.Clamp((int)Math.Round(_viewModel.IrPhaseStrength * 100.0),
                                  _irStrength.Minimum, _irStrength.Maximum);
        if (_irStrength.Value != strength) _irStrength.Value = strength;
        SyncVst3Controls();
        if (!string.Equals(_customSceneId.Text, _viewModel.CustomSceneId, StringComparison.Ordinal))
            _customSceneId.Text = _viewModel.CustomSceneId;
        if (!string.Equals(_customSceneName.Text, _viewModel.CustomSceneName, StringComparison.Ordinal))
            _customSceneName.Text = _viewModel.CustomSceneName;
        if (!string.Equals(_customSceneDescription.Text, _viewModel.CustomSceneDescription, StringComparison.Ordinal))
            _customSceneDescription.Text = _viewModel.CustomSceneDescription;
        _removeCustomScene.Enabled = _scenes.SelectedValue is string removableSceneId &&
                                     _viewModel.CustomSceneCards.Any(item => item.Id == removableSceneId);
        var selectedScene = _viewModel.SelectedScene?.Id;
        if (selectedScene is not null && !string.Equals(_scenes.SelectedValue as string, selectedScene,
                                                         StringComparison.Ordinal))
        {
            _updatingScene = true;
            _scenes.SelectedValue = selectedScene;
            _updatingScene = false;
        }
    }

    private void SyncSceneList()
    {
        var selectedId = _scenes.SelectedValue as string ?? _viewModel.SelectedScene?.Id;
        _updatingScene = true;
        try
        {
            _scenes.DataSource = null;
            _scenes.DataSource = _viewModel.Scenes.ToList();
            _scenes.DisplayMember = nameof(SceneCard.Name);
            _scenes.ValueMember = nameof(SceneCard.Id);
            if (selectedId is null) return;
            var scenes = _viewModel.Scenes.ToArray();
            var selectedIndex = Array.FindIndex(scenes, item => item.Id == selectedId);
            if (selectedIndex >= 0) _scenes.SelectedIndex = selectedIndex;
        }
        finally
        {
            _updatingScene = false;
        }
    }

    private void SyncSessionControls()
    {
        var selected = _viewModel.SelectedSession;
        var requested = Math.Clamp((int)Math.Round(_viewModel.SessionVolumeDb),
                                   _sessionVolume.Minimum, _sessionVolume.Maximum);
        if (_sessionVolume.Value != requested) _sessionVolume.Value = requested;
        if (_sessionLane.Text != _viewModel.SessionRouteLaneId)
            _sessionLane.Text = _viewModel.SessionRouteLaneId;
        if (_sessionOutput.Text != _viewModel.SessionRouteOutputGroup)
            _sessionOutput.Text = _viewModel.SessionRouteOutputGroup;
        if (selected is null) _sessionSelector.SelectedIndex = -1;
    }

    private void SyncRouteRuleList()
    {
        var selectedId = _routeRuleList.SelectedValue as string;
        _updatingRouteRules = true;
        try
        {
            var rules = _viewModel.RouteRules.ToArray();
            _routeRuleList.DataSource = null;
            _routeRuleList.DisplayMember = nameof(SessionRouteRuleCard.Summary);
            _routeRuleList.ValueMember = nameof(SessionRouteRuleCard.RuleId);
            _routeRuleList.DataSource = rules;
            if (selectedId is null) return;
            var selectedIndex = Array.FindIndex(rules, item => item.RuleId == selectedId);
            if (selectedIndex >= 0) _routeRuleList.SelectedIndex = selectedIndex;
        }
        finally
        {
            _updatingRouteRules = false;
        }
    }

    private void SyncVst3Controls()
    {
        var editor = _viewModel.Vst3TimelineEditor;
        _updatingVst3 = true;
        try
        {
            var selectedId = editor.SelectedTimelineId;
            _vst3TimelineSelector.DataSource = null;
            _vst3TimelineSelector.DataSource = editor.TimelineIds.ToList();
            if (selectedId is not null) _vst3TimelineSelector.SelectedItem = selectedId;

            var selectedRowIndex = editor.SelectedRowIndex;
            _vst3RowList.DataSource = null;
            _vst3RowList.DisplayMember = nameof(Vst3TimelineRowItem.Display);
            _vst3RowList.ValueMember = nameof(Vst3TimelineRowItem.Index);
            _vst3RowList.DataSource = editor.Rows.Select(row => new Vst3TimelineRowItem(
                row.Index,
                $"#{row.Index} 參數 {row.ParameterId}｜位置 {row.SamplePosition}｜值 {row.NormalizedValue.ToString("0.0###", CultureInfo.InvariantCulture)}")).ToList();
            if (selectedRowIndex >= 0)
            {
                var selectedIndex = _vst3RowList.Items.OfType<Vst3TimelineRowItem>()
                    .ToList()
                    .FindIndex(item => item.Index == selectedRowIndex);
                if (selectedIndex >= 0) _vst3RowList.SelectedIndex = selectedIndex;
            }

            if (!string.Equals(_vst3NewTimelineId.Text, editor.NewTimelineIdText, StringComparison.Ordinal))
                _vst3NewTimelineId.Text = editor.NewTimelineIdText;
            if (!string.Equals(_vst3ParamId.Text, editor.NewParameterIdText, StringComparison.Ordinal))
                _vst3ParamId.Text = editor.NewParameterIdText;
            if (!string.Equals(_vst3Position.Text, editor.NewPositionText, StringComparison.Ordinal))
                _vst3Position.Text = editor.NewPositionText;
            if (!string.Equals(_vst3Value.Text, editor.NewValueText, StringComparison.Ordinal))
                _vst3Value.Text = editor.NewValueText;
            if (!string.Equals(_vst3RowValue.Text, editor.SelectedRowValueText, StringComparison.Ordinal))
                _vst3RowValue.Text = editor.SelectedRowValueText;

            _vst3Status.Text = editor.StatusText;
            _vst3BeginEdit.Enabled = editor.HasSelection && !editor.HasEditSession;
            _vst3Commit.Enabled = editor.HasEditSession;
            _vst3Discard.Enabled = editor.HasEditSession;
            _vst3Undo.Enabled = editor.CanUndo;
            _vst3Redo.Enabled = editor.CanRedo;
            _vst3SaveBaseline.Enabled = editor.HasSelection && !editor.HasEditSession;
            _vst3RemoveTimeline.Enabled = editor.HasSelection && !editor.HasEditSession;
            _vst3Upsert.Enabled = editor.HasEditSession;
            _vst3SetRowValue.Enabled = editor.SelectedRowIndex >= 0 && editor.HasEditSession;
            _vst3RemoveRow.Enabled = editor.SelectedRowIndex >= 0 && editor.HasEditSession;
        }
        finally
        {
            _updatingVst3 = false;
        }
    }
}

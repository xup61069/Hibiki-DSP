// SPDX-License-Identifier: GPL-3.0-only

using System.ComponentModel;
using System.Globalization;

namespace Hibiki.ControlModel;

// Observable supervisor-side view model over Vst3TimelineSurfaceModelV1
// (Issues #390/#667/#679). The WinUI Expert shell and compatibility preview bind
// selection, draft transactions, canonical row projection and derived
// dirty/undo state through this V1 surface without touching engine or IPC.
// Parsing is fail-closed with invariant culture only; every action updates one
// Traditional Chinese status line instead of throwing.
public sealed class Vst3TimelineEditorViewModelV1 : INotifyPropertyChanged
{
    public sealed record TimelineEventRow(int Index, uint ParameterId, ulong SamplePosition, double NormalizedValue);

    private readonly Vst3TimelineSurfaceModelV1 _model = new();
    private IReadOnlyList<string> _timelineIds = Array.Empty<string>();
    private IReadOnlyList<TimelineEventRow> _rows = Array.Empty<TimelineEventRow>();
    private string _statusText = "尚未選取任何時間軸";
    private int _selectedRowIndex = -1;
    private string _newTimelineIdText = string.Empty;
    private string _newParameterIdText = string.Empty;
    private string _newPositionText = string.Empty;
    private string _newValueText = string.Empty;
    private string _selectedRowValueText = string.Empty;

    public event PropertyChangedEventHandler? PropertyChanged;

    public IReadOnlyList<string> TimelineIds => _timelineIds;
    public IReadOnlyList<TimelineEventRow> Rows => _rows;
    public string StatusText => _statusText;
    public string? SelectedTimelineId
    {
        get => _model.SelectedTimelineId;
        set
        {
            if (value is null && _model.SelectedTimelineId is null)
                return;
            if (!EqualityComparer<string?>.Default.Equals(value, _model.SelectedTimelineId))
                Select(value);
        }
    }
    public bool HasSelection => _model.HasSelection;
    public bool HasEditSession => _model.HasEditSession;
    public bool IsDirty => _model.IsDirty();
    public bool CanUndo => _model.CanUndo;
    public bool CanRedo => _model.CanRedo;
    public int UndoDepth => _model.UndoDepth;
    public int RedoDepth => _model.RedoDepth;
    public int SelectedRowIndex { get => _selectedRowIndex; set { if (value != _selectedRowIndex) { _selectedRowIndex = value; OnPropertyChanged(); } } }
    public string NewTimelineIdText { get => _newTimelineIdText; set { if (value != _newTimelineIdText) { _newTimelineIdText = value; OnPropertyChanged(); } } }
    public string NewParameterIdText { get => _newParameterIdText; set { if (value != _newParameterIdText) { _newParameterIdText = value; OnPropertyChanged(); } } }
    public string NewPositionText { get => _newPositionText; set { if (value != _newPositionText) { _newPositionText = value; OnPropertyChanged(); } } }
    public string NewValueText { get => _newValueText; set { if (value != _newValueText) { _newValueText = value; OnPropertyChanged(); } } }
    public string SelectedRowValueText { get => _selectedRowValueText; set { if (value != _selectedRowValueText) { _selectedRowValueText = value; OnPropertyChanged(); } } }

    public bool RegisterTimeline(string? id)
    {
        if (!_model.RegisterTimeline(id))
        {
            SetStatus("註冊失敗：ID 無效、重複或容量已滿");
            return false;
        }
        RefreshTimelineIds();
        SetStatus($"已註冊 {id}");
        return true;
    }

    public bool Select(string? id)
    {
        if (!_model.Select(id))
        {
            SetStatus("選取失敗：ID 不存在或草稿進行中");
            return false;
        }
        SelectedRowIndex = -1;
        RefreshRows();
        OnPropertyChanged(nameof(SelectedTimelineId));
        OnPropertyChanged(nameof(IsDirty));
        OnPropertyChanged(nameof(UndoDepth));
        OnPropertyChanged(nameof(RedoDepth));
        OnPropertyChanged(nameof(CanUndo));
        OnPropertyChanged(nameof(CanRedo));
        SetStatus($"已選取 {id}");
        return true;
    }

    public bool BeginEdit()
    {
        if (!_model.BeginEdit())
        {
            SetStatus("無法開始草稿（未選取或已有草稿）");
            return false;
        }
        RefreshRows();
        OnPropertyChanged(nameof(HasEditSession));
        OnPropertyChanged(nameof(CanUndo));
        OnPropertyChanged(nameof(CanRedo));
        SetStatus("編輯草稿開始");
        return true;
    }

    public bool Discard()
    {
        if (!_model.Discard())
        {
            SetStatus("沒有進行中的草稿");
            return false;
        }
        RefreshRows();
        OnPropertyChanged(nameof(HasEditSession));
        SetStatus("草稿已捨棄");
        return true;
    }

    public bool Commit()
    {
        if (!_model.Commit())
        {
            SetStatus("草稿驗證失敗，請修正或捨棄");
            return false;
        }
        RefreshRows();
        OnPropertyChanged(nameof(HasEditSession));
        OnPropertyChanged(nameof(IsDirty));
        OnPropertyChanged(nameof(UndoDepth));
        OnPropertyChanged(nameof(CanUndo));
        OnPropertyChanged(nameof(CanRedo));
        SetStatus("草稿已提交發布");
        return true;
    }

    public bool Undo()
    {
        if (!_model.Undo())
        {
            SetStatus("目前無法復原（草稿進行中或沒有歷史）");
            return false;
        }
        SelectedRowIndex = -1;
        RefreshRows();
        OnPropertyChanged(nameof(IsDirty));
        OnPropertyChanged(nameof(UndoDepth));
        OnPropertyChanged(nameof(RedoDepth));
        OnPropertyChanged(nameof(CanUndo));
        OnPropertyChanged(nameof(CanRedo));
        SetStatus("已復原");
        return true;
    }

    public bool Redo()
    {
        if (!_model.Redo())
        {
            SetStatus("目前無法重做（草稿進行中或沒有歷史）");
            return false;
        }
        SelectedRowIndex = -1;
        RefreshRows();
        OnPropertyChanged(nameof(IsDirty));
        OnPropertyChanged(nameof(UndoDepth));
        OnPropertyChanged(nameof(RedoDepth));
        OnPropertyChanged(nameof(CanUndo));
        OnPropertyChanged(nameof(CanRedo));
        SetStatus("已重做");
        return true;
    }

    public bool SaveSelected()
    {
        if (!_model.SaveSelected())
        {
            SetStatus("保存失敗（未選取或草稿進行中）");
            return false;
        }
        OnPropertyChanged(nameof(IsDirty));
        SetStatus("已保存至儲存槽");
        return true;
    }

    // Removes the selected timeline through the model's bounded removal and
    // refreshes the projected timeline list. The stale selection index is
    // cleared so a follow-up action cannot target shifted entries.
    public bool RemoveSelectedTimeline()
    {
        if (!_model.RemoveSelected())
        {
            SetStatus("刪除失敗（未選取時間軸或草稿進行中）");
            return false;
        }
        SelectedRowIndex = -1;
        RefreshTimelineIds();
        RefreshRows();
        OnPropertyChanged(nameof(SelectedTimelineId));
        OnPropertyChanged(nameof(HasSelection));
        OnPropertyChanged(nameof(IsDirty));
        OnPropertyChanged(nameof(UndoDepth));
        OnPropertyChanged(nameof(RedoDepth));
        OnPropertyChanged(nameof(CanUndo));
        OnPropertyChanged(nameof(CanRedo));
        SetStatus("已刪除選取時間軸");
        return true;
    }

    // Surface-level history reset mirrors the native supervisor facade. It is
    // valid with or without a selection and never changes rows, dirty state or
    // an open draft.
    public bool ClearHistory()
    {
        _model.ClearHistory();
        OnPropertyChanged(nameof(UndoDepth));
        OnPropertyChanged(nameof(RedoDepth));
        OnPropertyChanged(nameof(CanUndo));
        OnPropertyChanged(nameof(CanRedo));
        SetStatus("編輯歷史已清除");
        return true;
    }

    // Parses the three bound text fields with invariant culture and forwards
    // a validated upsert into the open draft. Any parse failure is refused
    // without touching draft state.
    public bool UpsertFromFields()
    {
        if (!TryParseFields(out var parameterId, out var position, out var value))
        {
            SetStatus("欄位格式無效，已拒絕");
            return false;
        }
        if (!_model.Upsert(new Vst3TimelineSurfaceModelV1.TimelineEvent(parameterId, position, value)))
        {
            RefreshRows();
            SetStatus("事件超出限制，已拒絕");
            return false;
        }
        RefreshRows();
        SetStatus("事件已加入草稿");
        return true;
    }

    // Replaces the normalized value of the selected draft row from bound
    // text; the ordering key stays untouched.
    public bool SetSelectedRowValue(string? valueText)
    {
        if (_selectedRowIndex < 0 || !TryParseValue(valueText, out var value))
        {
            SetStatus("欄位格式無效或未選取列，已拒絕");
            return false;
        }
        if (!_model.SetValueAt(_selectedRowIndex, value))
        {
            SetStatus("數值修改被拒絕");
            return false;
        }
        RefreshRows();
        SetStatus("數值已更新於草稿");
        return true;
    }

    // Removes the currently selected row from the open draft. Rows are
    // renumbered by the next refresh and the stale index is cleared so a
    // follow-up edit cannot target shifted rows.
    public bool RemoveSelectedRow()
    {
        if (!HasEditSession || _selectedRowIndex < 0 ||
            !_model.RemoveAt(_selectedRowIndex))
        {
            SetStatus("無法刪除（未選取列、沒有草稿或索引無效）");
            return false;
        }
        SelectedRowIndex = -1;
        RefreshRows();
        SetStatus("已刪除選取列");
        return true;
    }

    private bool TryParseFields(out uint parameterId, out ulong position, out double value)
    {
        parameterId = 0U;
        position = 0UL;
        value = 0.0;
        return uint.TryParse(_newParameterIdText ?? string.Empty, NumberStyles.None,
                             CultureInfo.InvariantCulture, out parameterId) &&
               ulong.TryParse(_newPositionText ?? string.Empty, NumberStyles.None,
                              CultureInfo.InvariantCulture, out position) &&
               TryParseValue(_newValueText, out value);
    }

    private static bool TryParseValue(string? text, out double value) =>
        double.TryParse(text ?? string.Empty, NumberStyles.Float, CultureInfo.InvariantCulture, out value) &&
        double.IsFinite(value);

    private void RefreshTimelineIds()
    {
        _timelineIds = _model.TimelineIds;
        OnPropertyChanged(nameof(TimelineIds));
    }

    private void RefreshRows()
    {
        var source = _model.Draft ?? _model.Published;
        var rows = new List<TimelineEventRow>(source.Count);
        for (var index = 0; index < source.Count; ++index)
        {
            var item = source[index];
            rows.Add(new TimelineEventRow(index, item.ParameterId, item.SamplePosition, item.NormalizedValue));
        }
        _rows = rows;
        OnPropertyChanged(nameof(Rows));
    }

    private void SetStatus(string text)
    {
        _statusText = text;
        OnPropertyChanged(nameof(StatusText));
    }

    private void OnPropertyChanged([System.Runtime.CompilerServices.CallerMemberName] string? propertyName = null) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName ?? string.Empty));
}

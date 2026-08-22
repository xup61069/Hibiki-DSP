// SPDX-License-Identifier: GPL-3.0-only

namespace Hibiki.ControlModel;

// UI/control-plane mirror for the merged VST3 supervisor timeline editing
// surface (Vst3TimelineSupervisorSurfaceV1, Issue #351). The engine facade
// remains the authoritative owner of store persistence; this bounded managed
// model keeps an identical fail-closed editing contract for the future
// supervisor UI: canonical (sample_position, parameter_id) ordering, 256-event
// and 16-parameter capacity, normalized [0,1] values, one draft transaction at
// a time, bounded undo/redo history of 8 published snapshots and derived dirty
// state. It owns no IPC frame, worker, audio buffer or file handle and never
// runs on the RT thread.
public sealed class Vst3TimelineSurfaceModelV1
{
    public const int MaxEvents = 256;
    public const int MaxParameters = 16;
    public const int MaxHistoryDepth = 8;
    public const int MaxTimelineIds = 16;
    public const int MaxIdLength = 64;

    public readonly record struct TimelineEvent(uint ParameterId, ulong SamplePosition, double NormalizedValue);

    private static readonly string[] ReservedStems =
    {
        "CON", "PRN", "AUX", "NUL",
        "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"
    };

    private readonly Dictionary<string, List<TimelineEvent>> _storedTimelines =
        new(StringComparer.Ordinal);
    private readonly List<TimelineEvent> _published = new(MaxEvents);
    private readonly List<TimelineEvent> _baseline = new(MaxEvents);
    private readonly List<List<TimelineEvent>> _undo = new(MaxHistoryDepth);
    private readonly List<List<TimelineEvent>> _redo = new(MaxHistoryDepth);
    private List<TimelineEvent>? _draft;

    public IReadOnlyList<string> TimelineIds => _storedTimelines.Keys.OrderBy(id => id, StringComparer.Ordinal).ToArray();
    public int TimelineIdCount => _storedTimelines.Count;
    public string? SelectedTimelineId { get; private set; }
    public bool HasSelection => SelectedTimelineId is not null;
    public bool HasEditSession => _draft is not null;
    public IReadOnlyList<TimelineEvent> Published => _published;
    public IReadOnlyList<TimelineEvent>? Draft => _draft;
    public bool CanUndo => _undo.Count > 0;
    public bool CanRedo => _redo.Count > 0;
    public int UndoDepth => _undo.Count;
    public int RedoDepth => _redo.Count;

    // Mirrors the store's strict filename-safe ID contract: 1..64 chars,
    // ASCII alphanumeric start, only alnum/'.'/'_'/'-' inside, no trailing
    // dot, no reserved Windows stem (case-insensitive).
    public static bool IsValidTimelineId(string? id)
    {
        if (string.IsNullOrEmpty(id) || id.Length > MaxIdLength) return false;
        if (!IsAsciiAlnum(id[0])) return false;
        foreach (var ch in id)
        {
            if (!(IsAsciiAlnum(ch) || ch == '.' || ch == '_' || ch == '-')) return false;
        }
        if (id[^1] == '.') return false;
        var stem = id.Split('.')[0];
        foreach (var reserved in ReservedStems)
        {
            if (string.Equals(stem, reserved, StringComparison.OrdinalIgnoreCase)) return false;
        }
        return true;
    }

    // Registers an empty stored timeline slot (store listing mirror). Sorted
    // enumeration is derived; capacity is 16 entries like the native store.
    public bool RegisterTimeline(string? id)
    {
        if (!IsValidTimelineId(id) || _storedTimelines.ContainsKey(id!)) return false;
        if (_storedTimelines.Count >= MaxTimelineIds) return false;
        _storedTimelines[id!] = new List<TimelineEvent>(MaxEvents);
        return true;
    }

    // Loads one stored timeline as the published snapshot plus dirty baseline.
    // Refused while an edit session is open or the ID is unknown; a failure
    // keeps the previous selection untouched.
    public bool Select(string? id)
    {
        if (_draft is not null || id is null || !_storedTimelines.TryGetValue(id, out var stored))
        {
            return false;
        }
        SelectedTimelineId = id;
        _published.Clear();
        _published.AddRange(stored);
        _baseline.Clear();
        _baseline.AddRange(stored);
        _undo.Clear();
        _redo.Clear();
        return true;
    }

    public bool BeginEdit()
    {
        if (_draft is not null || !HasSelection) return false;
        _draft = new List<TimelineEvent>(_published);
        return true;
    }

    public bool Discard()
    {
        if (_draft is null) return false;
        _draft = null;
        return true;
    }

    // Publishes the draft only after full snapshot validation; on failure the
    // draft is kept so the caller can repair or discard it.
    public bool Commit()
    {
        if (_draft is null) return false;
        if (!IsValidSnapshot(_draft)) return false;
        PushUndo(_published);
        _redo.Clear();
        _published.Clear();
        _published.AddRange(_draft);
        _draft = null;
        return true;
    }

    // Insert-or-replace by (sample_position, parameter_id) inside the draft.
    public bool Upsert(TimelineEvent item)
    {
        if (_draft is null) return false;
        if (!double.IsFinite(item.NormalizedValue) ||
            item.NormalizedValue < 0.0 || item.NormalizedValue > 1.0)
        {
            return false;
        }
        var index = FindKeyIndex(_draft, item.SamplePosition, item.ParameterId);
        if (index >= 0)
        {
            _draft[index] = item;
            return true;
        }
        if (_draft.Count >= MaxEvents) return false;
        if (!ParameterWouldStayBounded(_draft, item.ParameterId)) return false;
        var insertAt = CountKeysBefore(_draft, item.SamplePosition, item.ParameterId);
        _draft.Insert(insertAt, item);
        return true;
    }

    public bool RemoveAt(int index)
    {
        if (_draft is null || index < 0 || index >= _draft.Count) return false;
        _draft.RemoveAt(index);
        return true;
    }

    // Replaces only the normalized value; the ordering key stays untouched.
    public bool SetValueAt(int index, double normalizedValue)
    {
        if (_draft is null || index < 0 || index >= _draft.Count) return false;
        if (!double.IsFinite(normalizedValue) ||
            normalizedValue < 0.0 || normalizedValue > 1.0)
        {
            return false;
        }
        var existing = _draft[index];
        _draft[index] = existing with { NormalizedValue = normalizedValue };
        return true;
    }

    // History moves published snapshots only and is refused mid-draft.
    public bool Undo()
    {
        if (_draft is not null || _undo.Count == 0) return false;
        var previous = _undo[^1];
        _undo.RemoveAt(_undo.Count - 1);
        _redo.Add(new List<TimelineEvent>(_published));
        _published.Clear();
        _published.AddRange(previous);
        return true;
    }

    public bool Redo()
    {
        if (_draft is not null || _redo.Count == 0) return false;
        var next = _redo[^1];
        _redo.RemoveAt(_redo.Count - 1);
        _undo.Add(new List<TimelineEvent>(_published));
        _published.Clear();
        _published.AddRange(next);
        return true;
    }

    // Persists the published snapshot into the selected timeline's storage
    // slot and re-baselines dirty tracking.
    public bool SaveSelected()
    {
        if (_draft is not null || !HasSelection) return false;
        var stored = _storedTimelines[SelectedTimelineId!];
        stored.Clear();
        stored.AddRange(_published);
        _baseline.Clear();
        _baseline.AddRange(_published);
        return true;
    }

    // Derived dirty state: a selection exists and its published snapshot
    // differs from the last loaded/saved baseline. An open draft is reported
    // by HasEditSession, not by this flag.
    public bool IsDirty()
    {
        if (!HasSelection) return false;
        return !_published.SequenceEqual(_baseline);
    }

    private void PushUndo(IReadOnlyList<TimelineEvent> snapshot)
    {
        if (_undo.Count >= MaxHistoryDepth) _undo.RemoveAt(0);
        _undo.Add(new List<TimelineEvent>(snapshot));
    }

    internal static bool IsValidSnapshot(IReadOnlyList<TimelineEvent> events)
    {
        if (events.Count > MaxEvents) return false;
        var seen = new HashSet<uint>();
        for (var index = 0; index < events.Count; ++index)
        {
            var item = events[index];
            if (!double.IsFinite(item.NormalizedValue) ||
                item.NormalizedValue < 0.0 || item.NormalizedValue > 1.0)
            {
                return false;
            }
            if (index > 0)
            {
                var prior = events[index - 1];
                if (item.SamplePosition < prior.SamplePosition ||
                    (item.SamplePosition == prior.SamplePosition && item.ParameterId <= prior.ParameterId))
                {
                    return false;
                }
            }
            seen.Add(item.ParameterId);
        }
        return seen.Count <= MaxParameters;
    }

    private static int FindKeyIndex(List<TimelineEvent> events, ulong position, uint parameterId)
    {
        for (var index = 0; index < events.Count; ++index)
        {
            var candidate = events[index];
            if (candidate.SamplePosition == position && candidate.ParameterId == parameterId)
            {
                return index;
            }
            if (candidate.SamplePosition > position) break;
        }
        return -1;
    }

    private static int CountKeysBefore(List<TimelineEvent> events, ulong position, uint parameterId)
    {
        var count = 0;
        foreach (var candidate in events)
        {
            if (candidate.SamplePosition < position ||
                (candidate.SamplePosition == position && candidate.ParameterId < parameterId))
            {
                ++count;
                continue;
            }
            break;
        }
        return count;
    }

    private static bool ParameterWouldStayBounded(List<TimelineEvent> events, uint parameterId)
    {
        var seen = new HashSet<uint> { parameterId };
        foreach (var candidate in events)
        {
            seen.Add(candidate.ParameterId);
        }
        return seen.Count <= MaxParameters;
    }

    private static bool IsAsciiAlnum(char ch) =>
        (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
}

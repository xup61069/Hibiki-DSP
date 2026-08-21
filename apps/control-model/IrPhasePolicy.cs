// SPDX-License-Identifier: GPL-3.0-only

namespace Hibiki.ControlModel;

public enum IrPhaseMode
{
    MinimumPhase,
    MixedPhase,
    LinearPhase,
    Bypass
}

public sealed record IrPhaseModeOption(IrPhaseMode Mode, string Label, string Detail);

public readonly record struct IrPhasePolicyV1(IrPhaseMode Mode, double Strength)
{
    public const uint SchemaVersion = 1;
    public const double BalancedMaxDelayMs = 80.0;
    public const double MovieMaxDelayMs = 160.0;

    public bool IsValid => double.IsFinite(Strength) && Strength is >= 0.0 and <= 1.0 &&
                           (Mode != IrPhaseMode.Bypass || Strength == 0.0);

    public bool UsesFir => IsValid && (Mode is IrPhaseMode.MixedPhase or IrPhaseMode.LinearPhase) &&
                           Strength > 0.0;

    public double AddedDelayMs => !IsValid ? 0.0 : Mode switch
    {
        IrPhaseMode.MixedPhase => Strength * BalancedMaxDelayMs,
        IrPhaseMode.LinearPhase => Strength * MovieMaxDelayMs,
        _ => 0.0
    };

    public IrPhasePolicyV1 WithStrength(double strength) =>
        this with { Strength = Math.Clamp(strength, 0.0, 1.0) };
}

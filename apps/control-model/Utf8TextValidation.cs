using System.Text;

namespace Hibiki.ControlModel;

internal static class Utf8TextValidation
{
    private static readonly UTF8Encoding StrictUtf8 =
        new(encoderShouldEmitUTF8Identifier: false, throwOnInvalidBytes: true);

    internal static bool IsPrintable(string? value, int maxBytes, bool allowEmpty = true)
    {
        if (value is null || (!allowEmpty && value.Length == 0)) return false;
        if (value.Any(char.IsControl)) return false;
        try
        {
            return StrictUtf8.GetByteCount(value) <= maxBytes;
        }
        catch (EncoderFallbackException)
        {
            return false;
        }
    }
}

using Hibiki.ControlModel;

static void Check(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException(message);
}

static PhysicalDeviceCard Card(
    PhysicalDeviceAvailabilityV1 availability = PhysicalDeviceAvailabilityV1.Active,
    bool isDefault = true,
    ulong sequence = 10UL) =>
    new("endpoint-speaker", "Speaker", PhysicalDeviceFlowV1.Render,
        availability, 2, 48000, 128, isDefault, sequence);

var catalog = new PhysicalDeviceCatalogV1();
Check(catalog.Upsert(Card(), out var upsertError) && upsertError.Length == 0,
      "A valid baseline physical device must be accepted.");

Check(catalog.SetAvailability("endpoint-speaker", PhysicalDeviceAvailabilityV1.Disabled,
                             11UL, out var disabledError) && disabledError.Length == 0,
      "Disabled availability must be accepted.");
Check(catalog.TryGet("endpoint-speaker", out var disabled) && disabled is not null &&
      disabled.Availability == PhysicalDeviceAvailabilityV1.Disabled &&
      !disabled.IsDefault && disabled.LastSequence == 11UL,
      "Disabled transition must clear default and advance sequence.");

Check(catalog.SetAvailability("endpoint-speaker", PhysicalDeviceAvailabilityV1.Unplugged,
                             12UL, out var unpluggedError) && unpluggedError.Length == 0,
      "Unplugged availability must be accepted.");
Check(catalog.SetAvailability("endpoint-speaker", PhysicalDeviceAvailabilityV1.Unknown,
                             13UL, out var unknownError) && unknownError.Length == 0,
      "Unknown availability must be accepted.");
Check(catalog.SetAvailability("endpoint-speaker", PhysicalDeviceAvailabilityV1.Active,
                             14UL, out var activeError) && activeError.Length == 0,
      "Active availability must be accepted.");
Check(catalog.TryGet("endpoint-speaker", out var active) && active is not null &&
      active.Availability == PhysicalDeviceAvailabilityV1.Active &&
      !active.IsDefault && active.LastSequence == 14UL,
      "Valid availability transitions must preserve the card contract.");

var beforeStale = catalog.Devices.Single();
var beforeStaleCatalogSequence = catalog.CatalogSequence;
Check(!catalog.SetAvailability("endpoint-speaker", PhysicalDeviceAvailabilityV1.Disabled,
                              13UL, out var staleError) && staleError.Length > 0,
      "A stale valid availability event must be rejected.");
Check(catalog.TryGet("endpoint-speaker", out var afterStale) && afterStale == beforeStale &&
      catalog.CatalogSequence == beforeStaleCatalogSequence,
      "A stale availability event must preserve the card and catalog sequence.");

var beforeInvalid = catalog.Devices.Single();
var beforeCatalogSequence = catalog.CatalogSequence;
Check(!catalog.SetAvailability("endpoint-speaker", (PhysicalDeviceAvailabilityV1)99,
                              999UL, out var invalidError) &&
      invalidError.Length > 0,
      "An undefined availability enum must be rejected with an error.");
Check(catalog.TryGet("endpoint-speaker", out var afterInvalid) && afterInvalid is not null &&
      afterInvalid == beforeInvalid && catalog.CatalogSequence == beforeCatalogSequence,
      "Invalid availability must preserve the card and catalog sequence.");

Check(!catalog.SetAvailability("endpoint-speaker", (PhysicalDeviceAvailabilityV1)(-1),
                              1000UL, out var negativeError) && negativeError.Length > 0,
      "A negative undefined availability enum must also be rejected.");
Check(catalog.TryGet("endpoint-speaker", out var afterNegative) && afterNegative == beforeInvalid,
      "Negative invalid availability must preserve the prior card.");

Check(!catalog.SetAvailability("missing-endpoint", (PhysicalDeviceAvailabilityV1)99,
                              1001UL, out var missingInvalidError) &&
      missingInvalidError == "裝置狀態不受支援",
      "Enum validation must run before endpoint lookup for malformed status events.");

Console.WriteLine("Control-model physical-device availability enum checks passed.");

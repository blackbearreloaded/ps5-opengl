# SDL controller input and reconnect

The SDL2 bridge has a focused physical-input qualification on one
firmware-6.02 console, one controller and one signed-in user at 1440p.
The operator corroborated the button/stick actions and corresponding screen colors.

## Qualified behavior

- Cross press/release and left-stick travel/return to center.
- Controller removal and reconnection with a new SDL instance ID.
- Fresh button and stick events after reconnect.
- Matching 1440p119.88 HDMI and 59.94-Hz restoration.
- Successful SDL cleanup, title teardown and post-run service health.

This is not all-button, multi-controller or 4K input coverage. SDL events alone
do not authenticate physical origin, and rendering a screen does not prove input.

## Test contract

The opt-in `input-validation` consumer is bounded at 120 seconds. It requires
a connected nonvirtual controller, button/stick actions, removal, a new attached
instance and fresh post-reconnect activity. Timeout or early quit is incomplete.
Operator confirmation is separate from the numerical/event records.

Host checks use real SDL event handling and the PS5 joystick backend with
explicit platform doubles under ASan/UBSan. They cover ownership, failure and
reinitialization, plus rejection of virtual or inconsistent synthetic events.
Those checks are not physical-controller qualification.

See the [SDL integration instructions](../integration/SDL2/README.md#physical-input-and-reconnect)
for build and audit commands, and [testing](testing.md) for lifecycle requirements.

## Evidence identity

Source: `9c124202845b9b6acdfe831d62151e18b0cb514b`.

Native executable SHA-256:
`6e4ee48c8970dd956d829e984a2b1930f96e0c2cdb2c350b30623c422680eab9`.

The exact-binary qualification does not transfer automatically to another
SDK/SDL pair, firmware, display profile or controller. Raw receipts stay local.

# D6 360-degree obstacle avoidance v1

## Control behavior

- Obstacle avoidance is enabled after every reboot.
- `START` toggles autonomous forward search, not avoidance itself.
- PS2 and cloud motion commands pass through the same constraint layer.
- The module changes only the requested `RX`, `RY`, and `LX` axes.
- It never writes servo angles and never changes gait timing.
- Motion away from an obstacle remains available.
- A missing or stale radar fails open so the original controls remain usable.

## Radar orientation

The current configuration assumes:

- D6 raw `0 deg` points toward the robot front.
- Raw angles increase clockwise.
- Therefore `90 deg` is right, `180 deg` is rear, and `270 deg` is left.

These values are configured in
`MDK-ARM/USER/APP/obstacle_avoidance.h`:

```c
#define OBSTACLE_RADAR_FRONT_ANGLE_DEG 0U
#define OBSTACLE_RADAR_ANGLE_CLOCKWISE 1U
```

The forward slowdown test has confirmed the current mounting direction.
If the D6 zero mark is moved later, change
`OBSTACLE_RADAR_FRONT_ANGLE_DEG`.

## Distance processing

The scan is split into four overlapping 90-degree sectors:

- Front: `315-45 deg`
- Right: `45-135 deg`
- Rear: `135-225 deg`
- Left: `225-315 deg`

Each sector uses the second-nearest fresh point. This rejects one isolated
short-distance sample while still responding to small obstacles.

Samples are accepted only when:

- Distance is `120-12000 mm`.
- Point age is at most `500 ms`.
- The latest complete radar data frame is at most `300 ms` old.

## Constraints

| Distance | Result |
|---|---|
| `>= 800 mm` | Command passes unchanged |
| `500-800 mm` | Command is reduced linearly |
| `250-500 mm` | Motion toward the obstacle is blocked |
| `<= 250 mm` | Motion is blocked and marked emergency |

Forward/backward uses the front/rear sectors. Strafe and in-place rotation
use the right/left sectors. The first-order velocity filter is clamped and
reset when avoidance blocks an axis, preventing residual filtered motion.

## UART8 diagnostics

The status line is:

```text
[AVOID] en=0 ready=0 state=0 mm=0,0,0,0 in=0,0,0 out=0,0,0 ...
```

`mm` order is front, right, rear, left. States:

- `0`: disabled
- `1`: clear
- `2`: slowing
- `3`: blocked
- `4`: emergency
- `5`: radar stale

Before the first autonomous floor test, raise the robot and place an obstacle
successively at the front, right, rear, and left. Confirm that the smallest
value moves through the corresponding `mm` field in that order.

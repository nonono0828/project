# Autonomous forward search v1

## Controls

- Press `START` once to start autonomous forward search.
- Press `START` again to stop.
- Any PS2 joystick movement or active cloud motion command stops autonomous
  mode immediately and leaves that manual command in control.
- `R1` terrain mode and `R2` speed mode remain available.
- The selected speed mode scales autonomous commands as well.

Obstacle avoidance remains enabled in both manual and autonomous modes.

## State machine

```text
WAIT_RADAR
    |
    v
FORWARD -- front obstacle --> choose clearer left/right side
    ^                              |
    |                              v
    +------ front clear ------- TURN_LEFT / TURN_RIGHT
                                   |
                                   +-- timeout or both sides blocked --> REVERSE
                                                                         |
                                                                         +-- 800 ms --> choose side
```

When front distance reaches `250 mm`, reverse is preferred if the rear is
clear. If front, rear, left, and right are all blocked, the robot enters
`TRAPPED` and remains stopped until one direction becomes available.

Radar loss is fail-safe for autonomous mode: the robot stops in
`WAIT_RADAR`. Manual mode retains the obstacle module's fail-open behavior.

## Commands and thresholds

| Item | Value |
|---|---:|
| Forward command | `RY = 90` |
| Turn command | `LX = +/-85` |
| Reverse command | `RY = -70` |
| Start avoiding | Front `<= 650 mm` |
| Resume forward | Front `> 900 mm` |
| Side considered blocked | `<= 500 mm` |
| Rear considered blocked | `<= 500 mm` |
| Minimum turn duration | `350 ms` |
| Turn timeout | `2500 ms` |
| Reverse duration | `800 ms` |

## UART8

```text
[AUTO] en=1 ready=1 state=2 cmd=0,90,0 transition=2 ...
```

States:

- `0`: disabled
- `1`: waiting for radar
- `2`: forward
- `3`: turning left
- `4`: turning right
- `5`: reversing
- `6`: trapped
